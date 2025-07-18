#include "RpcServer.h"

#include <cassert>

#include <glog/logging.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>

#include "RpcChannel.h"

using namespace network;

RpcServer::RpcServer(EventLoop *loop, const InetAddress &listenAddr)
    : server_(loop, listenAddr, "RpcServer") {
  server_.setConnectionCallback(std::bind(&RpcServer::onConnection, this, _1));
}

void RpcServer::registerService(google::protobuf::Service *service) {
  const google::protobuf::ServiceDescriptor *desc = service->GetDescriptor();
  services_[desc->full_name()] = service;
}

void RpcServer::start() { server_.start(); }

void RpcServer::onConnection(const TcpConnectionPtr &conn) {
  // 记录连接信息，包括客户端地址、服务器地址以及连接状态
  LOG(INFO) << "RpcServer - " << conn->peerAddress().toIpPort() << " -> "
            << conn->localAddress().toIpPort() << " is "
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
    // FIXME:
  }
}
