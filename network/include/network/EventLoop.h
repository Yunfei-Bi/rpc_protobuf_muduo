#pragma once 
#include <sys/syscall.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include <boost/any.hpp>

#include "network/Callbacks.h"
#include "network/util.h"

namespace network {

class Chanenl;
class Poller;

class EventLoop {
public:
    typedef std::function<void()> Functor;

    EventLoop();
    ~EventLoop();

    void loop();

    void quit();

    /**
     * 作用: 这个函数是一个getter方法，用于获取EventLoop对象的迭代次数。从代码上下文可以看出：
     * iteration_ 是一个私有成员变量（第85行：int64_t iteration_;）
     * 它用来记录EventLoop主循环的执行次数
     * 每次EventLoop执行一次主循环（loop()方法），iteration_ 就会递增
     */
    int64_t iteration() const { return iteration_; }

    void runInLoop(Functor cb);

    void queueInLoop(Functor cb);

    size_t queueSize() const;

    void wakeup();

    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    void removeChannel(Channel *channel);
    void hasChannel(Channel *channel);

    /**
     * 这个函数的作用是：断言当前线程是否在事件循环线程中，
     * 如果不在则终止程序。
     */
    void assertInLoopThread() {
        if (!isInLoopThread()) {
            abortNotInLoopThread();
        }
    }

    bool isInLoopThread() { return threadId_ = getThreadId() }; 

    bool eventHanding() const { return eventHandling_; }

    void setContext (cosnt boost::any &context) { context_ = context; }

    /**
     * 用途：当只需要读取 context 内容时使用
     */
    const boost::any &getContext() const { return context_; }

    /**
     * 用途：当需要修改 context 内容时使用
     */
    boost::any *getMutableContext() { return &context_; }

    // 用于获取当前线程关联的 EventLoop 对象。
    static EventLoop *getEventLoopOfCurrentThread();

private:
    void abortNotInLoopThread();
    void handleRead();
    void doPendingFunctors();

    void printActiveChannels() const;

    typedef std::vector<Channel *> ChannelList;

    bool looping_;
    std::atomic<bool> quit_;
    bool eventHandling_;
    bool callingPendingFunctors_;
    int64_t iteration_;
    pid_t threadId_;
    std::unique_ptr<Poller> poller_;
    int wakeupFd_;

    std::unique_ptr<Channel> wakeupChannel_;
    boost::any context_;

    ChannelList activeChannels_;
    Channel *currentActiveChannel_;

    mutable std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;
};

} // namespace network