#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <glog/logging.h>

#include <network/SocketsOps.h>
#include <network/Endian.h>

using namespace network;

// 这些函数用于在不同的套接字地址结构体之间进行类型转换
const struct sockaddr *sockets::sockaddr_cast(const struct sockaddr_in *addr) {
    return static_cast<const struct sockaddr *>(static_cast<const void *>(addr));
}

const struct sockaddr *sockets::sockaddr_cast(const struct sockaddr_in6 *addr) {
    return static_cast<const struct sockaddr *>(static_cast<const void *>(addr));
}

struct sockaddr *sockets::sockaddr_cast(struct sockaddr_in6 *addr){
    return static_cast<struct sockaddr *>(static_cast<void *>(addr));
}

const struct sockaddr_in *sockets::sockaddr_in_cast(const struct sockaddr *addr) {
    return static_cast<const sockaddr_in *>(static_cast<const void *>(addr));
}

const struct sockaddr_in6 *sockets::sockaddr_in6_cast(const struct sockaddr *addr) {
    return static_cast<const struct sockaddr_in6 *>(static_cast<const void *>(addr));
}

// 这些函数用于创建、绑定、监听、接受连接和建立连接。
// 如果操作失败，会记录日志并可能终止程序
int sockets::createNonblockingOrDie(sa_family_t family) {
#if VALGRIND
    int sockfd = ::socket(family, SOCK_STREAM, IPIPROTO_TCP);
    if (sockfd < 0) {
        LOG(FATAL) << "sockets::createNonblocking OrDie";
    }

    setNonBlockAndCloseOnExec(sockfd);
#else
    int sockfd = 
        ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0) {
        LOG(FATAL) << "sockets::createNonblocingOrDie";
    }
#endif
    return sockfd;
}

