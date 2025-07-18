#include "network/TcpServer.h"

#include <stdio.h>  // snprintf

#include <cassert>

#include <glog/logging.h>

#include "network/Acceptor.h"
#include "network/EventLoop.h"
#include "network/EventLoopThreadPool.h"
#include "network/SocketsOps.h"

using namespace network;

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr,
                     const std::string &nameArg, Option option)
    : loop_(CHECK_NOTNULL(loop)), // 服务器使用的事件循环
      ipPort_(listenAddr.toIpPort()), // 监听地址的 IP 和端口信息
      name_(nameArg), // 服务器的名称
      // acceptor_：用于接受新连接的 Acceptor 对象
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      // threadPool_：事件循环线程池，用于处理连接的 I/O 操作
      threadPool_(new EventLoopThreadPool(loop, name_)),
      // connectionCallback_ 和 messageCallback_：默认的连接和消息回调函数
      connectionCallback_(defaultConnectionCallback),
      messageCallback_(defaultMessageCallback),
      // nextConnId_：下一个连接的 ID
      nextConnId_(1) {
  // 设置 Acceptor 的新连接回调函数为 TcpServer::newConnection
  acceptor_->setNewConnectionCallback(
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer() {
  loop_->assertInLoopThread();
  LOG(INFO) << "TcpServer::~TcpServer [" << name_ << "] destructing";
  started_ = false; // 标记服务器停止
  // 遍历所有连接，调用 TcpConnection::connectDestroyed 函数销毁连接
  for (auto &item : connections_) {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

// 检查线程数是否合法
void TcpServer::setThreadNum(int numThreads) {
  assert(0 <= numThreads);
  // 设置事件循环线程池的线程数量
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start() {
  // if (started_) {
  //   LOG(INFO) << "server is running";
  //   abort();
  // }
  // started_ = true;
  // 启动事件循环线程池
  threadPool_->start(threadInitCallback_);

  // 确保 Acceptor 没有在监听
  assert(!acceptor_->listening());

  // 在事件循环中调用 Acceptor::listen 开始监听连接
  loop_->runInLoop(std::bind(&Acceptor::listen, get_pointer(acceptor_)));
}

void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr) {
  loop_->assertInLoopThread(); // 处理新的连接请求
  // 从线程池中获取下一个事件循环
  EventLoop *ioLoop = threadPool_->getNextLoop();
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  // 生成连接名称
  std::string connName = name_ + buf;

  LOG(INFO) << "TcpServer::newConnection [" << name_ << "] - new connection ["
            << connName << "] from " << peerAddr.toIpPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  // 创建 TcpConnection 对象
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  // 将连接添加到连接映射中
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  // 设置连接的回调函数
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1));  // FIXME: unsafe
      // 在事件循环中调用 TcpConnection::connectEstablished 建立连接
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

// 在事件循环中调用 removeConnectionInLoop 方法移除连接
void TcpServer::removeConnection(const TcpConnectionPtr &conn) {
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  // 确保在事件循环线程中移除连接
  loop_->assertInLoopThread();
  // 记录移除连接的日志
  LOG(INFO) << "TcpServer::removeConnectionInLoop [" << name_
            << "] - connection " << conn->name();
  // 从连接映射中移除连接
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  // 在连接所在的事件循环中调用 TcpConnection::connectDestroyed 销毁连接
  EventLoop *ioLoop = conn->getLoop();
  ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}
