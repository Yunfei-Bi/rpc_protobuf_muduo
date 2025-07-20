#include <stdio.h>
#include <cassert>
#include <glog/logging.h>

#include "network/TcpClient.h"
#include "network/Connector.h"
#include "network/EventLoop.h"
#include "network/SocketsOps.h"

using namespace network;

namespace network {

namespace detail {

/**
 * removeConnection 函数：将 TcpConnection 的 connectDestroyed 方法封装成一个任务，
 * 通过 EventLoop 的 queueInLoop 方法将其放入事件循环中执行，用于销毁连接
 */
void removeConnection(EventLoop *loop, const TcpConnectionPtr &conn) {
    loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}

void removeConnector(const ConnectorPtr &connector) {
    
}

} // namespace detail

} // namespace network

TcpClient::TcpClient(EventLoop *loop, const InetAddress &serverAddr, const std::string &nameArg)
    : loop_(CHECK_NOTNULL(loop)),
    connector_(new Connector(loop, serverAddr)),
    name_(nameArg),
    connectionCallback_(defaultConenctionCallback),
    messageCallback_(defaultMessageCallback),
    retry_(false),
    connect_(true),
    nextConnId_(1) {
    connector_->setNewConnectionCallback(
        std::bind(&TcpClient::newConnection, this, _1));
    LOG(INFO) << " TcpClient::TcpClient[" << name_ << "] - connector "
                << get_pointer(connector_);
}

/**
 * 这段代码的目的是安全地销毁 TcpClient 对象时，正确关闭 TCP 连接或停止连接器，防止资源泄漏或多线程问题
 * 通过互斥锁和智能指针的引用计数，确保连接的生命周期和线程安全。
 * 只有当 TcpClient 是连接的唯一持有者时，才会强制关闭连接，否则只设置关闭回调。
 */
TcpClient::~TcpClient() {
    LOG(INFO) << " TcpClient::~TcpClient[" << name_ << "] - connector "
                << get_pointer(connector_);
    TcpConnectionPtr conn;
    bool unique = false;
    {

        // 使用互斥锁保护 connection_ 成员，判断连接是否唯一
        std::unique_lock<std::mutex> lock(mutex_);
        unique = connection_.unique();
        conn = connection_;
    }

    /**
     * 如果 conn 存在（即连接还在）：
     * 断言当前事件循环和连接的事件循环一致。
     * 创建一个关闭回调 cb，用于在连接关闭时移除连接。
     * 通过事件循环 loop_，异步设置连接的关闭回调。
     * 如果连接是唯一的（没有其他地方持有），则强制关闭连接（conn->forceClose()）。
     * 如果 conn 不存在（即还没建立连接），则停止连接器（connector_->stop()），不再尝试连接。
     */
    if (conn) {
        assert(loop_ == conn->getLoop());
        CloseCallback cb = std::bind(&detail::removeConnection, loop_, _1);
        loop_->runInLoop(std::bind(&TcpConnection::setCloseCallback, conn, cb));
        if (unique) {
            conn->forceClose();
        }
    } else {
        connector_->stop();
    }
}

/**
 * connect 方法：记录连接日志，设置连接标志为 true，并启动连接器开始连接
 */
void TcpClient::connect() {
    LOG(INFO) << "TcpClient::connect[ " << name_ << "] - connecting to "
                << connector_->start();
    connect_ = true;
    connector_->start();
}

void TcpClient::disconnect() {
    connect_ = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (connection_) {
            connection_->shutdown();
        }
    }
}

/**
 * stop 方法：设置连接标志为 false，停止连接器
 */
void TcpClient::stop() {
    connect_ = false;
    connector_->stop();
}

void TcpClient::newConnection(int sockfd) {
    loop_->assertInLoopThread(); // 确保在事件循环线程中执行
    InetAddress peerAddr(sockets::getPeerAddr(sockfd)); // 获取对端地址
    char buf[32];
    snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;
    InetAddress localAddr(sockets::getLocalAddr(sockfd)); // 获取本地地址

    // 创建一个 TcpConnection 对象，用于管理新建立的连接
    TcpConnectionPtr conn(
        new TcpConnection(loop_, connName, sockfd, localAddr, peerAddr));

    // 设置连接的各种回调函数，包括连接回调、消息回调、写完成回调和关闭回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(std::bind(&TcpClient::removeConnection, this, _1));

    // 使用互斥锁保护 connection_ 成员，将新连接赋值给 connection_
    {
        std::unique_lock<std::mutex> lock(mutex_);
        connection_ = conn;
    }
    conn_.connectEstablished();
}

void TcpClient::removeConnection(const TcpConnectionPtr &conn) {
    loop_->assertInLoopThread();
    assert(loop_ == conn->getLoop());

    // 使用互斥锁保护 connection_ 成员，重置 connection_
    {
        std::unique_lock<std::mutex> lock(mutex_);
        assert(connection_ == conn);
        connection_.reset();
    }

    loop_->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn))
    if (retry_ && connect_) {
        LOG(INFO) << " TcpClient::connect[ " << name_ << " ] - Reconnectiong to "
                    << connector_->serverAddress(),toIpPort();
        connector_->restart();
    }
}