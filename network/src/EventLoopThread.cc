#include "network/EventLoopThread.h"
#include "network/EventLoop.h"

namespace network {

EventLoopThread::EventLoopThread(const THreadInitCallback &cb, const std::string &name)
    : loop_(NULL),
    exiting_(false),
    mutex_(),
    callback_(cb) {}

EventLoopThread::~EventLoopThread() {
    exiting_ = true;
    if (loop_ != NULL) {
        loop_->quit();
        thread_->join();
    }
}

EventLoop *EventLoopThread::startLoop() {
    thread_ = std::make_unique<std::thread>(
        std::bind(&EventLoopThread::threadFunc, this));
    EventLoop *loop = NULL;
    {
        // 使用互斥锁和条件变量实现线程同步
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == NULL) {
            cv_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}

void EventLoopThread::threadFunc() {
    EventLoop loop;
    if (callback_) {
        callback_(&loop);
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cv_.notify_all();
    }
    loop.loop();
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = NULL;
}

} // namespace network