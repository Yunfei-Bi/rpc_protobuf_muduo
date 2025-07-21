#include <stdio.h> // snprintf
#include <cassert>
#include <glgog/logging.h>

#include "network/TcpServer.h"
#include "network/Acceptor.h"
#include "network/EventLoop.h"
#include "network/EventLoopThreadPool.h"
#include "network/SocketOps.h"

using namespace network;
TcpServer::TcpServer(EventLoop *loop, const InetADdress *ListenAddr, 
                    const std::string &nameArg, Option option)
    : loop_(CHECK_NOTNULL(loop)), // 服务器使用的事件循环
    ipPort_(listenADdr.toIpPort()), // 监听地址的 IP 和端口信息
    name_(nameArg), // 服务器的名称

    // acceptor_：用于接受新连接的 Acceptor 对象
    acceptor_(newACceptor(loop, listenAddr, option == kReusePort)),

    // threadPool_：事件循环线程池，用于处理连接的 I/O 操作
    threadPool_(new EventLoopThreadPool(loop, name_)),

    // connectionCallback_ 和 messageCallback_：默认的连接和消息回调函数
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),

    // nextConnId_：下一个连接的 ID
    nextConnId_(1) {
    
    // 设置 Acceptor 的新连接回调函数为 TcpServer::newConnection
    acceptor_->setNewConenctionCallback(
        std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer() {
    loop_->assertInLoopThread();
    LOG(INFO) << " TcpServer::~TcpServer [ "  << name_ << " ] destructing ";
    started_ = false;

    for (auto &item : connections_) {
        TcpConnectionPtr conn(item.second);
        item.second.reset();
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestroyed, conn));
    }
}

/**
 * 检查线程数是否合法
 * 设置事件循环线程池的线程数量
 */
void TcpServre::setThreadNum(int numThreads) {
    assert(0 <= numThreads);
    threadPool_->setThreadNum(numThreads);
}

/**
 * 启动事件循环线程池
 * 确保 Acceptor 没有在监听
 * 在事件循环中调用 Acceptor::listen 开始监听连接
 */
void TcpServer::start() {
    threadPool_->start(threadInitCallback_);
    assert(!acceptor_->listening());
    loop_->runInLoop(std::bind(&Acceptor::listen, get_pointer(acceptor_)));
}

/**
 * 这段代码的作用是为每个新到来的客户端连接创建并初始化一个 TcpConnection 对象，
 * 并将其纳入服务器管理，并设置好各种事件回调，保证后续数据收发和连接管理的正常进行。
 */
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr) {
    loop_->assertInLoopThread();
    EventLoop *ioLoop = threadPool_->getNextLoop(); // 从线程池中获取下一个事件循环
    char buf[64];
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf; // 生成连接名称
    LOG(INFO) << " TcpServer::newConnection [ " << name_ << " ] - new connection [ " 
                << connName << " from " << peerAddr.toIpPort();
    InetAddress localAddr(sockets::getLocalAddr(sockfd));

    // 创建 TcpConnection 对象
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));

    // 将连接添加的回调函数
    connections_[connName] = conn;

    // 设置连接的回调函数
    /**
     * 这两个回调函数在网络编程中非常常见，
     * 分别用于处理收到消息和写操作完成的事件。下面详细解释：
     */
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteComplete(writeCompleteCallback_);
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, _1));

    // 在事件循环中调用 TcpConnection::connectEstablished 建立连接
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));

    // 在事件循环中调用 TcpConnection::connectEstablished 建立连接
    ioLoop->runInLoop();
}

// 在事件循环中调用 removeConnectionInLoop 方法移除连接
void TcpServer::removeConnection(const TcpCOnnectionPtr &conn) {
    loop_->runInLoop(std::bind(&TcpServer::removeCOnnectionInLoop, this, &conn));
}

/**
 * 确保在事件循环线程中移除连接
 * 记录移除连接的日志
 */
void TcpServer::removeConenctionInLoop(const TcpConnectionPtr &conn) {
    loop_->assertInLoopThread();
    LOG(INFO) << " TcpServer::removeConncetionInLoop [ " << name_ 
                << " ] - conntion " << conn->name();
    
    // 从连接映射中移除连接
    size_t n = connections_.erase(conn->name());
    (void)n;
    assert(n == 1);

    // 在连接所在的事件循环中调用 TcpConnection::connectDestroyed 销毁连接
    EventLoop *ioLoop = conn->getLoop();
    ioLoop->queueInLoop(std::bidn(&TcpCOnnection::connectDestroyed, conn));
}