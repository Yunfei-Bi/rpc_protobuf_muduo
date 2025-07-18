#pragma once

struct tcp_info;

namespace network {

class InetAddress;

class Socket {
public:
    explicit Socket(int sockfd) : sockfd_(sockfd) {}

    ~Socket();

    int fd() const {return sockfd_;}

    bool getTcpInfo(struct tcp_info *)const;

    bool getTcpInfoString(char *buf, int len) const;

    void bindAddress(const InetAddress &localaddr);

    void listen();

    int accept(InetAddress *peeraddr);

    void shutdownWrite();

    void setTcpNoDelay(bool on);

    void setReuseAddr(bool on);

    void setReusePort(bool on);

    /**
     * http的keep-alive， 是由应用层（用户态）实现的，称为http长连接
     * tcp的keepalive，是由tcp层（内核态实现的），称为tcp保活机制
     */
    void setKeepAlive(bool on);

private:
    const int sockfd_;

};

} // namespace network