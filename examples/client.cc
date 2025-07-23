#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <cassert>
#include <thread>
#include <glog/logging.h>

#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/TcpClient.h"
#include "network/TcpConnection.h"
#include "rpc_framework/RpcChannel.h"
#include "monitor.pb.h"

using namespace network;

class RpcClient {
public:
    RpcClient(EventLoop *loop, const InetAddress &serverAddr)
        : loop_(loop),
        client_(loop, serverAddr, "RpcClient"),
        channel_(new RpcChannel),
        stub_(get_pointer(channel_)) {
        client_.setConnectionCallback(
            std::bind(&RpcClient::onConnection, this, _1));
        client_.setMessageCallback(
            std::bind(&RpcChannel::onMessage, get_pointer(channel_), _1, _2));
    }

    void SetMonitorInfo(const monitor::TestRequest &request) {
        // 创建了一个新的响应对象，用于接收 RPC 调用的返回结果。用 new 分配在堆上，后续需要手动释放
        monitor::TestResponse *response = new monitor::TestResponse();

        /**
         * 发起 RPC 调用
         * stub_：通常是一个 RPC 客户端的代理对象（stub），用于发起远程调用。
         * MonitorInfo：远程过程调用的方法名
         * NULL：一般是 RPC 的控制参数（如超时、上下文等），这里传了空指针
         * &request：请求参数，传递给远程服务。
         * response：用于接收远程服务的响应
         * NewCallback(this, &RpcClient::closure, response)：回调函数，RPC 调用完成后会自动调用这个回调。
         * 这里传递了当前对象 this，成员函数指针 &RpcClient::closure，以及 response 作为参数
         */

        /**
         * 在 protobuf RPC 框架中，MonitorInfo 这个方法在客户端的 stub（如 TestService::Stub）里只是一个代理函数。
         * 你在客户端调用 stub->MonitorInfo(request, response, done)，并不是直接调用服务器上的函数，而是：
         * 1. 把请求序列化成二进制数据。
         * 2. 通过网络发送到服务器。
         * 3. 服务器收到后，反序列化，调用真正的服务实现（如 MonitorInfo），处理完后把结果序列化发回客户端。
         * 4. 客户端收到响应后，反序列化填充到 response 对象。
         */

        /**
         * // 客户端代码
         * stub->MonitorInfo(request, response, done); // 这里不会立刻有结果
         * // 实际流程
         * 1. stub->MonitorInfo() 只是发起请求
         * 2. 事件循环驱动数据发送
         * 3. 服务器收到请求，调用真正的 MonitorInfo 实现，填充 response
         * 4. 服务器把 response 通过网络发回客户端
         * 5. 客户端事件循环检测到响应到达，反序列化填充 response
         * 6. 如果有 done 回调，自动调用
         */

        /**
         * MonitorInfo 是你在 .proto 文件里定义的一个 RPC 服务方法，比如：
         * service TestService {
         *     rpc MonitorInfo (TestRequest) returns (TestResponse);
         * }
         * protoc 编译后，会自动生成 C++ 代码，包含：
         * 服务基类（如 TestService::Service），里面有虚函数 MonitorInfo，需要你在服务器端继承并实现。
         * 客户端存根类（如 TestService::Stub），里面有 MonitorInfo 方法，负责发起 RPC 请求。
         * 客户端中的Stub中的MonitorInfo 方法不是本地实现，
         * 而是把请求序列化后通过网络发给服务器，服务器收到后调用你实现的 MonitorInfo，处理完再把结果返回客户端。
         * 总结: 
         * MonitorInfo 的真正实现是在服务器端，由你继承并重写 proto 生成的 Service 基类的虚函数。
         * 客户端的 MonitorInfo 只是一个代理，负责发起 RPC 请求，实际处理逻辑在服务器端。
         */

        /**
         * 数据是怎么“发出去”的？
         * stub_.MonitorInfo(...) 内部会调用 RpcChannel::CallMethod，
         * 而 RpcChannel 里会把数据写入发送缓冲区，并注册/激活 socket 的“可写事件”。
         * Stub 不关心底层通信细节，它只知道要把请求交给 RpcChannel。
         * 所以，所有的 Stub 方法（如 MonitorInfo）内部最终都会调用 RpcChannel::CallMethod，让通道负责实际的网络通信
         */

        /**
         * NewCallback 用于生成一个“回调对象”，当异步操作完成时自动调用你指定的成员函数或普通函数。
         * NewCallback 生成的回调对象的“异步操作完成”，指的是“收到来自服务端的回复”
         */
        stub_.MonitorInfo(NULL, &request, response,
                            NewCallback(this, &RpcClient::closure, response));
    }

    void connect() { client_.connect(); }

    /**
     * [EventLoop]
        |
        |---> [Channel(fd1)] <--- [TcpConnection1]
        |---> [Channel(fd2)] <--- [TcpConnection2]
        |---> [Channel(fd3)] <--- [TcpConnection3]
     */

private:
    void onConnection(const TcpConnectionPtr &conn) {
        if (conn->connected()) {
            channel_->setConnection(conn);
        } else {
            RpcClient::connect();
        }
    }

    void closure(monitor:TestResponse *resp) {
        LOG(INFO) << " resp: \n " << resp->DebugString();
    }

    EventLoop *loop_;
    TcpClient client_;
    TpcChannelPtr channel_;

    /**
     * 这是由 protobuf 的 service 语法自动生成的客户端存根（Stub）对象。
     * 作用：Stub 封装了 RPC 客户端调用的细节，让你像调用本地函数一样去调用远程服务
     * monitor::TestService 是你在 .proto 文件里定义的服务名，Stub 是 protoc 生成的内部类，专门用于客户端
     */
    monitor::TestService::Stub stub_;
};

int main(int argc, char *argv[]) {
    LOG(INFO) << " pid = " << getpid();

    /**
     * 启动一个 RPC 客户端，连接到指定服务器
     * 后台线程每 3 秒向服务器发送一次监控请求（内容为 name="cpu0"，count 递增）
     * 主线程负责事件循环，处理网络事件
     * 如果没有传入服务器 IP，则提示用法
     */
    if (argc > 1) {
        EventLoop loop; // 创建 EventLoop
        InetAddress serverAddr(argv[1], 9981); // 创建服务器地址

        // 创建 RpcClient 并连接
        RpcClient rpcClient(&loop, serverAddr);
        rpcClient.connect();
        std::unique_ptr<std::thread> thread_ = nullptr;
        
        // 创建并启动线程
        int count = 0;
        thread_ = std::make_unique<std::thread>([&]() {
            while (true) {
                count++;

                monitor::TestRequest request;
                request.set_name("cpu0");
                request.set_count(count);

                // 通过 rpcClient（一个 RPC 客户端对象）向远程服务器发送一个监控信息请求（request）
                rpcClient.SetMonitorInfo(request);
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        });
        thread_->detach();
        loop.loop(); // 主线程进入事件循环，处理网络事件。
    } else {
        printf("Usage: %s host_ip\n", argv[0]);
    }

    /**
     * ShutdownProtobufLibrary()：这是 protobuf 提供的一个全局函数，
     * 用于在程序结束时释放 protobuf 运行时分配的全局资源，防止内存泄漏
     */
    google::protobuf::ShutdownProtobufLibrary();

    return 0;
}

