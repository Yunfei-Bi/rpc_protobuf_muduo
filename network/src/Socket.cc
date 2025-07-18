#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <cassert>
#include <glog/logging.h>

#include "network/Socket.h"
#include "network/InetAddress.h"
#include "network/SocketsOps.h"

using namespace network;
namespace network{

// 当 Socket 对象销毁时，调用 sockets::close 函数关闭套接字文件描述符，
// 确保资源的正确释放
Socket::~Socket() { sockets::close(sockfd_); }

// getTcpInfo 函数：使用 getsockopt 系统调用获取 TCP 连接的详细信息，
// 存储在 struct tcp_info 结构体中。如果调用成功，返回 true，否则返回 false
bool Socket::getTcpInfo(struct tcp_info *tcpi) const {
    socklen_t len = sizeof(*tcpi);
    memset(tcpi, 0, len);
    return ::getsockopt(sockfd_, SOL_TCP, TCP_INFO, tcpi, &len) == 0;
}

// getTcpInfoString 函数：调用 getTcpInfo 获取 TCP 信息，
// 若成功则将信息格式化为字符串存储在 buf 中，并返回 true；否则返回 false。
bool Socket::getTcpInfoString(char *buf, int len) const{
    struct tcp_info tcpi;
    bool ok = getTcpInfo(&tcpi);
    if (ok) {
        snprintf(
            buf, len,
            "unrecovered=%u "
            "rto=%u ato=%u snd_mss=%u rcv_mss=%u "
            "lost=%u retrans=%u rtt=%u rttvar=%u "
            "sshthresh=%u cwnd=%u total_retrans=%u",
            tcpi.tcpi_retransmits,  // Number of unrecovered [RTO] timeouts
            tcpi.tcpi_rto,          // Retransmit timeout in usec
            tcpi.tcpi_ato,          // Predicted tick of soft clock in usec
            tcpi.tcpi_snd_mss, tcpi.tcpi_rcv_mss,
            tcpi.tcpi_lost,     // Lost packets
            tcpi.tcpi_retrans,  // Retransmitted packets out
            tcpi.tcpi_rtt,      // Smoothed round trip time in usec
            tcpi.tcpi_rttvar,   // Medium deviation
            tcpi.tcpi_snd_ssthresh, tcpi.tcpi_snd_cwnd,
            tcpi.tcpi_total_retrans);  // Total retransmits for entire connection
    }
    return ok;
}

// 绑定地址和监听连接
// bindAddress 函数：使用 bind 系统调用将套接字绑定到指定的地址。
// 如果绑定失败，使用 glog 记录致命错误并终止程序
void Socket::bindAddress(const InetAddress &addr) {
    int ret = ::bind(sockfd_, addr.getSockAddr(), 
                        static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
    if (ret < 0) {
        LOG(FATAL) << "sockets::bindOrDie";
    }
}

// listen 函数：使用 listen 系统调用将套接字设置为监听状态，允许客户端连接。
// 如果监听失败，使用 glog 记录致命错误并终止程序
void Socket::listen() {
    int ret = ::listen(sockfd_, SOMAXCONN);
    if (ret < 0) {
        LOG(FATAL) << "sockets::listenOrDie";
    }
}

// accept 函数：使用 accept 系统调用接受客户端的连接请求。
// 如果接受失败，使用 glog 记录错误信息；
// 如果成功，将客户端的地址信息存储在 peeraddr 中，
// 并返回新的客户端套接字文件描述符
int Socket::accept(InetAddress *peeraddr) {
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));

    socklen_t addrlen = static_cast<socklen_t>(sizeof(addr));
    int client_fd = 
        ::accept(sockfd_, reinterpret_cast<sockaddr *>(&addr), &addrlen);
    if (client_fd < 0) {
        LOG(ERROR) << "Socket::accept error";
    }
    if (client_fd >= 0) {
        peeraddr->setSockAddrInet6(addr);
    }
    return client_fd;
}

// shutdownWrite 函数：使用 shutdown 系统调用关闭套接字的写端，
// 禁止继续发送数据。如果关闭失败，使用 glog 记录错误信息
void Socket::shutdownWrite() {
    if (::shutdown(sockfd_, SHUT_WR) < 0) {
        LOG(ERROR) << "sockets::shutdownWrite";
    }
}

// setTcpNoDelay 函数：
// 使用 setsockopt 系统调用设置 TCP 的 TCP_NODELAY 选项，
// 禁用 Nagle 算法，减少延迟
void Socket::setTcpNoDelay(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, 
                    static_cast<socklen_t>(sizeof optval));
}

// setReuseAddr 函数：
// 使用 setsockopt 系统调用设置套接字的 SO_REUSEADDR 选项，
// 允许地址重用
void Socket::setReuseAddr(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, 
                    static_cast<socklen_t>(sizeof optval));
}

// setReusePort 函数：
// 使用 setsockopt 系统调用设置套接字的 SO_REUSEPORT 选项，
// 允许端口重用。如果系统不支持该选项，记录错误信息
void Socket::setReusePort(bool on) {
#ifdef SO_REUSEPORT
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, 
                            static_cast<socklen_t>(sizeof optval));
    if (ret < 0 && on) {
        LOG(ERROR) << "SO_REUSEPORT failed.";
    }
#else
    if (on) {
        LOG(ERROR) << "SO_REUSEPORT is not supported."
    }
#endif
}

// setKeepAlive 函数：
// 使用 setsockopt 系统调用设置套接字的 SO_KEEPALIVE 选项，
// 启用 TCP 保活机制
void Socket::setKeepAlive(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, 
                    static_cast<socklen_t>(sizeof optval));
}

} // namespace network