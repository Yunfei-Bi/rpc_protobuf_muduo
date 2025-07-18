#include "network/EventLoopThread.h"

#include "network/EventLoop.h"

namespace network {
EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : loop_(NULL) // 指向 EventLoop 实例的指针，用于在单独线程中运行事件循环
    , exiting_(false) // 布尔标志，用于指示线程是否正在退出
    , mutex_() // 互斥锁，用于保护共享数据的线程安全
    , callback_(cb) {} // 线程初始化回调函数，用于在事件循环启动前进行自定义初始化

EventLoopThread::~EventLoopThread() {
  exiting_ = true; // 设置退出标志 exiting_ 为 true
  // 如果事件循环指针 loop_ 不为空，调用 loop_->quit() 停止事件循环
  if (loop_ !=
      NULL)  // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just
    // now. but when EventLoopThread destructs, usually programming is exiting
    // anyway.
    loop_->quit();
    thread_->join(); // 等待线程结束（thread_->join()），确保资源正确释放
  }
}

EventLoop *EventLoopThread::startLoop() {
  // assert(!thread_.started());
  // thread_.start();
  // 创建新线程并绑定 threadFunc 作为线程执行函数
  thread_ = std::make_unique<std::thread>(
      std::bind(&EventLoopThread::threadFunc, this));
  EventLoop *loop = NULL;
  {
    // 使用互斥锁和条件变量实现线程同步
    std::unique_lock<std::mutex> lock(mutex_);
    // 等待直到 loop_ 被初始化（由 threadFunc 函数设置）
    while (loop_ == NULL) {
      cv_.wait(lock); // 当 loop_ 被设置后，条件变量通知等待线程
    }
    loop = loop_; // 返回初始化后的 EventLoop 指针
  }

  return loop;
}

void EventLoopThread::threadFunc() {
  EventLoop loop; // 在当前线程中创建 EventLoop 实例 loop

  // 如果存在线程初始化回调函数 callback_，
  // 调用该回调函数并传入 EventLoop 实例指针
  if (callback_) {
    callback_(&loop);
  }

  {
    // 使用互斥锁保护共享数据
    std::unique_lock<std::mutex> lock(mutex_);
    // 将 loop_ 指向新创建的 EventLoop 实例
    loop_ = &loop;
    cv_.notify_all(); // 通过条件变量 cv_ 通知等待线程（startLoop 方法中的等待）
  }

  // 启动事件循环 loop.loop()，开始处理事件
  loop.loop();
  // assert(exiting_);
  // 事件循环结束后（loop.loop() 返回），将 loop_ 置为 NULL
  std::unique_lock<std::mutex> lock(mutex_);
  loop_ = NULL;
}
}  // namespace network