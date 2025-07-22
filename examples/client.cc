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
            std::bind(&RpcChannel::onConnection, this, _1));
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

    void closure(monitor:TestResponse *resq) {
        LOG(INFO) << " resq: \n " << resq->DebugString();
    }

    EventLoop *loop_;
    TcpClient client_;
    TpcChannelPtr channel_;
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

