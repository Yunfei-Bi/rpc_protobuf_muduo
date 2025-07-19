#pragma once
#include <functional>
#include "network/Channel.h"
#include "network/Socket.h"

namespace network {

class EventLoop;
class InetAddress;

class Acceptor {
public:
    typedef std::function<void(int sockfd, const InetAddress &)> NewConnectionCallback;

    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(const NewConenctionCallback &cb) {
        newConnectionCallback_ = cb;
    }

    void listen();

    bool listening() const { return listening_; }

private:
    void handleRead();

    EventLoop *loop_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listening_;
    int idleFd_;
};

} // namespace network