#include <errno.h>
#include <fcntl.h>
#include <cassert>
#include <glog/logging.h>
#include <unistd.h>

#include "network/Acceptor.h"
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/SocketsOps.h"

/**
 * 通过上述分析，我们可以看出 Acceptor 类是一个关键的网络组件，负责监听和接受新连接，并处理一些异常情况。
 * 它与其他网络组件协同工作，实现了服务器端的网络通信功能。
 */

using namespace network;
namespace network {
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop), // 关联的事件循环
    acceptSocket_(sockets::createNonblockingOrDie(listenAddr.family())), // 创建一个非阻塞的监听套接字
    acceptChannel_(loop, acceptSocket_.fd()), // 为监听套接字创建一个 Channel 对象，用于事件处理
    listening_(false), // 初始化为 false，表示还未开始监听
    idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) { // 打开 /dev/null 文件，用于处理文件描述符耗尽的情况
    assert(idleFd_ >= 0);
    acceptSocket_.setReuseAddr(true); // 允许地址重用
    acceptSocket_.setReusePort(reuseport); // 根据传入的参数设置端口重用
    acceptSocket_.bindAddress(listenAddr); // 将监听套接字绑定到指定的地址和端口

    // 设置读事件回调函数 handleRead
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor() {
    acceptChannel_.disableAll(); // 禁用 acceptChannel_ 的所有事件
    acceptChannel_.remove(); // 从事件循环中移除 acceptChannel_
    ::close(idleFd_); // 关闭 idleFd_
}

void Acceptor::listen() {
    loop_->assertInLoopThread();
    listening_ = true; // 将 listening_ 设置为 true，表示开始监听
    acceptSocket_.listen(); // 调用 acceptSocket_.listen() 开始监听连接请求
    acceptChannel_.enableReading(); // 启用 acceptChannel_ 的读事件，以便在有新连接到来时触发 handleRead 函数
}

void Acceptor::handleRead() {
    loop_->assertInLoopThread(); // 确保在事件循环所在的线程中调用该函数
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr); // 调用 acceptSocket_.accept(&peerAddr) 接受新连接，
    if (connfd >= 0) { // 如果 connfd >= 0，表示成功接受新连接
        if (newConnectionCallback_) { // 如果 newConnectionCallback_ 不为空，调用该回调函数处理新连接
            newConnectionCallback_(connfd, peerAddr); 
        } else {
            sockets::close(connfd); // 否则，关闭新连接的文件描述符
        }
    } else { // 如果 connfd < 0，表示接受连接失败，记录错误日志
        LOG(ERROR) << " in Acceptor::handleRead ";
        // 如果错误码为 EMFILE，表示文件描述符耗尽，通过关闭 idleFd_ 接受一个连接
        // ，然后立即关闭该连接，最后重新打开 /dev/null 文件
        if (errno == EMFILE) {
            ::close(idleFd_);
            idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
            :close(idleFd_);
            idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        }
    }
}

} // namespace network