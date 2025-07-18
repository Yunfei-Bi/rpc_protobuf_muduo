#include "network/Channel.h"

#include <poll.h>

#include <cassert>
#include <sstream>

#include "network/EventLoop.h"

namespace network {

// 初始化 Channel 对象，接受一个 EventLoop 指针和一个文件描述符作为参数
Channel::Channel(EventLoop *loop, int fd__)
    : loop_(loop), // loop_ 指向所属的 EventLoop 对象
      fd_(fd__), // fd_ 存储关联的文件描述符。
      events_(0), // events_ 表示关注的事件，初始化为 0
      revents_(0), // 表示实际发生的事件，初始化为 0
      index_(-1), // 用于 Poller 管理，初始化为 -1
      event_handling_(false), // event_handling_ 表示是否正在处理事件，初始化为 false
      addedToLoop_(false) {} // addedToLoop_ 表示是否已添加到 EventLoop 中，初始化为 false

Channel::~Channel() {
  assert(!event_handling_); // 确保当前没有正在处理事件
  assert(!addedToLoop_); // 确保已从 EventLoop 中移除
  // 如果当前线程是 EventLoop 所在的线程，还会检查 EventLoop 中是否不再包含该 Channel 对象
  if (loop_->isInLoopThread()) {
    assert(!loop_->hasChannel(this));
  }
}

void Channel::update() {
  // 将 addedToLoop_ 标记为 true，表示该 Channel 已添加到 EventLoop 中
  addedToLoop_ = true; 
  // 调用 EventLoop 的 updateChannel 方法，
  // 通知 EventLoop 更新该 Channel 的事件关注状态
  loop_->updateChannel(this);
}

void Channel::remove() {
  // 调用 isNoneEvent 方法进行断言检查，确保当前 Channel 没有关注任何事件
  assert(isNoneEvent());
  // 将 addedToLoop_ 标记为 false，表示该 Channel 已从 EventLoop 中移除
  addedToLoop_ = false;
  // 调用 EventLoop 的 removeChannel 方法，通知 EventLoop 移除该 Channel
  loop_->removeChannel(this);
}

void Channel::handleEvent() {
  // 处理文件描述符上发生的事件，将 event_handling_ 标记为 true，表示正在处理事件
  event_handling_ = true;
  if ((revents_ & POLLHUP) && !(revents_ & POLLIN)) {
    if (closeCallback_) closeCallback_();
  }

  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
    if (readCallback_) readCallback_();
  }
  if (revents_ & POLLOUT) {
    if (writeCallback_) writeCallback_();
  }

  if (revents_ & (POLLERR | POLLNVAL)) {
    if (errorCallback_) errorCallback_();
  }
  event_handling_ = false;
}
}  // namespace network