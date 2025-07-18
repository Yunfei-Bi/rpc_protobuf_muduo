#include "network/EventLoop.h"

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>

#include <glog/logging.h>

#include "network/Channel.h"
#include "network/Poller.h"
#include "network/SocketsOps.h"

using namespace network;

namespace network {
// t_loopInThisThread 是一个线程局部变量，用于存储当前线程的 EventLoop 指针。
// 每个线程只能有一个 EventLoop 实例
static thread_local EventLoop *t_loopInThisThread = nullptr;

const int kPollTimeMs = 10000; // kPollTimeMs 是事件轮询的超时时间，单位为毫秒

// createEventfd 函数用于创建一个事件文件描述符（eventfd），
// 并设置为非阻塞和在执行 exec 时关闭
int createEventfd() {
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    LOG(ERROR) << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

}  // namespace network

// getEventLoopOfCurrentThread 是一个静态成员函数，
// 用于获取当前线程的 EventLoop 指针
EventLoop *EventLoop::getEventLoopOfCurrentThread() {
  return t_loopInThisThread;
}

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      eventHandling_(false),
      callingPendingFunctors_(false),
      iteration_(0),
      poller_(new Poller(this)), // 创建一个 Poller 实例，用于事件轮询
      wakeupFd_(createEventfd()), // 创建一个事件文件描述符 wakeupFd_
      // 并创建一个对应的 Channel 实例 wakeupChannel_
      wakeupChannel_(new Channel(this, wakeupFd_)),
      currentActiveChannel_(NULL) {
  LOG(INFO) << "EventLoop created " << this << " in thread " << threadId_;
  // 检查当前线程是否已经有 EventLoop 实例，
  // 如果有则记录致命错误并终止程序；否则将当前实例赋值给 t_loopInThisThread
  if (t_loopInThisThread) {
    LOG(FATAL) << "Another EventLoop " << t_loopInThisThread
               << " exists in this thread " << threadId_;
  } else {
    t_loopInThisThread = this;
  }
  // 为 wakeupChannel_ 设置读回调函数 handleRead
  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  // ，并启用读事件
  wakeupChannel_->enableReading();
  // 记录当前线程的 ID
  threadId_ = getThreadId();
}

EventLoop::~EventLoop() {
  // LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
  //           << " destructs in thread " << CurrentThread::tid();
  // 禁用 wakeupChannel_ 的所有事件，并从 Poller 中移除
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_); // 关闭事件文件描述符 wakeupFd_
  t_loopInThisThread = NULL; // 将 t_loopInThisThread 置为 NULL
}

void EventLoop::loop() {
  // 确保当前 EventLoop 没有在循环中，并且在当前线程中调用
  assert(!looping_);
  assertInLoopThread();
  // 设置 looping_ 为 true，quit_ 为 false
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG(INFO) << "EventLoop " << this << " start looping";

  // 进入循环，直到 quit_ 为 true
  while (!quit_) {
    // 清空 activeChannels_ 列表
    activeChannels_.clear();
    // 调用 Poller 的 poll 函数进行事件轮询，
    // 将活跃的通道存储在 activeChannels_ 中
    poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_;

    // TODO sort channel by priority
    eventHandling_ = true;
    // 遍历 activeChannels_ 列表，调用每个通道的 handleEvent 函数处理事件
    for (Channel *channel : activeChannels_) {
      currentActiveChannel_ = channel;
      currentActiveChannel_->handleEvent();
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    // 调用 doPendingFunctors 函数处理待执行的回调函数
    doPendingFunctors();
  }

  LOG(INFO) << "EventLoop " << this << " stop looping";
  // 循环结束后，记录日志并设置 looping_ 为 false
  looping_ = false;
}

void EventLoop::quit() {
  // 设置 quit_ 为 true，表示要退出事件循环
  quit_ = true;
  // 如果当前不在事件循环所在的线程中，调用 wakeup 函数唤醒事件循环
  if (!isInLoopThread()) {
    wakeup();
  }
}

void EventLoop::runInLoop(Functor cb) {
  // 如果当前在事件循环所在的线程中，直接执行回调函数 cb
  if (isInLoopThread()) {
    cb();
  } else {
    // 否则，将回调函数加入待执行队列
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb) {
  {
    // 使用互斥锁保护 pendingFunctors_ 队列，将回调函数加入队列
    std::unique_lock<std::mutex> lock(mutex_);
    pendingFunctors_.push_back(std::move(cb));
  }

  // 如果当前不在事件循环所在的线程中，
  // 或者正在处理待执行的回调函数，调用 wakeup 函数唤醒事件循环
  if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup();
  }
}

size_t EventLoop::queueSize() const {
  // 使用互斥锁保护 pendingFunctors_ 队列，返回队列的大小
  std::unique_lock<std::mutex> lock(mutex_);
  return pendingFunctors_.size();
}

void EventLoop::updateChannel(Channel *channel) {
  // updateChannel 函数用于更新通道的事件监听状态，
  // 调用 Poller 的 updateChannel 函数
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel) {
  // removeChannel 函数用于从 Poller 中移除通道，
  // 调用 Poller 的 removeChannel 函数
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_) {
    assert(currentActiveChannel_ == channel ||
           std::find(activeChannels_.begin(), activeChannels_.end(), channel) ==
               activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

// 检查通道是否存在于 Poller 中，调用 Poller 的 hasChannel 函数
bool EventLoop::hasChannel(Channel *channel) {
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread() {
  // LOG(FATAL) << "Event loop is not in the current thread, threadID: "
  //            << threadId_ << ", current threadID = " << getThreadId();
}

// wakeup 函数用于向事件文件描述符 wakeupFd_ 写入一个字节，唤醒事件循环
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    LOG(ERROR) << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

// handleRead 函数用于从事件文件描述符 wakeupFd_ 读取一个字节，处理唤醒事件
void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    LOG(ERROR) << "EventLoop::handleRead() reads " << n
               << " bytes instead of 8";
  }
}

void EventLoop::doPendingFunctors() {
  // 创建一个临时的 functors 向量，
  // 将 pendingFunctors_ 中的回调函数交换到 functors 中
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
    std::unique_lock<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  // 遍历 functors 向量，执行每个回调函数
  for (const Functor &functor : functors) {
    functor();
  }
  // 设置 callingPendingFunctors_ 为 false，表示处理完毕
  callingPendingFunctors_ = false;
}

// 该函数用于打印活跃通道的信息，目前代码被注释掉
void EventLoop::printActiveChannels() const {
  // for (const Channel* channel : activeChannels_)
  // {
  //   LOG_TRACE << "{" << channel->reventsToString() << "} ";
  // }
}
