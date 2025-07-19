#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <glog/logging.h>

#include "network/EventLoop.h"
#include "network/Channel.h"
#include "network/Poller.h"
#include "network/SocketsOps.h"

using namespace network;

namespace network {

/**
 * t_loopInThisThread 是一个线程局部变量，用于存储当前线程的 EventLoop 指针。
 * 每个线程只能有一个 EventLoop 实例
 * kPollTimeMs 是事件轮询的超时时间，单位为毫秒
 */
static thread_local EventLoop *t_loopInThisThread = nullptr;
const int kPollTimeMs = 10000;

/**
 * createEventfd 函数用于创建一个事件文件描述符（eventfd），
 * 并设置为非阻塞和在执行 exec 时关闭
 */
int createEventfd() {
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0) {
        LOG(ERROR) << " Failed in eventfd ";
        abort();
    }
    return evtfd;
}

} // namespace network

/**
 * getEventLoopOfCurrentThread 是一个静态成员函数，
 * 用于获取当前线程的 EventLoop 指针
 */
EventLoop *EventLoop::getEventLoopOfCurrentThread() {
    return t_loopInThisThread;
}

EventLoop::EventLoop()
    : looping_(false),
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    poller_(new Poller(this)),
    wakeupFd_(createEventfd()), 
    wakeupChannel_(new Channel(this, wakeupFd_)),
    currentActiveChannel_(NULL) {
    
    /**
     * 检查当前线程是否已经有 EventLoop 实例，
     * 如果有则记录致命错误并终止程序；否则将当前实例赋值给 t_loopInThisThread
     */
    LOG(INFO) << "EventLoop created " << this << " in thread " << threadId_;
    if (t_loopInThisThread) {
        LOG(FATAL) << " Another EventLoop " << t_loopInThisThread
                    << " exists in this thread " << threadId_;
    } else {
        t_loopInThisThread = this;
    }

    /**
     * 为 wakeupChannel_ 设置读回调函数 handleRead, 
     * 并启用读事件
     * 记录当前线程的 ID
     */
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
    threadId_ = getThreadId();
}

EventLoop::~EventLoop() {
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = NULL;
}

void EventLoop::loop() {

    // 确保当前 EventLoop 没有在循环中，并且在当前线程中调用
    assert(!looping_);
    assertInLoopThread();

    // 设置 looping_ 为 true，quit_ 为 false
    looping_ = true;
    quit_ = false;
    LOG(INFO) << " EventLoop " << this << " start looping ";

    // 进入循环，直到 quit_ 为 true
    while (!quit_) {

        // 清空 activeChannels_ 列表
        activeChannels_.clear();

        // 调用 Poller 的 poll 函数进行事件轮询，
        // 将活跃的通道存储在 activeChannels_ 中
        poller_->poll(kPollTimeMs, &activeChannels_);
        ++iteration_;

        eventHandling_ = true;

        // 遍历 activeChannels_ 列表，调用每个通道的 handleEvent 函数处理事件
        for (Channel *channel : activeChannels_) {
            currentActiveChannel_ = channel;
            currentActiveChannel_->handleEvent();
        }
        currentActiveChannel_ = NULL;
        eventHandlin_ = false;

        // 调用 doPendingFunctors 函数处理待执行的回调函数
        doPendingFunctors();
    }
    LOG(INFO) << " EventLoop " << this << " stop looping ";

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

/**
 * 如果当前在事件循环所在的线程中，直接执行回调函数 cb
 * 否则，将回调函数加入待执行队列
 */
void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

/**
 * 使用互斥锁保护 pendingFunctors_ 队列，将回调函数加入队列
 * 如果当前不在事件循环所在的线程中，
 * 或者正在处理待执行的回调函数，调用 wakeup 函数唤醒事件循环
 * wakeup() 通过向 wakeupFd_ 写入数据，让 wakeupFd_ 变为可读。
 * EventLoop 的 poller 监听了 wakeupFd_，一旦可读就会立刻返回，达到“唤醒”效果。
 * 这样可以让 EventLoop 线程及时响应新任务或跨线程事件。
 */
void EventLoop::queueInLoop(Functor cb) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }

    if (!isInLoopThread() || callingPendingFunctors_) {
        wakeup();
    }
}

ssize_t EventLoop::queueSize() const {
    // 使用互斥锁保护 pendingFunctors_ 队列，返回队列的大小
    std::unique_lock<std::mutex> lock(mutex_);
    return pendingFunctors_.size();
}

/**
 * updateChannel 函数用于更新通道的事件监听状态，
 * 调用 Poller 的 updateChannel 函数
 */
void EventLoop::updateChannel(Channel *channel) {
    assert(channel->ownerLoop() = =this);
    assertInLoopThread();
    poller_->updateChannel(channel);
}

/**
 * removeChannel 函数用于从 Poller 中移除通道，
 * 调用 Poller 的 removeChannel 函数
 */
void EventLoop::removeChannel(Channel *channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    if (eventHandling_) {
        // 断言：要么当前正在处理的通道就是要被移除的通道，要么这个通道根本不在活跃通道列表里。
        assert(currentActiveChannel_ == channel || 
                std::find(activeChannels_.begin(), activeChannels_,end(), channel) == 
                activeChannels_.end()) ;
    }
    poller_->removeChannel(channel);
}

/**
 * 检查通道是否存在于 Poller 中，调用 Poller 的 hasChannel 函数
 */
bool EventLoop::hasChannel(Channel *channel) {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread() {

}

/**
 * wakeup 函数用于向事件文件描述符 wakeupFd_ 写入一个字节，唤醒事件循环
 * wakeup() 通过向 wakeupFd_ 写入数据，让 wakeupFd_ 变为可读。
 * EventLoop 的 poller 监听了 wakeupFd_，一旦可读就会立刻返回，达到“唤醒”效果。
 * 这样可以让 EventLoop 线程及时响应新任务或跨线程事件。
 */
void EventLoop::wakeup() {
    uint64_t one = 1;
    sszie_t n = sockets::write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one) {
        LOG(ERROR) << " EventLoop::wakeup() writes " << n << "bytes instead of 8 ";
    }
}

/**
 * 创建一个临时的 functors 向量
 * 将 pendingFunctors_ 中的回调函数交换到 functors 中
 * 用 functors.swap(pendingFunctors_); 把所有待执行的回调函数转移到本地变量 functors，
 * 这样锁可以尽快释放，减少锁的持有时间，提高效率。
 */
void EventLoop::doPendingFunctors() {
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

/**
 * 该函数用于打印活跃通道的信息，目前代码被注释掉
 */
void EventLoop::printActiveChannels() const {

}