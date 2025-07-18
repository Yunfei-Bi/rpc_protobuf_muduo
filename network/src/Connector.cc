#include "network/Connector.h"

#include <errno.h>

#include <glog/logging.h>

#include "network/Channel.h"
#include "network/EventLoop.h"
#include "network/SocketsOps.h"


// 这段代码实现了 network 命名空间下的 Connector 类，
// 该类主要用于管理 TCP 连接的建立过程，包括连接尝试、重试机制以及错误处理等功能。

using namespace network;
namespace network {
// 定义了 Connector 类的静态常量 kMaxRetryDelayMs，该常量表示最大重试延迟时间。
const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop *loop, const InetAddress &serverAddr)
    : loop_(loop), // 指向所属的 EventLoop 对象
      serverAddr_(serverAddr), // 存储服务器的地址信息
      connect_(false), // 表示是否开始连接，初始化为 false
      state_(kDisconnected), // 表示连接状态，初始化为 kDisconnected
      retryDelayMs_(kInitRetryDelayMs)  // 表示重试延迟时间，初始化为 kInitRetryDelayMs
{
  LOG(INFO) << "ctor[" << this << "]";
}

Connector::~Connector() {
  LOG(INFO) << "dtor[" << this << "]";
  assert(!channel_);
}

void Connector::start() {
  connect_ = true; // 将 connect_ 标记为 true，表示开始连接
  // 调用 EventLoop 的 runInLoop 方法，将 startInLoop 方法添加到事件循环中执行
  loop_->runInLoop(std::bind(&Connector::startInLoop, this));  // FIXME: unsafe
}

void Connector::startInLoop() {
  // 确保当前线程是 EventLoop 所在的线程，并断言连接状态为 kDisconnected
  loop_->assertInLoopThread();
  assert(state_ == kDisconnected);
  // 如果 connect_ 为 true，则调用 connect 方法开始连接；否则，记录日志表示不进行连接。
  if (connect_) {
    connect();
  } else {
    LOG(INFO) << "do not connect";
  }
}

void Connector::stop() {
  connect_ = false; // 将 connect_ 标记为 false，表示停止连接
  // 调用 EventLoop 的 queueInLoop 方法，将 stopInLoop 方法添加到事件循环的队列中执行
  loop_->queueInLoop(std::bind(&Connector::stopInLoop, this));  // FIXME: unsafe
  // FIXME: cancel timer
}

void Connector::stopInLoop() {
  loop_->assertInLoopThread(); // 确保当前线程是 EventLoop 所在的线程
  // 如果连接状态为 kConnecting，
  // 则将状态设置为 kDisconnected，移除并重置通道，然后调用 retry 方法进行重试。
  if (state_ == kConnecting) {
    setState(kDisconnected);
    int sockfd = removeAndResetChannel();
    retry(sockfd);
  }
}

void Connector::connect() {
  // 创建一个非阻塞的套接字，并尝试连接到服务器
  int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());
  int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
  int savedErrno = (ret == 0) ? 0 : errno;
  // 根据连接结果的错误码，执行不同的操作
  switch (savedErrno) {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
      connecting(sockfd);
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
      LOG(ERROR) << "Unexpected error in Connector::startInLoop " << savedErrno;
      sockets::close(sockfd);
      // connectErrorCallback_();
      break;
  }
}

void Connector::restart() {
  loop_->assertInLoopThread(); // 确保当前线程是 EventLoop 所在的线程
  // 将连接状态设置为 kDisconnected，重置重试延迟时间，
  // 将 connect_ 标记为 true，然后调用 startInLoop 方法重新开始连接
  setState(kDisconnected);
  retryDelayMs_ = kInitRetryDelayMs;
  connect_ = true;
  startInLoop();
}

void Connector::connecting(int sockfd) {
  setState(kConnecting); // 将连接状态设置为 kConnecting。
  assert(!channel_);
  // 创建一个新的 Channel 对象，并设置其写回调函数和错误回调函数
  channel_.reset(new Channel(loop_, sockfd));
  // 启用通道的写事件监测
  channel_->setWriteCallback(
      std::bind(&Connector::handleWrite, this));  // FIXME: unsafe
  channel_->setErrorCallback(
      std::bind(&Connector::handleError, this));  // FIXME: unsafe

  // channel_->tie(shared_from_this()); is not working,
  // as channel_ is not managed by shared_ptr
  channel_->enableWriting();
}

int Connector::removeAndResetChannel() {
  // 禁用通道的所有事件监测，并从 EventLoop 中移除该通道
  channel_->disableAll();
  channel_->remove();
  // 获取通道的套接字描述符
  int sockfd = channel_->fd();
  // Can't reset channel_ here, because we are inside Channel::handleEvent
  // 将 resetChannel 方法添加到事件循环的队列中执行，以确保在合适的时机重置通道
  loop_->queueInLoop(
      std::bind(&Connector::resetChannel, this));  // FIXME: unsafe
  return sockfd;
}

// 重置 channel_ 指针，释放其管理的 Channel 对象
void Connector::resetChannel() { channel_.reset(); }

void Connector::handleWrite() {
  LOG(INFO) << "Connector::handleWrite " << state_;

  // 如果连接状态为 kConnecting，则移除并重置通道，检查套接字的错误码
  if (state_ == kConnecting) {
    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    if (err) {
      // 如果有错误，调用 retry 方法进行重试
      LOG(INFO) << "Connector::handleWrite - SO_ERROR = " << err;
      retry(sockfd);
    } 
    else if (sockets::isSelfConnect(sockfd)) 
    {
      // 如果是自连接，调用 retry 方法进行重试
      LOG(INFO) << "Connector::handleWrite - Self connect";
      retry(sockfd);
    } 
    else 
    {
      // 否则，将连接状态设置为 kConnected
      setState(kConnected);
      // 如果 connect_ 为 true，则调用 newConnectionCallback_ 回调函数
      if (connect_) 
      {
        newConnectionCallback_(sockfd);
      } 
      else // 否则，关闭套接字
      {
        sockets::close(sockfd);
      }
    }
  } else {
    // what happened?
    assert(state_ == kDisconnected);
  }
}

void Connector::handleError() {
  // 处理通道的错误事件
  LOG(ERROR) << "Connector::handleError state=" << state_;
  // 如果连接状态为 kConnecting，则移除并重置通道，获取套接字的错误码，
  // 记录日志并调用 retry 方法进行重试
  if (state_ == kConnecting) {
    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    LOG(INFO) << "SO_ERROR = " << err;
    retry(sockfd);
  }
}

void Connector::retry(int sockfd) {
  // 关闭套接字，将连接状态设置为 kDisconnected
  sockets::close(sockfd);
  setState(kDisconnected);
  // 如果 connect_ 为 true，
  // 则记录重试信息，但重试逻辑被注释掉；否则，记录日志表示不进行连接
  if (connect_) {
    LOG(INFO) << "Connector::retry - Retry connecting to "
              << serverAddr_.toIpPort() << " in " << retryDelayMs_
              << " milliseconds. ";
    // loop_->runAfter(retryDelayMs_ / 1000.0,
    //                 std::bind(&Connector::startInLoop, shared_from_this()));
    // retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
  } else {
    LOG(INFO) << "do not connect";
  }
}
}  // namespace network