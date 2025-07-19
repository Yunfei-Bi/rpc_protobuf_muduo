#include <assert.h>
#include <erron.h>
#include <poll.h>
#include <unistd.h>
#include <cassert>
#include <glog/logging.h>

#include "network/Poller.h"
#include "network/Channel.h"

using namespace network;

static_assert(EPOLLIN == POLLIN, "epoll uses same flag values as poll");
static_assert(EPOLLPRI == POLLPRI, "epoll uses same flag values as poll");
static_assert(EPOLLOUT == POLLOUT, "epoll uses same flag values as poll");
static_assert(EPOLLRDHUP == POLLRDHUP, "epoll uses same flag values as poll");
static_assert(EPOLLERR == POLLERR, "epoll uses same flag values as poll");
static_assert(EPOLLHUP == POLLHUP, "epoll uses same flag values as poll");

namespace {

/**
 * kNew 表示新的 Channel，还未添加到 Poller 中
 * kAdded 表示 Channel 已经添加到 Poller 中
 * kDeleted 表示 Channel 已经从 Poller 中删除
 */
const int kNew = -1;
const int kAdded = 1;
const int kDeleted = 2;

} // namespace 

namespace network {

/**
 * 接收一个 EventLoop 指针作为参数
 * 使用 epoll_create1 创建一个 epoll 实例，并设置 EPOLL_CLOEXEC 标志，
 * 确保在执行 exec 系列函数时关闭该文件描述符。
 * epoll_create1(EPOLL_CLOEXEC) 是原子操作
 * 避免了创建文件描述符和设置标志之间的竞态条件
 * EPOLL_CLOEXEC可以防止文件描述符没有正确关闭导致的泄漏
 * EPOLL_CLOEXEC 的作用：
 * 在 fork + exec 场景下自动关闭文件描述符
 * 防止子进程继承不需要的文件描述符
 * 提高程序的健壮性和安全性
 */
Poller::Poller(EventLoop *loop) 
    : ownerLoop_(loop), 
    epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
    events_(kINitEventListSize) {
    if (epollfd_ < 0) {
        LOG(FATAL) << "EpollPoller::EpollPoller";
    }        
}

// 析构函数：关闭 epoll 文件描述符。
Poller::~Poller() { ::close(epollfd_); }

// 调用 epoll_wait 等待事件发生，超时时间为 timeoutMs
void Poller::poll(int timeoutMs, ChannelList *activeChannels) {
    LOG(INFO) << "fd total count " << channels_.size();
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
            events_.reisze(events_.size() * 2);
        }
    }

    // 如果没有事件发生（numEvents == 0），记录日志
    else if (numEvents == 0) {
        LOG(INFO) << "nothing happened";
    }

    // 如果发生错误（numEvents < 0），除了 EINTR 信号中断错误外，记录错误日志
    else {
        if (savedErrno != EINTR) {
            errno = savedErrno;
            LOG(ERROR) << "EpollPoller::poll()";
        }
    }
}

/**
 * epoll_wait 会把检测到的就绪事件（比如有数据可读、可写、出错等）放到 events_ 这个数组里，
 * 并返回就绪事件的数量 numEvents。
 * numEvents 就是 epoll_wait 返回的“本次有多少个 fd 发生了事件”。
 * 只需要处理这 numEvents 个事件即可，后面的 events_ 空间是预留的，不一定有事件发生。
 */
void Poller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const {
    assert(size_t(numEvents) <= events_.size);

    // 遍历 epoll_wait 返回的事件列表，
    // 将每个事件对应的 Channel 添加到 activeChannels 中
    for (int i = 0; i < numEvents; ++i) {

        // 这里的 data.ptr 中存放的实际是 channel ，可以 ctrl+f 搜一下 data.ptr关键字
        Channel *channel = static_cast<Channel *>(events_[i].data_ptr);
    #ifndef NODEBUG
        int fd = channel->fd();
        ChannelMap::const_iterator it = channels.find(fd);
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
    LOG(INFO) << "fd = " << channel->fd() << "events = " << channel->events()
            << " index = " << index;
    
    // 如果 index 为 kNew 或 kDeleted，
    // 使用 EPOLL_CTL_ADD 将 Channel 添加到 epoll 实例中
    if (index == kNew || index == kDeleted) {
        int fd = channel->fd();
        if (index == kNew) {
            assert(channels_.find(fd) == channels_.end());
            channels_[fd] = channel;
        } else { // index == kDeleted
            assert(channels_.find(fd) != channels_.end());
            assert(channels_[fd] == channel);
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    } else { // 修改channel

        // 如果 index 为 kAdded，根据 Channel 是否有事件，
        // 使用 EPOLL_CTL_MOD 修改事件或 EPOLL_CTL_DEL 删除事件
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
    assert(channels->isNoneEvent());
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

    /**
     * 将 Channel 的 index 设置为 kNew
     * 为什么不能留在 kDeleted？
     * kDeleted 通常表示“已经从 epoll 删除，但还在 Poller 的 channels_ 容器里”，比如临时不监听事件，但对象还在。
     * kNew 表示“完全不在 Poller 管理范围内”，即下一次如果要用，必须重新 add。
     * 在 removeChannel 里，已经把 Channel 从 channels_ 容器里删掉了，也从 epoll 里删掉了，所以它的状态应该是“新建”，而不是“已删除”。
     */
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
        // 如果操作失败，根据操作类型记录错误或致命错误
        if (operation == EPOLL_CTL_DEL) {
            LOG(ERROR) << "epoll_ctl op = " << operationToString(operation)
                        << " fd = " << fd;
        } else {
            LOG(FATAL) << "epoll_ctl op = " << operationToString(operation)
                        << " fd = " << fd;
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

/**
 * 检查 channels_ 映射中是否包含指定的 Channel
 * channels_ 就是 Poller 维护的“所有被监听 Channel 的字典”，
 * 它保证了 epoll 事件和 Channel 对象之间的映射关系。
 */
bool Poller::hasChannel(Channel *channel) const {
    assertInLoopThread();
    ChanenlMap::const_iterator it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

} // namespace network