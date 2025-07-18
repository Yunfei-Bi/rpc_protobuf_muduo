#include "network/EventLoopThreadPool.h"

#include <stdio.h>

#include <glog/logging.h>

#include "network/EventLoop.h"
#include "network/EventLoopThread.h"

namespace network {
// 构造函数：初始化成员变量，接收主线程的 EventLoop 指针和线程池名称
EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop,
                                         const std::string &nameArg)
    : baseLoop_(baseLoop), // 指向主线程的 EventLoop 实例，通常用于处理新连接
      name_(nameArg), // 线程池的名称，用于日志和调试
      started_(false), // 布尔标志，指示线程池是否已启动
      numThreads_(0), // 线程池中线程的数量
      next_(0) {} // 轮询索引，用于 getNextLoop 方法的轮询策略

// 不需要显式释放 EventLoop 实例，因为它们是栈上变量，会被自动销毁
EventLoopThreadPool::~EventLoopThreadPool() {
  // Don't delete loop, it's stack variable
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb) {
  assert(!started_); // 断言确保线程池未启动且在正确线程中调用
  baseLoop_->assertInLoopThread();

  // 设置启动标志 started_ 为 true
  started_ = true;

  // 循环创建 numThreads_ 个 EventLoopThread 实例
  for (int i = 0; i < numThreads_; ++i) {
    char buf[name_.size() + 32];
    // 为每个线程生成唯一名称（当前代码中未正确设置名称，buf 未被初始化）
    LOG(INFO) << "name11111: " << name_.c_str();
    EventLoopThread *t = new EventLoopThread(cb, buf);
    // 将线程对象加入 threads_ 容器
    threads_.push_back(std::unique_ptr<EventLoopThread>(t));
    // 调用 startLoop() 方法获取对应的 EventLoop 实例并加入 loops_ 容器
    loops_.push_back(t->startLoop());
  }
  if (numThreads_ == 0 && cb) {
    cb(baseLoop_);
  }
}

// getNextLoop 方法：轮询获取 EventLoop
EventLoop *EventLoopThreadPool::getNextLoop() {
  // 确保在正确线程中调用且线程池已启动
  baseLoop_->assertInLoopThread();
  assert(started_);
  EventLoop *loop = baseLoop_; // 默认返回主线程的 EventLoop

  // 如果存在子线程的 EventLoop，使用轮询策略获取
  if (!loops_.empty()) {
    // round-robin
    loop = loops_[next_]; // 从 loops_ 中按 next_ 索引获取
    ++next_; // 更新 next_ 索引，实现循环轮询
    if (size_t(next_) >= loops_.size()) {
      next_ = 0;
    }
  }
  return loop;
}

// getLoopForHash 方法：按哈希值分配 EventLoop
EventLoop *EventLoopThreadPool::getLoopForHash(size_t hashCode) {
  baseLoop_->assertInLoopThread(); // 确保在正确线程中调用
  EventLoop *loop = baseLoop_; // 默认返回主线程的 EventLoop

  // 如果存在子线程的 EventLoop，使用哈希值取模策略获取
  // 确保相同哈希值的请求分配到同一个 EventLoop
  // 实现请求的一致性哈希分配
  if (!loops_.empty()) {
    loop = loops_[hashCode % loops_.size()];
  }
  return loop;
}

// 该方法返回所有 EventLoop 实例
std::vector<EventLoop *> EventLoopThreadPool::getAllLoops() {
  // 确保在正确线程中调用且线程池已启动
  baseLoop_->assertInLoopThread();
  assert(started_);
  // 如果没有子线程的 EventLoop，返回只包含主线程 EventLoop 的向量
  // 否则返回包含所有 EventLoop 实例的向量
  if (loops_.empty()) {
    return std::vector<EventLoop *>(1, baseLoop_);
  } else {
    return loops_;
  }
}
}  // namespace network