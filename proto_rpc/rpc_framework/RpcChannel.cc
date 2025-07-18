#include "RpcChannel.h"

#include <cassert>

#include <glog/logging.h>
#include <google/protobuf/descriptor.h>

#include "rpc.pb.h"
using namespace network;
RpcChannel::RpcChannel()
    : codec_(std::bind(&RpcChannel::onRpcMessage, this, std::placeholders::_1,
                       std::placeholders::_2)),
      services_(NULL) {
  LOG(INFO) << "RpcChannel::ctor - " << this;
}

RpcChannel::RpcChannel(const TcpConnectionPtr &conn)
    : codec_(std::bind(&RpcChannel::onRpcMessage, this, std::placeholders::_1,
                       std::placeholders::_2)),
      conn_(conn),
      services_(NULL) {
  LOG(INFO) << "RpcChannel::ctor - " << this;
}

RpcChannel::~RpcChannel() {
  LOG(INFO) << "RpcChannel::dtor - " << this;
  for (const auto &outstanding : outstandings_) {
    OutstandingCall out = outstanding.second;
    delete out.response;
    delete out.done;
  }
}

void RpcChannel::CallMethod(const ::google::protobuf::MethodDescriptor *method,
                            google::protobuf::RpcController *controller,
                            const ::google::protobuf::Message *request,
                            ::google::protobuf::Message *response,
                            ::google::protobuf::Closure *done) {
  RpcMessage message;
  message.set_type(REQUEST);
  int64_t id = id_.fetch_add(1) + 1;
  message.set_id(id);
  message.set_service(method->service()->full_name());
  message.set_method(method->name());
  message.set_request(request->SerializeAsString());

  OutstandingCall out = {response, done};
  {
    std::unique_lock<std::mutex> lock(mutex_);
    outstandings_[id] = out;
  }
  codec_.send(conn_, message);
}

void RpcChannel::onMessage(const TcpConnectionPtr &conn, Buffer *buf) {
  codec_.onMessage(conn, buf);
}

void RpcChannel::onRpcMessage(const TcpConnectionPtr &conn,
                              const RpcMessagePtr &messagePtr) {
  assert(conn == conn_);
  RpcMessage &message = *messagePtr;
  if (message.type() == RESPONSE) {
    handle_response_msg(messagePtr);
  } else if (message.type() == REQUEST) {
    handle_request_msg(conn, messagePtr);
  }
}

void RpcChannel::handle_response_msg(const RpcMessagePtr &messagePtr) {
  RpcMessage &message = *messagePtr;
  int64_t id = message.id();

  OutstandingCall out = {NULL, NULL};

  {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = outstandings_.find(id);
    if (it != outstandings_.end()) {
      out = it->second;
      outstandings_.erase(it);
    }
  }

  if (out.response) {
    std::unique_ptr<google::protobuf::Message> d(out.response);
    if (!message.response().empty()) {
      out.response->ParseFromString(message.response());
    }
    if (out.done) {
      out.done->Run();
    }
  }
}

// RpcChannel::handle_request_msg 函数是 RPC 服务端处理请求消息的核心函数，
// 它负责查找服务和方法、解析请求消息、调用服务方法，并处理可能出现的错误。
// 通过该函数，RPC 服务端能够正确处理客户端的请求，并返回相应的响应消息
void RpcChannel::handle_request_msg(const TcpConnectionPtr &conn,
                                    const RpcMessagePtr &messagePtr) {
  RpcMessage &message = *messagePtr;
  ErrorCode error = WRONG_PROTO;
  if (services_) {
    // 在 services_ 中查找请求的服务
    std::map<std::string, google::protobuf::Service *>::const_iterator it =
        services_->find(message.service());
    if (it != services_->end()) {
      // 找到服务
      google::protobuf::Service *service = it->second;
      assert(service != NULL);
      // 获取服务的描述符
      const google::protobuf::ServiceDescriptor *desc =
          service->GetDescriptor();
      // 在服务中查找请求的方法
      const google::protobuf::MethodDescriptor *method =
          desc->FindMethodByName(message.method());
      if (method) {
        // 找到方法
        // 创建请求消息对象
        std::unique_ptr<google::protobuf::Message> request(
            service->GetRequestPrototype(method).New());
        if (request->ParseFromString(message.request())) {
          // 成功解析请求消息
          // 创建响应消息对象
          google::protobuf::Message *response =
              service->GetResponsePrototype(method).New();
          // response is deleted in doneCallback
          // 记录消息的 id
          int64_t id = message.id();
          // 调用服务的方法
          service->CallMethod(
              method, NULL, request.get(), response,
              NewCallback(this, &RpcChannel::doneCallback, response, id));
          // 设置错误码为 NO_ERROR
          error = NO_ERROR;
        } else {
          error = INVALID_REQUEST; // 解析请求消息失败
        }
      } else {
        error = NO_METHOD; // 未找到方法
      }
    } else {
      error = NO_SERVICE; // 未找到服务
    }
  } else {
    error = NO_SERVICE; // services_ 为空
  }
  // 如果出现错误，发送包含错误信息的响应消息
  if (error != NO_ERROR) {
    RpcMessage response;
    response.set_type(RESPONSE);
    response.set_id(message.id());
    response.set_error(error);
    codec_.send(conn_, response);
  }
}

// RpcChannel::doneCallback 函数是在 RPC 服务端处理完客户端请求后，
// 用于将响应消息发送回客户端的回调函数。
void RpcChannel::doneCallback(::google::protobuf::Message *response,
                              int64_t id) {
  // 使用智能指针管理响应消息对象，确保资源自动释放
  std::unique_ptr<google::protobuf::Message> d(response);
  RpcMessage message; // 创建一个 RpcMessage 对象，用于封装响应消息
  message.set_type(RESPONSE); // 设置消息类型为 RESPONSE
  message.set_id(id); // 设置消息的唯一标识符
  // 将响应消息序列化为字符串并设置到 RpcMessage 中
  message.set_response(response->SerializeAsString());  // FIXME: error check
  // 使用 ProtoRpcCodec 发送封装好的 RpcMessage 到指定的 TCP 连接
  codec_.send(conn_, message);
}
