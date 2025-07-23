#include <cassert>
#include <glog/logging>
#include <google/protobuf/descriptor.h>

#include "RpcChannel.h"
#include "rpc.pb.h"

using namespace network;

RpcChannel::RpcChannel() : codec_(std::bind(&RpcChannel::onRpcMessage, this, 
                                            std::placeholders::_1, std::placeholders::_2)),
                            services_(NULL) {
    LOG(INFO) << " RpcChannel::ctor - " << this;
}

RpcChannel::RpcChannel(const TcpConenctionPtr &conn)
    : codec_(std::bind(&RpcChannel::onRpcMessage, this, 
                        std::placeholders::_1, std::placeholders::_2)),
    conn_(conn),
    services_(NULL) {
    LOG(INFO) << " RpcChannel::ctor - " << this;
}

RpcChannel::~RpcChannel() {
    LOG(INFO) << " RpcChannel:dtor - " << this;
    for (const auto &outstanding : outstandings_) {
        OutstandingCall out = outstanding.second;
        delete out.response;
        delete out.done;
    }
}

/**
 * 这段代码是一个 RPC 框架中 RpcChannel 的 CallMethod 方法实现，作用是发起一次远程过程调用（RPC）请求。
 */
void RpcChannel::CallMethod(const ::google::protobuf::MethodDescriptor *method, 
                            google::protobuf::RpcController *controller,
                            const ::google::protobuf::Message *request,
                            ::google::protobuf::Message *response,
                            ::google::protobuf::Closure *done) {
    RpcMessage message; // 创建一个 RpcMessage 对象，表示一次 RPC 请求
    message.set_type(REQUEST); // 设置消息类型为 REQUEST
    int64_t id = id.fetch_add(1) + 1; // 生成唯一的请求 ID（id.fetch_add(1) + 1，线程安全自增）
    message.set_id(id);

    // 设置服务名、方法名
    message.set_service(method->service()->full_name());
    message.set_method(method->name());

    // 把请求消息序列化为字符串，填入 message
    message.set_request(request->SerializeAsString());

    /**
     * 构造一个 OutstandingCall 结构体，保存响应对象和回调
     * 用互斥锁保护，把这次调用的信息（以请求 ID 为 key）存入 outstandings_ 容器，
     * 方便后续收到响应时能找到对应的回调和响应对象
     */ 
    OutstandingCall out = { response, done };
    {
        std::unique_lock<std::mutex> lock(mutex_);
        outstandings_[id] = out;
    }

    // 通过 codec_（编解码器）把构造好的 RpcMessage 发送到远程服务器，
    // 底层用的是网络连接 conn_
    codec_.send(conn_, message);
}

/**
 * 这是 RpcChannel 类的 onMessage 方法。
 * 作用是：当网络连接上收到新数据时，调用 codec_（编解码器）的 onMessage 方法来处理收到的数据
 * 这是典型的 RPC 框架消息分发逻辑
 */
void RpcChannel::onMessage(const TcpConnectionPtr &conn, Buffer *buf) {
    codec_.onMessage(conn, buf);
}

/**
 * 这段代码是 RpcChannel::onRpcMessage 方法的实现，
 * 作用是根据收到的 RPC 消息类型，分发到不同的处理函数
 */
void RpcChannel::onRpcMessage(const TcpConnectionPtr &conn, const RpcMessagePtr &messagePtr) {
    // 检查收到消息的连接和当前通道绑定的连接是否一致，防止出错。
    assert(conn == conn_);

    /**
     * RpcMessage &message = *messagePtr; 的作用是定义一个对该对象的引用，名字叫 message。
     * 不会产生对象的拷贝，效率高
     */
    RpcMessage &message = *messagePtr;

    /**
     * 如果消息类型是 RESPONSE（响应），调用 handle_reponse_msg 处理响应消息。
     * 如果消息类型是 REQUEST（请求），调用 handle_request_msg 处理请求消息。
     */
    if (message.type() == RESPONSE) {
        handle_reponse_msg(messagePtr);
    } else if (message.type() == REQUEST) {
        handle_request_msg(conn, messagePtr);
    }
}

/**
 * 这段代码的作用是根据响应消息的 ID 找到对应的请求，把响应内容反序列化到用户的响应对象里，
 * 并调用用户注册的回调函数
 */
