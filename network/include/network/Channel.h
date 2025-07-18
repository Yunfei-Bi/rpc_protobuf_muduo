#pragma once 

#include <sys/epoll.h>
#include <atomic>
#include <functional>
#include <memory>

namespace network {

class EventLoop;

class Channel {
public:
    enum EventType {
        NoneEvent = 0,
        ReadEvent = EPOLLIN, 
        WriteEvent = EPOLLOUT,
    };

    typedef std::function<void()> EventCallback;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    void handleEvent();
    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }
    bool isNoneEvent() const { return events_ == EventType::NoneEvent; }

    void enableReading() {
        events_ |= EventType::ReadEvent;
        update();
    }
      void disableReading() {
        events_ &= ~EventType::ReadEvent;
        update();
    }
    void enableWriting() {
        events_ |= EventType::WriteEvent;
        update();
    }
    void disableWriting() {
        events_ &= ~EventType::WriteEvent;
        update();
    }
    void disableAll() {
        events_ = EventType::NoneEvent;
        update();
    }

    bool isWriting() const { return events_ & EventType::WriteEvent; }
    bool isReading() const { return events_ & EventType::ReadEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    EventLoop *ownerLoop() { return loop_; }
    void remove();

private:
    void update();

    EventLoop *loop_;
    const int fd_;
    int events_;
    int revents_;
    int index_;

    std::weak_ptr<void> tie_;
    std::atomic<bool> event_handing_;
    std::atomic<bool> addedToLoop_;
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

} // namespace network