#include <stdio.h>
#include <glog/logging.h>

#include "network/EventLoop.h"
#include "network/EventLoopThread.h"
#include "network/EventLoopThreadPool.h"

namespace network {

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg) 
    : baseLoop_(baseLoop),
    name_(nameArg),
    started_(false),
    numThreads_(0),
    next_(0) {}

EventLoopThreadPool::~EventLoopThreadPool() {

}

/**
 * 这个函数就是用来启动 EventLoop 线程池的，每个线程都拥有自己的 EventLoop，并且都能被统一管理。
 * 如果没有额外线程，则直接在主线程的 EventLoop 上执行初始化操作
 */
void EventLoopThreadPoll::start(const ThreadInitCallback &cb) {
    assert(!started_);
    baseLoop_->assertInLoopThread();

    started_ = true;

    for (int i = 0; i < numThreads_; ++i) {
        char buf[name_.size() + 32]; // 为每个线程生成唯一名称
        LOG(INFO) << " name11111: " << name_.c_str(); // 
        EventLoopThread *t = new EventLoopThread(cb, buf); //  new 一个线程
        threads_.push_back(std::unique_ptr<EventLoopThread>(t)); // 将线程对象加入 threads_ 容器
        loops_.push_back(t->startLoop()); // 调用 startLoop() 方法获取对应的 EventLoop 实例并加入 loops_ 容器
    }
    if (numThreads_ == 0 && cb) {
        cb(baseLoop_);
    }
}

EventLoop *EventLoopThreadPool::getNextLoop() {

    // 确保在正确线程中调用且线程池已启动
    baseLoop_->assertInLoopThread();
    assert(started_);
    EventLoop *loop = baseLoop_;

    // 如果存在子线程的 EventLoop，使用轮询策略获取
    if (!loops_.empty()) {
        loop = loops_[next_];
        ++next; // 更新 next_ 索引，实现循环轮询
        if (size_t(next_) >= loops_.size()) {
            next_ = 0;
        }
    }
}

/**
 * getLoopForHash 方法：按哈希值分配 EventLoop
 */
EventLoop *EventLoopThreadPool::getLoopForHash(size_t hashCode) {
    baseLoop_->assertInLoopThread();
    EventLoop *loop = baseLoop_;
    if (!loops_.empty()) {
        loop = loops_[hashCode % loops_.size()];
    }
    return loop;
}

/**
 * 该方法返回所有 EventLoop 实例
 */
std::vector<EventLoop *> EventLoopThreadPool::getAllLoops() {
    baseLoop_->assertInLoopThread();
    assert(started_);
    if (loops_.empty()) {
        return std::vector<EventLoop *> (1, baseLoop_);
    } else {
        return loops_;
    }
}

} // namespace network