void RpcChannel::handle_response_msg(const RpcMessagePtr &messagePtr) {

    // 解引用消息指针，获取响应 ID
    RpcMessage &message = *messagePtr;
    int64_t id = message.id();

    // 准备查找未完成的调用
    OutstandingCall out = { NULL, NULL };

    // 查找并移除对应的未完成调用
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = outstandings_.find(id);
        if (it != outstandings_.end()) {
            out = it->second;
            outstandings_.erase(it);
        }
    }

    // 处理响应对象和回调
    if (out.response) {
        std::unique_ptr<google::protobuf::Message> d(out.response);

        // 如果响应消息内容不为空，就用 ParseFromString 反序列化填充响应对象。
        if (!message.response().empty()) {
            out.response->ParseFromString(message.response());
        }

        // 如果有回调（done），就调用回调，通知用户“RPC 响应已收到并处理”。
        if (out.done) {
            out.done->Run();
        }
    }
}

/**
 * RpcChannel::handle_request_msg 函数是 RPC 服务端处理请求消息的核心函数，
 * 它负责查找服务和方法、解析请求消息、调用服务方法，并处理可能出现的错误。
 * 通过该函数，RPC 服务端能够正确处理客户端的请求，并返回相应的响应消息
 */
void RpcChannel::handle_request_msg(const TcpConenctionPtr &conn, const RpcMessagePtr &messagePtr) {
    RpcChannel &message = *messagePtr;
    ErrorCode error = WRONG_PROTO;
    if (services_) {
        std::map<std::string, google::protobuf::Service *>::const_iterator it = 
            services_->find(message.service());
        if (it != services_->end()) { // 找到服务
            google::protobuf::Service *service = it->second;
            assert(service != NULL);

            /**
             * 获取服务的描述符
             * 在服务中查找请求的方法
             */
            const google::protobuf::ServiceDescriptor *desc = service->GetDescriptor();
            const google::protobuf::MethodDescriptor *method = desc->FindMethodByName(message.method());

            // 找到方法
            if (method) {

                // 创建请求消息对象
                std::unique_ptr<google::protobuf::Message> request(
                    service->GetRequestPrototype(method).New());
                if (request->ParseFromString(message.request())) { // 成功解析请求消息
                    google::protobuf::Message *response =  // 创建响应消息对象
                        service->GetResponsePrototype(method).New();
                    int64_t id = message.id();

                    /**
                     * 这里的 CallMethod 是 protobuf Service 的虚函数，
                     * 它会根据 method 描述，自动分发到你实现的具体方法（如 MonitorInfo）
                     * 
                     * 当 CallMethod 被调用时，最终会分发到你实现的 MonitorInfo。
                     * 你在 MonitorInfo 里处理完业务逻辑，填充 response。
                     * 你需要在业务逻辑最后调用 done->Run();
                     * 这时，done 实际上就是 NewCallback(this, &RpcChannel::doneCallback, response, id) 生成的 Closure。
                     * 所以，当你调用 done->Run() 时，框架就会自动调用 RpcChannel::doneCallback(response, id)
                     */
                    service->CallMethod(
                        method, NULL, request.get(), response,
                        NewCallback(this, &RpcChannel::doneCallback, response, id));
                    error = NO_ERROR; // 设置错误码为 NO_ERROR
                } else {
                    error = INVALID_REQUEST; // 解析请求消息失败
                }
            } else {
                error = NO_SERVICE; // 未找到服务
            }
        } else {
            error = NO_SERVICE; // services_为空
        }
    }

    // 如果出现错误，发送包含错误信息的响应消息
    if (error != NO_ERROR) {
        RpcMessage response;
        repsonse.set_type(RESPONSE);
        response.set_id(message.id());
        response.set_error(error);
        codec_.send(conn_, response);
    }
}

/**
 * RpcChannel::doneCallback 函数是在 RPC 服务端处理完客户端请求后，
 * 用于将响应消息发送回客户端的回调函数。
 */
void RpcChannel::doneCallback(::google::protobuf::Message *response, int64_t id) {

    // 使用智能指针管理响应消息对象，确保资源自动释放
    std::unique_ptr<google::protobuf::Message> d(response);
    RpcMessage message; // 创建一个 RpcMessage 对象，用于封装响应消息
    message.set_type(RESPONSE); // 设置消息类型为 RESPONSE
    message.set_id(id); // 设置消息的唯一标识符
    message.set_response(reponse->SerializeAsString()); // 将响应消息序列化为字符串并设置到 RpcMessage 中
    codec_.send(conn_, message); // 使用 ProtoRpcCodec 发送封装好的 RpcMessage 到指定的 TCP 连接
}