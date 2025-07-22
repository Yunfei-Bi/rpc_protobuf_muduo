#include <cassert>
#include <glog/logging.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>

#include "RpcServer.h"
#include "RpcChannel.h"

using namespace network;

/**
 * 这段代码是 RpcServer 构造函数的实现，
 * 作用是初始化 RPC 服务器对象，并设置连接回调函数。
 * 初始化成员变量 server_，它是一个 TCP 服务器对象（比如 TcpServer）
 * 传入事件循环、监听地址和服务器名称 "RpcServer"
 * 这样每当有新连接时，server_ 会自动调用 RpcServer::onConnection 方法
 */
RpcServer::RpcServer(EventLoop *loop, const InetAddress &listenAddr)
    : server_(loop, listenAddr, "RpcServer") {
    server_.setConnectionCallback(std::bind(&RpcServer::onConnection, this, _1));
}

/**
 * 这段代码的作用是把一个 protobuf 服务对象注册到服务器的服务表里，以便后续根据服务名查找和分发请求
 * 通过 GetDescriptor() 获取该服务的描述信息（ServiceDescriptor），里面包含服务的名字、方法等元数据
 * 取出服务的全名（desc->full_name()，比如 "myproto.MyService"）。
 * 把服务对象指针存入 services_ 容器（通常是 std::map<std::string, Service*>），以服务全名为 key
 * 这样后续收到 RPC 请求时，可以通过服务名快速查找到对应的服务对象并调用其方法
 */
void RpcServer::registerService(google::protobuf::Service *service) {
    const google::protobuf::ServiceDescriptor *desc = service->GetDescriptor();
    services_[desc->full_name()] = service;
}

void RpcServer::start() { server_.start(); }

/**
 * 这段代码是 RpcServer 类中 onConnection 方法的实现，主要用于处理 TCP 连接的建立和断开。
 * 它的作用是：当有新的客户端连接到 RPC 服务器，或者连接断开时，会自动调用这个函数进行相应的处理
 */
void RpcServer::onConnection(const TcpConnectionPtr &conn) {
    // 打印连接信息，包括客户端地址、服务器地址以及连接状态
    LOG(INFO) << "RpcServer - " << conn->peerAddress().toIpPort() << " -> "
                << conn->localAddress().toIpPort() <<  " is " 
                << (conn->connected() ? "UP" : "DOWN");
    // 检查连接是否处于已连接状态
    if (conn->connected()) {
        // 创建一个新的 RpcChannel 对象，并传入当前连接
        RpcChannelPtr channel(new RpcChannel(conn));
        // 将服务列表的指针设置到 RpcChannel 中，以便 RpcChannel 可以查找和调用相应的服务
        channel->setServices(&services_);
        // 为连接设置消息回调函数，当有消息到达时，会调用 RpcChannel 的 onMessage 函数进行处理
        conn->setMessageCallback(
            std::bind(&RpcChannel::onMessage, get_pointer(channel), _1, _2));
        // 将 RpcChannel 对象存储到连接的上下文中，方便后续使用
        conn->setContext(channel);
    } else {
        // 如果连接断开，将连接的上下文设置为空
        conn->setContext(RpcChannelPtr());
    }
}


