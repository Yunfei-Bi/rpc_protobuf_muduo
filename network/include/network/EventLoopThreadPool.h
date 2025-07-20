#pragma once 

#include <functional>
#include <memory>
#include <vector>

namespace network {

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool {
public:
    typedef std::function<void(EventLoop *)> ThreadInitCallback;

    EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
    ~EventLoopThreadPool();
    void setThreadNum(int numThreads) { numThreads_ = numThreads; }
    void start (const ThreadInitCallback &cb = ThreadInitCallback());

    EventLoop *getLoopForHash(size_t hashCode);

    std::vector<EventLoop *> getAllLoops();

    bool started() const { return started_; }

    const std::string &name() const { return name_; }

private:
    EventLoop *baseLoop_; // 指向主线程的 EventLoop 实例，通常用于处理新连接
    std::string name_; // 线程池的名称，用于日志和调试
    bool started_ = false; // 布尔标志，指示线程池是否已启动
    int numThreads_; // 线程池中线程的数量
    int next_; // 轮询索引，用于 getNextLoop 方法的轮询策略
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop *> loops_; // 存储 EventLoop 指针的容器，管理事件循环实例
};


} // namespace network