#include "network/Poller.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <cassert>

#include <glog/logging.h>

#include "network/Channel.h"

using namespace network;

// On Linux, the constants of poll(2) and epoll(4)
// are expected to be the same.
// 这些静态断言确保 epoll 和 poll 使用相同的标志值，以保证代码的兼容性。
static_assert(EPOLLIN == POLLIN, "epoll uses same flag values as poll");
static_assert(EPOLLPRI == POLLPRI, "epoll uses same flag values as poll");
static_assert(EPOLLOUT == POLLOUT, "epoll uses same flag values as poll");
static_assert(EPOLLRDHUP == POLLRDHUP, "epoll uses same flag values as poll");
static_assert(EPOLLERR == POLLERR, "epoll uses same flag values as poll");
static_assert(EPOLLHUP == POLLHUP, "epoll uses same flag values as poll");

namespace {
const int kNew = -1; // kNew 表示新的 Channel，还未添加到 Poller 中
const int kAdded = 1; // kAdded 表示 Channel 已经添加到 Poller 中
const int kDeleted = 2; // kDeleted 表示 Channel 已经从 Poller 中删除
}  // namespace

namespace network {
Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop), // 接收一个 EventLoop 指针作为参数
      // 使用 epoll_create1 创建一个 epoll 实例，并设置 EPOLL_CLOEXEC 标志，
      // 确保在执行 exec 系列函数时关闭该文件描述符。
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)), 
      // 初始化 events_ 向量，用于存储 epoll_wait 返回的事件
      events_(kInitEventListSize) {
  if (epollfd_ < 0) { // 如果 epoll_create1 失败，使用 glog 记录致命错误
    LOG(FATAL) << "EPollPoller::EPollPoller";
  }
}

// 析构函数：关闭 epoll 文件描述符。
Poller::~Poller() { ::close(epollfd_); }

void Poller::poll(int timeoutMs, ChannelList *activeChannels) {
  LOG(INFO) << "fd total count " << channels_.size();
  // 调用 epoll_wait 等待事件发生，超时时间为 timeoutMs
  int numEvents = ::epoll_wait(epollfd_, &*events_.begin(),
                               static_cast<int>(events_.size()), timeoutMs);
  int savedErrno = errno;

  // 如果有事件发生（numEvents > 0），
  // 调用 fillActiveChannels 方法将活跃的 Channel 添加到 activeChannels 中，
  // 并根据事件数量动态调整 events_ 向量的大小
  if (numEvents > 0) {
    LOG(INFO) << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);
    if (size_t(numEvents) == events_.size()) {
      events_.resize(events_.size() * 2);
    }
  } 
  // 如果没有事件发生（numEvents == 0），记录日志
  else if (numEvents == 0) {
    LOG(INFO) << "nothing happened";
  } 
  // 如果发生错误（numEvents < 0），除了 EINTR 信号中断错误外，记录错误日志
  else {
    // error happens, log uncommon ones
    if (savedErrno != EINTR) {
      errno = savedErrno;
      LOG(ERROR) << "EPollPoller::poll()";
    }
  }
}

void Poller::fillActiveChannels(int numEvents,
                                ChannelList *activeChannels) const {
  assert(size_t(numEvents) <= events_.size());
  // 遍历 epoll_wait 返回的事件列表，
  // 将每个事件对应的 Channel 添加到 activeChannels 中
  for (int i = 0; i < numEvents; ++i) {
    Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
#ifndef NDEBUG
    int fd = channel->fd();
    ChannelMap::const_iterator it = channels_.find(fd);
    assert(it != channels_.end());
    assert(it->second == channel);
#endif
    // 设置 Channel 的 revents 为实际发生的事件
    channel->set_revents(events_[i].events);
    activeChannels->push_back(channel);
  }
}

void Poller::updateChannel(Channel *channel) {
  Poller::assertInLoopThread();
  // 根据 Channel 的 index 状态，决定是添加、修改还是删除该 Channel
  const int index = channel->index();
  LOG(INFO) << "fd = " << channel->fd() << " events = " << channel->events()
            << " index = " << index;
  // 如果 index 为 kNew 或 kDeleted，
  // 使用 EPOLL_CTL_ADD 将 Channel 添加到 epoll 实例中
  if (index == kNew || index == kDeleted) {
    // a new one, add with EPOLL_CTL_ADD
    int fd = channel->fd();
    if (index == kNew) {
      assert(channels_.find(fd) == channels_.end());
      channels_[fd] = channel;
    } else  // index == kDeleted
    {
      assert(channels_.find(fd) != channels_.end());
      assert(channels_[fd] == channel);
    }

    channel->set_index(kAdded);
    update(EPOLL_CTL_ADD, channel);
  } 
  // 如果 index 为 kAdded，根据 Channel 是否有事件，
  // 使用 EPOLL_CTL_MOD 修改事件或 EPOLL_CTL_DEL 删除事件
  else 
  {
    // update existing one with EPOLL_CTL_MOD/DEL
    int fd = channel->fd();
    (void)fd;
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(index == kAdded);
    if (channel->isNoneEvent()) {
      update(EPOLL_CTL_DEL, channel);
      channel->set_index(kDeleted);
    } else {
      update(EPOLL_CTL_MOD, channel);
    }
  }
}

void Poller::removeChannel(Channel *channel) {
  Poller::assertInLoopThread();
  int fd = channel->fd();
  LOG(INFO) << "fd = " << fd;
  // 从 channels_ 映射中删除指定的 Channel
  assert(channels_.find(fd) != channels_.end());
  assert(channels_[fd] == channel);
  assert(channel->isNoneEvent());
  int index = channel->index();
  assert(index == kAdded || index == kDeleted);
  size_t n = channels_.erase(fd);
  (void)n;
  assert(n == 1);

  // 如果 Channel 的 index 为 kAdded，
  // 使用 EPOLL_CTL_DEL 从 epoll 实例中删除该 Channel
  if (index == kAdded) {
    update(EPOLL_CTL_DEL, channel);
  }
  // 将 Channel 的 index 设置为 kNew
  channel->set_index(kNew);
}

void Poller::update(int operation, Channel *channel) {
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = channel->events();
  event.data.ptr = channel;
  int fd = channel->fd();
  // 使用 epoll_ctl 对 epoll 实例进行添加、修改或删除操作
  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) {
    if (operation == EPOLL_CTL_DEL) {
      LOG(ERROR) << "epoll_ctl op =" << operationToString(operation)
                 << " fd =" << fd;
    } 
    // 如果操作失败，根据操作类型记录错误或致命错误
    else {
      LOG(FATAL) << "epoll_ctl op =" << operationToString(operation)
                 << " fd =" << fd;
    }
  }
}

// 将 epoll_ctl 的操作类型转换为字符串，方便日志记录
const char *Poller::operationToString(int op) {
  switch (op) {
    case EPOLL_CTL_ADD:
      return "ADD";
    case EPOLL_CTL_DEL:
      return "DEL";
    case EPOLL_CTL_MOD:
      return "MOD";
    default:
      assert(false && "ERROR op");
      return "Unknown Operation";
  }
}

// 检查 channels_ 映射中是否包含指定的 Channel
bool Poller::hasChannel(Channel *channel) const {
  assertInLoopThread();
  ChannelMap::const_iterator it = channels_.find(channel->fd());
  return it != channels_.end() && it->second == channel;
}

}  // namespace network