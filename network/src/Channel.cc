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
} // namespace network