#include <errno.h>
#include <cassert>
#include <functional>
#include <string>
#include <glog/logging.h>

#include "network/TcpCOnnection.h"
#include "network/Channel.h"
#include "netwrok/EventLoop.h"
#include "network/Socket.h"
#include "network/SocketOps.h"

using namespace network;
namespace network {

/**
 * defaultConnectionCallback：默认的连接回调函数，
 * 用于记录连接的状态（UP 或 DOWN）
 */
void defaultConnectionCallback(const TcpConnectionPtr &conn) {
    LOG(INFO) << conn->localAddress().toIpPort() << " -> "
            << conn->localAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
}

void defaultMessageCallback(const TcpConnectionPtr &, Buffer *buf) {
    buf->retrieveAll();
}

/**
 * 构造函数：初始化 TcpConnection 对象，设置连接状态为 kConnecting，
 * 创建 Socket 和 Channel 对象，并为 Channel 设置读写、关闭和错误回调函数
 */
TcpConnection::TcpConnection(EventLoop *loop, const std::string &nameArg, 
                            int sockfd, const InetAddress &localAddr, 
                            const InetAddress &peerAddr)
    : loop_(CHECK_NOTNULL(loop)),
    name_(nameArg),
    state_(kConnecting),
    reading_(true),
    socket_(new Socket(sockfd)),
    channel_(new Channel(loop, sockfd)),
    localAddr_(localAddr),
    peerAddr_(peerAddr) {
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));
    LOG(INFO) << " TcpConnection::ctor[ " << name << "] at " << this
                << " fd = " << sockfd;
    socket_->setKeepAlive(true);
}

/**
 * 析构函数：记录析构信息，并断言连接状态为 kDisconnected
 */
TcpConnection::~TcpConnection() {
    LOG(INFO) << " TcpConnection::dtor[ " << name_ << "] at " << this
                << " fd = " << channel_->fd() << " state = " << stateToString();
    assert(state_ == kDisconnected);
}

/**
 * getTcpInfoString：调用 Socket 对象的 getTcpInfoString 方法，
 * 将 TCP 连接的详细信息格式化为字符串
 */
std::string TcpConnection::getTcpInfoString() const {
    char buf[1024];
    buf[0] = '\0';
    socket_->getTcpInfoString(buf, sizeof buf);
    return buf;
}

/**
 * 
 */
void TcpConnection::send(Buffer *buf) {
    if (state_ == kConnected) {
        if (loop_->isInLoopThread()) {
            sendInLoop(buf->peek(), buf->readableBytes());
            buf->retrieveAll();
        } else {
            std::function<void(const std::string &)> fp = 
                [this](const std::string &str) { this->sendInLoop(str); };
            loop_->runInLoop(std::bind(fp, buf->retrieveAllAsString()))
        }
    }
}

/**
 * 
 */
void TcpConnection::sendInLoop(const std::string &message) {
    sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void *data, size_t len) {
    loop_->assertInLoopThread();
    sszie_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    if (state_ == kDisconnected) {
        LOG(INFO) << "disconnected, give up writing";
        return ;
    }

    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        nwrote = sockets::write(cahnnel_->fd(), data, len);
        if (nwrote >= 0) {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_) {
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this()));
            }
        } else { // nwrote < 0
            nwrote = 0;
            if (errno != EWOULDBLOCK) {
                LOG(ERROR) << " TcpConnection::sendInLoop";
                if (errno == EPIPE || errno == ECONNRESET) {
                    faultError = true;
                }
            }
        }
    }

    if (!faultError && remaining > 0) {
        size_t oldLen = outputBuffer_.readableBytes();
        outputBuffer_.append(static_cast<cosnt char *>(data) + nwrote, remaining);
        if (!channel_->isWriting()) {
            channel_->enableWriting();
        }
    }
}

/**
 * shutdown：如果连接状态为 kConnected，
 * 将状态设置为 kDisconnecting，并在事件循环线程中调用 shutdownInLoop
 */
void TcpConnection::shutdown() {
    if (state_ == kConnected) {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

/**
 * shutdownInLoop：在事件循环线程中关闭写端
 */
void TcpConnection::shutdownInLoop() {
    loop_->assertInLoopThread();
    if(!channel_->isWriting()) {
        socket_->shutdownWrite();
    }
}

/**
 * 这个函数的作用是：强制关闭连接，但实际的关闭操作会被安全地安排到事件循环线程中去执行，防止多线程问题。
 * 只有在连接还没断开时才会执行。
 * 用 shared_from_this() 保证对象在异步关闭期间不会被提前析构。
 */
void TcpConnection::forceClose() {
    if (state_ == kConnected | state_ == kDisconnecting) {
        setState(kDisconnecting);
        loop_->queueInLoop(
            std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}

/**
 * 如果连接状态为 kConnected 或 kDisconnecting
 * 将状态设置为 kDisconnecting，并在指定时间后调用 forceClose
 */
void TcpConnection::forceCloseWithDelay(double seconds) {
    if (state_ == kConnected || state_ == kDisconnecting) {
        setState(kDisconnecting);
    }
}

/**
 * forceCloseInLoop：在事件循环线程中调用 handleClose 关闭连接
 */
void TcpConnection::forceCloseInLoop() {
    loop_->assertInLoopThread();
    if (state_ == kConnected || state_ == kDisconnecting) {
        handleClose();
    }
}

/**
 * stateToString：将连接状态转换为字符串
 */
const char *TcpCOnnection::stateToString() const {
    switch(state_) {
        case kDisconnected:
            return "kDisconnected";
        case kConnecting:
            return "kConnecting";
        case kConnected:
            return "kConnected";
        case kDisconnecting:
            return "kDisconnecting";
        default:
            return "unknown state";
    }
}

/**
 * setTcpNoDelay：设置 TCP 连接的 TCP_NODELAY 选项
 */
void TcpConnection::setTcpNoDelay(bool on) { socket_->setTcpNoDelay(on); }

/**
 * startRead：在事件循环线程中调用 startReadInLoop 启用读事件
 */
void TcpConnection::startRead() {
    loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

/**
 * startReadInLoop：在事件循环线程中启用读事件
 */
void TcpConneciton::startReadInLoop() {
    loop_->assertInLoopThread();
    if (!reading_ || !channel_->isReading()) {
        channel_->enableReading();
        reading_ = true;
    }
}

/**
 * stopRead：在事件循环线程中调用 stopReadInLoop 禁用读事件
 */
void TcpConnection::stopRead() {
    loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

/**
 * stopReadInLoop：在事件循环线程中禁用读事件
 */
void TcpConnection::stopReadInLoop() {
    loop_->assertInLoopThread();
    if (reading_ || channel_->isReading()) {
        channel_->disableReading();
        reading_ = false;
    }
}





} // namespace network
