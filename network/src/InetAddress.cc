#include <netdb.h>
#include <netinet/in.h>
#include <cassert>
#include <glog/logging.h>

#include "network/InetAddress.h"
#include "network/Endian.h"
#include "network/SocketsOps.h"

#pragma GCC diagnostic ignored "-Wold-style- cast"
static const in_addr_t kInaddrAny = INADDR_ANY;
static const in_addr_t kInaddrLoopback = INADDR_LOOPBACK;
#pragma GCC diagnostic error "-wold-style-cast"

using namespace network;

// 通过编译期断言确保 InetAddress 类与系统地址结构的兼容性
// 确保 InetAddress 与 sockaddr_in6 大小一致
// 确保地址结构中关键字段的偏移量一致，保证类型转换的安全性
// 语法：offsetof(struct_type, member)
// 返回值：member 在 struct_type 结构体中的偏移字节数（size_t 类型）。
static_assert(sizeof(InetAddress) == sizeof(struct sockaddr_in6),
              "InetAddress is same size as sockaddr_in6");
static_assert(offsetof(sockaddr_in, sin_family) == 0, "sin_family offset 0");
static_assert(offsetof(sockaddr_in6, sin6_family) == 0, "sin6_family offset 0");
static_assert(offsetof(sockaddr_in, sin_port) == 2, "sin_port offset 2");
static_assert(offsetof(sockaddr_in6, sin6_port) == 2, "sin6_port offset 2");

InetAddress::InetAddress(uint16_t portArg, bool loopbackOnly, bool ipv6) {
    static_assert(offsetof(InetAddress, addr6_) == 0, "addr6_ offset 0");
    static_assert(offsetof(InetAddress, addr_) == 0, "addr_ offset 0");

  // 根据 ipv6 标志选择 IPv4 或 IPv6 地址结构
  // 支持环回地址（loopbackOnly）和任意地址（kInaddrAny）
  // 使用 sockets::hostToNetwork 系列函数进行字节序转换
  if (ipv6) {
    memset(&addr6_, 0, sizeof(addr6_));
    addr6_.sin6_family = AF_INET6;
    in6_addr ip = loopbackOnly ? in6addr_loopback : in6addr_any;
    addr6_.sin6_addr = ip;
    addr6_.sin6_port = sockets::hostToNetwork16(portArg);
  } else {
    memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    in_addr_t ip = loopbackOnly ? kInaddrLoopback : kInaddrAny;
    addr_.sin_addr.s_addr = sockets::hostToNetwork32(ip);
    addr_.sin_port = sockets::hostToNetwork16(portArg);
  }
}

InetAddress::InetAddress(const std::string &ip, uint16_t portArg, bool ipv6) {
    if (ipv6 || strchr(ip.c_str(), ':')) {
        memset(&addr6_, 0, sizeof(addr6_));
        sockets::fromIpPort(ip.c_str(), portArg, &addr6_);
    } else {
        memset(&addr_, 0, sizeof(addr_));
        sockets::fromIpPort(ip.c_str(), portArg, &addr_);
    }
}

// toIpPort()：将地址转换为 "IP: 端口" 格式的字符串
std::string InetADdress::toIpPort() const {
    char buf[64] = "";
    sockets::toIpPort(buf, sizeof buf, getSockAddr());
    return buf;
}

// toIp()：仅转换 IP 地址部分
std::string InetAddress::toIp() const {
    char buf[64] = "";
    sockets::toIp(buf, sizeof buf, getSockAddr());
    return buf;
}

// ipv4NetEndian()：获取 IPv4 地址的网络字节序表示
uint32_t InetAddress::ipv4NetEndian() const {
    assert(family() == AF_INET);
    return addr_.sin_addr.s_addr;
}

// port()：获取端口的主机字节序表示
uint16_t InetAddress::port() const {
    // 依赖 sockets::networkToHost16 进行字节序转换
    return sockets::networkToHost16(portNetEndian());
}

// 使用线程局部缓冲区 t_resolveBuffer 避免线程安全问题
static __thread char t_resolveBuffer[64 * 1024];

bool InetAddress::resolve(const std::string &hostname, InetAddress *out) {
    assert(out != NULL);
    struct hostent hent;
    struct hostent *he = NULL;
    int herrno = 0;
    memset(&hent, 0, sizeof(hent));

    // 调用 gethostbyname_r 安全版本的主机名解析函数
    // 这个函数的作用是：将主机名（如 "www.example.com"）解析为 IP 地址，并存储到 InetAddress 对象中。
    int ret = gethostbyname_r(hostname.c_str(), &hent, t_resolveBuffer, 
                                sizeof t_resolveBuffer, &he, &herrno);
    if (ret == 0 && he != NULL) {
        // 支持 IPv4 地址解析（通过断言限制）
        assert(he->h_addrtype == AF_INET && he->h_length == sizeof(uint32_t));
        // 解析成功后将结果存储到 InetAddress 对象中
        out->addr_.sin_addr = *reinterpret_cast<struct in_addr *>(he->h_addr);
        return true;
    } else {
        if (ret) {
            LOG(ERROR) << "InetAddress::resolve";
        }
        return false;
    }
}

// 专门为 IPv6 地址设置作用域 ID
// 用于本地链路地址等场景
// 确保仅对 IPv6 地址生效
void InetAddress::setScopeId(uint32_t scope_id) {
    if (family() == AF_INET6) {
        addr6_.sin6_scope_id = scope_id;
    }
}
