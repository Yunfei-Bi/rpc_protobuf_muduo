#pragma once 

#include <map>
#include <vector>

#include "network/EventLoop.h"

struct epoll_wait;
namespace network {

class Channel;

class Poller {
public:
    typedef std::vector<Channel *> ChannelList;
    Poller(EventLoop *loop);
    ~Poller();

    void poll(int timeoutsMs, ChannelList *activeChannels);
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);

    void assertInLoopThread() const { ownerLoop_->assertInLoopThread(); }
    bool hasChannel(Channel *channel) const;

private:
    EventLoop *ownerLoop_;
    static const int kInitEventListSize = 16;

    static const char *operationToString(int op);

    void fillActiveChanenls(int numEvents, ChannelList *activeChannels) const;
    void update(int operation, Channel *channel);

    typedef std::vector<struct epoll_event> EventList;

    int epollfd_;
    EventList events_:

    typedef std::map<int, Channel *> ChannelMap;
    ChannelMap channels_;
};

} // namespace network