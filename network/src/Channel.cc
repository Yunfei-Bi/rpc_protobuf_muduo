#include <poll.h>
#include <cassert>
#include <sstream>

#include "network/Channel.h"
#include "network/EventLoop.h"

namespace network {

// 初始化 Channel 对象，接受一个 EventLoop 指针和一个文件描述符作为参数
Channel::Channel(EventLoop *loop, int fd__)
    : loop_(loop),
    fd_(fd__),
    events_(0),
    revents_(0),
    index_(-1),
    event_handing_(false),
    addedToLoop_(false) {}

Channel::~Channel() {
    assert(!event_handing_); // 确保当前没有正在处理事件
    assert(!addedToLoop_); // 确保已从 EventLoop 中移除
    if (loop_->isInLoopThread()) {
        assert(!loop_->hasChannel(this))
    }
}

/**
 * 将 addedToLoop_ 标记为 true，表示该 Channel 已添加到 EventLoop 中
 * 调用 EventLoop 的 updateChannel 方法，
 * 通知 EventLoop 更新该 Channel 的事件关注状态
 */
void Channel::update() {
    addedToLoop_ = true;
    loop_->updateChannel(this);
}

/**
 * 调用 isNoneEvent 方法进行断言检查，确保当前 Channel 没有关注任何事件
 * 将 addedToLoop_ 标记为 false，表示该 Channel 已从 EventLoop 中移除
 * 调用 EventLoop 的 removeChannel 方法，通知 EventLoop 移除该 Channel
 */
void Channel::remove() {
    assert(isNoneEvent());
    addedToLoop_ = false;
    loop_->removeChannel(this);
}

/**
 * 处理文件描述符上发生的事件，将 event_handling_ 标记为 true，表示正在处理事件
 */
void Channel::handleEvent() {
    event_handling_ = true;

    // 这句的意思是：如果发生了挂起事件，但没有可读事件，说明对端关闭了连接。
    if ((revents_ & POLLHUP) && !(revents_ & POLLIN)) {
        if (closeCallback_) closeCallback_();
    }

    // POLLOUT 表示可以写数据（写缓冲区有空间）。
    if (revents_ & POLLOUT) {
        if (writeCallback_) writeCallback_();
    }

    // 如果发生了错误或 fd 非法，并且设置了 errorCallback_，就调用它，处理错误事件。
    if (revents_ & (POLLERR | POLLNVAL)) {
        if (errorCallback_) errorCallback_();
    }
    
    // 事件处理结束，标记为未处理状态。
    event_handling_ = false;
}

} // namespace network