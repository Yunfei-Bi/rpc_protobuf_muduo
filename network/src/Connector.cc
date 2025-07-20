#include <errno.h>
#include <glog/logging.h>

#include "network/Connector.h"
#include "network/Channel.h"
#include "network/EventLoop.h"
#include "network/SocketsOps.h"

using namespace network;
namespace network {

const int Conncetor::kMaxRetryDelayMs;

Connector::Connector(EventLoop *loop, const InetADdress &serverAddr)
    : loop_(loop),
    serverAddr_(serverAddr),
    connect_(false),
    state_(kDisconnected),
    retryDelayMs_(kInitRetryDelayMs) {
    LOG(INFO) << "ctor[" << this << "]";
}

Connector::~Connector() {
    LOG(INFO) << "dtor[" << this << "]";
    assert(!channel_);
}

/**
 * 将 connect_ 标记为 true，表示开始连接
 * 调用 EventLoop 的 runInLoop 方法，将 startInLoop 方法添加到事件循环中执行
 */
void Connector::start() {
    connect_ = true;
    loop_->runInLoop(std::bind(&Connector::startInLoop, this));
}

/**
 * 确保当前线程是 EventLoop 所在的线程，并断言连接状态为 kDisconnected
 * 如果 connect_ 为 true，则调用 connect 方法开始连接；否则，记录日志表示不进行连接。
 */
void Connector::startInLoop() {
    loop_->assertInLoopThread();
    assert(state_ == kDisconnected);
    if (connect_)
    {
        connect();
    } else {
        LOG(INFO) << "do not connect";
    }
}

/**
 * 将 connect_ 标记为 false，表示停止连接
 * 调用 EventLoop 的 queueInLoop 方法，将 stopInLoop 方法添加到事件循环的队列中执行
 */
void Connector::stop() {
    connect_ = false;
    loop_->queueInLoop(std::bind(&Connector::stopInLoop, this));
}

/**
 * 确保当前线程是 EventLoop 所在的线程
 * 如果连接状态为 kConnecting，
 * 则将状态设置为 kDisconnected，移除并重置通道，然后调用 retry 方法进行重试。
 */
void Connector::stopInLoop() {
    loop_->assertInLoopThread();
    if (state_ == kConnecting) {
        setState(kDisconnected);
        int sockfd = removeAndResetChannel();
        retry(sockfd);
    }
}

/**
 * 创建一个非阻塞的套接字，并尝试连接到服务器
 * 根据连接结果的错误码，执行不同的操作
 */
void Connector::connect() {
    int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());
    int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
    int savedErrno = (ret == 0) ? 0 : errno;

    switch (savedErrno) {
        case 0:
        case EINPROGRESS:
        case EINTR:
        case EISCONN:
            connecting(sock);
            break;
        case EAGAIN:
        case EADDRINUSE:
        case EADDRNOTAVAIL:
        case ECONNREFUSED:
        case ENETUNREACH:
            retry(sockfd);
            break;
        case EACCES:
        case EPERM:
        case EAFNOSUPPORT:
        case EALREADY:
        case EBADF:
        case EFAULT:
        case ENOTSOCK:
            LOG(ERROR) << "connect error in Connector::startInLoop " << savedErrno;
            sockets::close(sockfd);
            break;
        
        default:
            LOG(ERROR) << " Unexpected error in Connector::startInLoop " << savedErrno;
            sockets::close(sockfd);
            break;
    }
}

/**
 * 确保当前线程是 EventLoop 所在的线程
 * 将连接状态设置为 kDisconnected，重置重试延迟时间，
 * 将 connect_ 标记为 true，然后调用 startInLoop 方法重新开始连接
 */
void Connector::restart() {
    loop_->assertInLoopThread();
    setState(kDisconnected);
    retryDelayMs_ = kInitRetryDelayMs;
    connect_ = true;
    startInLoop();
}

/**
 * 将连接状态设置为 kConnecting。
 * 创建一个新的 Channel 对象，并设置其写回调函数和错误回调函数
 * 启用通道的写事件监测
 */
void Connector::connecting(int sockfd) {
    setState(kConnecting);
    assert(!channel_);
    channel_.reset(new Channel(loop_, sockfd));
    channel_->setWriteCallback(
        std::bind(&Connector::handleWrite, this));
    channel_->setErrorCallback(
        std::bind(&Connector::handleError, this));
    channel_->enableWeiting();
}

/**
 * 禁用通道的所有事件监测，并从 EventLoop 中移除该通道
 * 获取通道的套接字描述符
 * 将 resetChannel 方法添加到事件循环的队列中执行，以确保在合适的时机重置通道
 */
int Connector::removeAndResetChannel() {
    channel_->disableAll();
    channel_->remove();
    int sockfd = channel_->fd();
    loop_->queueInLoop(
        std::bind(&Connector::resetChannel, this));
    return sockfd;
}

/**
 * 重置 channel_ 指针，释放其管理的 Channel 对象
 */
void Connector::resetChannel() { channel_.reset(); }

/**
 * Connector::handleWrite()
 * 在异步连接过程中，检测到套接字可写时，判断连接是否成功。
 * 如果失败就重试，如果成功就回调上层，否则关闭套接字。
 */
void Connector::handleWrite() {
    LOG(INFO) << " Connector::handleWrite " << state_;

    // 如果连接状态为 kConnecting，则移除并重置通道，检查套接字的错误码
    if (state_ == kConnecting) {
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        if (err) { // 如果有错误，调用 retry 方法进行重试
            LOG(INFO) << " Connector::handleWrite - SO_ERROR = " << err;
            retry(sockfd);
        } else if (sockets::isSelfConnect(sockfd)) { // 如果是自连接，调用 retry 方法进行重试
            LOG(INFO) << " Connector::handleWrite - Self connect ";
            retry(sockfd);
        } else { // 否则，将连接状态设置为 kConnected
            setState(kConnected);
            if (connect_) {
                newConnectionCallback_(sockfd);
            } else { // 否则，关闭套接字
                sockets::close(sockfd);
            }
        }
    } else {
        assert(state_ == kDisconnected);
    }
}

/**
 * 当异步连接过程中发生错误时，获取错误信息，打印日志，
 * 然后进行重试，保证连接的健壮性和自动恢复能力。
 * 调用 removeAndResetChannel()，移除并重置通道，获取套接字描述符。
 * 调用 sockets::getSocketError(sockfd) 获取套接字的错误码。
 * 调用 retry(sockfd)，进行重试（即重新尝试连接）。
 */
void Connector::handleError() {
    LOG(ERROR) << " Connector::handleError state = " << state_;
    if (state_ == kConnecting) {
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        LOG(INFO) << " SO_ERROR = " << err;
        retry(sockfd);
    }
}

/**
 * 当连接失败或发生错误时，关闭当前的 socket，并准备在一段时间后重新尝试连接。
 * 如果 connect_ 标志为真（说明用户还希望继续连接），就打印日志，表示将在 retryDelayMs_ 毫秒后重试连接。
 * 如果 connect_ 为假，说明用户不再需要连接，打印日志表示不再重试。
 */
void Connector::retry(int sockfd) {
    sockets::close(sockfd);
    setState(kDisconnected);
    if (connect_) {
        LOG(INFO) << " Connector::retry - Retry connecting to "
                << serverAddr_.toIpPort() << " in " << retryDelayMs_
                << " milliseconds ";
    } else {
        LOG(INFO) << " do not connect ";
    }
}

} // namespace network