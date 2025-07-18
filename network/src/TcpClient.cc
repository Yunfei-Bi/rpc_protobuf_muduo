#include "network/TcpClient.h"

#include <stdio.h>  // snprintf

#include <cassert>

#include <glog/logging.h>

#include "network/Connector.h"
#include "network/EventLoop.h"
#include "network/SocketsOps.h"

using namespace network;

// TcpClient::TcpClient(EventLoop* loop)
//   : loop_(loop)
// {
// }

// TcpClient::TcpClient(EventLoop* loop, const std::string& host, uint16_t port)
//   : loop_(CHECK_NOTNULL(loop)),
//     serverAddr_(host, port)
// {
// }

namespace network {
namespace detail {

// removeConnection 函数：将 TcpConnection 的 connectDestroyed 方法封装成一个任务，
// 通过 EventLoop 的 queueInLoop 方法将其放入事件循环中执行，用于销毁连接
void removeConnection(EventLoop *loop, const TcpConnectionPtr &conn) {
  loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}

void removeConnector(const ConnectorPtr &connector) {
  // connector->
}

}  // namespace detail

}  // namespace network

TcpClient::TcpClient(EventLoop *loop, const InetAddress &serverAddr,
                     const std::string &nameArg)
    // 初始化 TcpClient 对象，接收一个 EventLoop 指针、服务器地址 InetAddress 和客户端名称
    // 创建一个 Connector 对象，用于建立与服务器的连接
    : loop_(CHECK_NOTNULL(loop)),
      connector_(new Connector(loop, serverAddr)),
      name_(nameArg),
      connectionCallback_(defaultConnectionCallback),
      messageCallback_(defaultMessageCallback),
      retry_(false),
      connect_(true),
      nextConnId_(1) {
  // 设置连接器的新连接回调函数为 TcpClient 的 newConnection 方法
  connector_->setNewConnectionCallback(
      std::bind(&TcpClient::newConnection, this, _1));
  // FIXME setConnectFailedCallback
  LOG(INFO) << "TcpClient::TcpClient[" << name_ << "] - connector "
            << get_pointer(connector_);
}

TcpClient::~TcpClient() {
  LOG(INFO) << "TcpClient::~TcpClient[" << name_ << "] - connector "
            << get_pointer(connector_);
  TcpConnectionPtr conn;
  bool unique = false;
  {
    // 使用互斥锁保护 connection_ 成员，判断连接是否唯一
    std::unique_lock<std::mutex> lock(mutex_);
    unique = connection_.unique();
    conn = connection_;
  }
  // 如果存在连接，设置连接的关闭回调函数为 removeConnection，
  // 并根据连接是否唯一决定是否强制关闭连接
  if (conn) {
    assert(loop_ == conn->getLoop());
    // FIXME: not 100% safe, if we are in different thread
    CloseCallback cb = std::bind(&detail::removeConnection, loop_, _1);
    loop_->runInLoop(std::bind(&TcpConnection::setCloseCallback, conn, cb));
    if (unique) {
      conn->forceClose();
    }
  } 
  // 如果不存在连接，停止连接器
  else 
  {
    connector_->stop();
    // FIXME: HACK
    // loop_->runAfter(1, std::bind(&detail::removeConnector, connector_));
  }
}

// connect 方法：记录连接日志，设置连接标志为 true，并启动连接器开始连接
void TcpClient::connect() {
  // FIXME: check state
  LOG(INFO) << "TcpClient::connect[" << name_ << "] - connecting to "
            << connector_->serverAddress().toIpPort();
  connect_ = true;
  connector_->start();
}

// disconnect 方法：设置连接标志为 false，使用互斥锁保护 connection_ 成员，
// 如果存在连接则调用 shutdown 方法关闭连接
void TcpClient::disconnect() {
  connect_ = false;

  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (connection_) {
      connection_->shutdown();
    }
  }
}

// stop 方法：设置连接标志为 false，停止连接器
void TcpClient::stop() {
  connect_ = false;
  connector_->stop();
}

// 新连接处理方法
void TcpClient::newConnection(int sockfd) {
  loop_->assertInLoopThread(); // 确保在事件循环线程中执行
  // 获取对端地址和本地地址，生成连接名称
  InetAddress peerAddr(sockets::getPeerAddr(sockfd));
  char buf[32];
  snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf;
  // 获取对端地址和本地地址，生成连接名称
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  // 创建一个 TcpConnection 对象，用于管理新建立的连接
  TcpConnectionPtr conn(
      new TcpConnection(loop_, connName, sockfd, localAddr, peerAddr));

  // 设置连接的各种回调函数，包括连接回调、消息回调、写完成回调和关闭回调
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpClient::removeConnection, this, _1));  // FIXME: unsafe
  {
    // 使用互斥锁保护 connection_ 成员，将新连接赋值给 connection_
    std::unique_lock<std::mutex> lock(mutex_);
    connection_ = conn;
  }
  // 使用互斥锁保护 connection_ 成员，将新连接赋值给 connection_
  conn->connectEstablished();
}

// 移除连接处理方法
void TcpClient::removeConnection(const TcpConnectionPtr &conn) {
  // 确保在事件循环线程中执行，检查连接的事件循环是否一致
  loop_->assertInLoopThread();
  assert(loop_ == conn->getLoop());

  {
    // 使用互斥锁保护 connection_ 成员，重置 connection_
    std::unique_lock<std::mutex> lock(mutex_);
    assert(connection_ == conn);
    connection_.reset();
  }

  // 将 TcpConnection 的 connectDestroyed 方法封装成一个任务，
  // 通过 EventLoop 的 queueInLoop 方法将其放入事件循环中执行，用于销毁连接
  loop_->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
  // 如果 retry_ 标志为 true 且 connect_ 标志为 true，
  // 则重新启动连接器进行重连
  if (retry_ && connect_) {
    LOG(INFO) << "TcpClient::connect[" << name_ << "] - Reconnecting to "
              << connector_->serverAddress().toIpPort();
    connector_->restart();
  }
}