void sockets::bindOrDie(int sockfd, const struct sockaddr *addr){
    int ret = 
        ::bind(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
    if (ret < 0) {
        LOG(FATAL) << "sockets::bindOrDie";
    }
}

void sockets::listenOrDie(int sockfd) {
    int ret = ::listen(sockfd, SOMAXCONN);
    if (ret < 0) {
        LOG(FATAL) << "sockets::listenOrDie";
    }
}

int sockets::accept(int sockfd, struct sockaddr_in6 *addr) {
    socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
#if VALGRIND || defined(NO_ACCEPT4)
    int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
    setNonBlockANdCloseOnExec(connfd);
#else
    int connfd = ::accept4(sockfd, sockaddr_cast(addr), &addrlen,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
    if (connfd < 0) {
        int savedErrno = errno;
        LOG(ERROR) << "Sockets::accept";
        switch (savedErrno) {
        case EAGAIN:
        case ECONNABORTED:
        case EINTR:
        case EPROTO:  // ???
        case EPERM:
        case EMFILE:  // per-process lmit of open file desctiptor ???
            // expected errors
            errno = savedErrno;
            break;
        case EBADF:
        case EFAULT:
        case EINVAL:
        case ENFILE:
        case ENOBUFS:
        case ENOMEM:
        case ENOTSOCK:
        case EOPNOTSUPP:
            // unexpected errors
            LOG(FATAL) << "unexpected error of ::accept " << savedErrno;
            break;
        default:
            LOG(FATAL) << "unknown error of ::accept " << savedErrno;
            break;
        }
    }
    return connfd;
}

int sockets::connect(int sockfd, const struct sockaddr *addr) {
    return ::connect(sockfd, addr, 
                        static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
}

// 这些函数用于从套接字读取数据、使用分散 / 聚集 I/O 读取数据、
// 向套接字写入数据和关闭套接字

ssize_t sockets::read(int sockfd, void *buf, size_t count) {
    return ::read(sockfd, buf, count);
}

ssize_t sockets::readv(int sockfd, const struct iovec *iov, int iovcnt) {
    return ::readv(sockfd, iov, iovcnt);
}

ssize_t sockets::write(int sockfd, const void *buf, size_t count) {
    return ::write(sockfd, buf, count);
}

void sockets::close(int sockfd) {
    if (::close(sockfd) < 0) {
        LOG(ERROR) << "socket::close";
    }
}

// 这些函数用于将套接字地址转换为字符串形式、
// 从字符串形式转换为套接字地址、获取套接字错误码、
// 获取本地地址、获取对端地址以及检查是否是自连接
void sockets::toIpPort(char *buf, size_t size, const struct sockaddr *addr) {
    if (addr->sa_family == AF_INET6) {
        buf[0] = '[';
        toIp(buf + 1, size - 1, addr);
        size_t end = ::strlen(buf);
        const struct sockaddr_in6 *addr6 = sockaddr_in6_cast(addr);
        uint16_t port = sockets::networkToHost16(addr6->sin6_port);
        assert(size > end);
        /**
         * #include <stdio.h>
         * int snprintf(char *str, size_t size, const char *format, ...);
         * str：目标字符串缓冲区的指针。
         * size：最多写入的字符数（包括结尾的 \0）。
         * format：格式字符串（和 printf 一样）。
         * ...：后续参数，用于格式化。
         * snprintf 会最多写入 size-1 个字符，并自动在末尾加上 \0，防止缓冲区溢出。
         * 比 sprintf 更安全，推荐使用。
         */
        snprintf(buf + end, size - end, "]:%u", port);
        return ;
    }
    toIp(buf, size, addr);
    size_t end = ::strlen(buf);
    const struct sockaddr_in *addr4 = sockaddr_in_cast(addr);
    uint16_t port = sockets::networkToHost16(addr4->sin_port);
    assert(size > end);
    snprintf(buf + end, size - end, ":%u", port);
}

void sockets::toIp(char *buf, size_t size, const struct sockaddr *addr) {
    if (addr->sa_family == AF_INET) {
        assert(size >= INET_ADDRSTRLEN);
        const struct sockaddr_in *addr4 = sockaddr_in_cast(addr);
        ::inet_ntop(AF_INET, &addr4->sin_addr, buf, static_cast<socklen_t>(size));
    } else if (addr->sa_family == AF_INET6) {
        assert(size >= INET6_ADDRSTRLEN);
        const struct sockaddr_in6 *addr6 = sockaddr_in6_cast(addr);
        ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, static_cast<socklen_t>(size));
    }
}

void sockets::fromIpPort(const char *ip, uint16_t port,
                            struct sockaddr_in *addr) {
    addr->sin_family = AF_INET;
    addr->sin_port = hostToNetwork16(port);
    if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0) {
        LOG(ERROR) << "sockets::fromIpPort";
    }
}

// 把一个 IPv6 地址字符串和端口号，转换成可以用于 socket 编程的 sockaddr_in6 结构体。
void sockets::fromIpPort(const char *ip, uint16_t port, 
                        struct sockaddr_in6 *addr) {
    addr->sin6_family = AF_INET6;
    addr->sin6_port = hostToNetwork16(port);

    // 把主机字节序的端口号转换为网络字节序（大端），并赋值。
    // inet_pton(AF_INET6, ip, &addr->sin6_addr)
    if (::inet_pton(AF_INET6, ip, &addr->sin6_addr) <= 0) {
        LOG(ERROR) << "sockets::fromIpPort";
    }
}

int sockets::getSocketError(int sockfd) {
    int optval;
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);
    LOG(INFO) << "111111" << sockfd;

    // SOL_SOCKET：表示要操作的是套接字层面的选项。
    // SO_ERROR：表示要获取 socket 的错误状态。
    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
        return errno;
    } else {
        return optval;
    }
}

struct sockaddr_in6 sockets::getLocalAddr(int sockfd) {
    struct sockaddr_in6 localaddr;
    memset(&localaddr, 0, sizeof(localaddr));
    socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);

    // getsockname 是一个系统调用，用于获取与某个 socket 关联的本地地址信息。
    // 这行代码的作用是：获取 sockfd 这个 socket 当前绑定的本地 IP 地址和端口号，并存入 localaddr 结构体。
    if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0) {
        LOG(ERROR) << "sockets::getLocalAddr";
    }
    return localaddr;
}

struct sockaddr_in6 sockets::getPeerAddr(int sockfd) {
    struct sockaddr_in6 peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
    if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0) {
        LOG(ERROR) << "sockets::getPeerAddr";
    }
    return peeraddr;
}

// 这个函数的作用是：
// 判断一个 socket 是否“自连接”（self-connect），也就是本地地址和远端地址是否完全相同。
bool sockets::isSelfConnect(int sockfd) {
    struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
    struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);
    if (localaddr.sin6_family == AF_INET) {
        const struct sockaddr_in *laddr4 = 
            reinterpret_cast<struct sockaddr_in *>(&localaddr);
        const struct sockaddr_in *raddr4 = 
            reinterpret_cast<struct sockaddr_in *>(&peeraddr);
        return laddr4->sin_port == raddr4->sin_port && 
                laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
    } else if (localaddr.sin6_family == AF_INET6) {
        return localaddr.sin6_port == peeraddr.sin6_port && 
                memcmp(&localaddr.sin6_addr, &peeraddr.sin6_addr, 
                        sizeof localaddr.sin6_addr) == 0;
    } else {
        return false;
    }
}