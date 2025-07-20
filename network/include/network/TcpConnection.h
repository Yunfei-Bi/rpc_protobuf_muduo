#pragma once 

#include <memory>
#include <boost/any.hpp>

#include "network/Buffer.h"
#include "network/Callbacks.h"
#include "network/InetAddress.h"

struct tcp_info;

namespace network {

class Channel;
class EventLoop;
class Socket;

// 这样做的目的是让 TcpConnection 的对象可以在成员函数内部安全地获得指向自己的 std::shared_ptr 智能指针。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop *loop, const std::string &name, int sockfd, 
                    const InetAddress &localAddr, const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const std::string &name() const { return name_; }
    const InetAddress &lcoalAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }
    bool connected() const { return state_ == kConnected; }
    bool disconnected() const { return state_ == kDisconnected; }
    bool getTcpInfo(struct tcp_info *) const;
    std::string getTcpInfoString() const;

    void send(Buffer *message);

    void shutdown();

    void forceClose();

    void forceCloseWithDelay(double seconds);

    void setTcpNoDelay(bool on);

    void startRead();

    void stopRead();

    bool isReading() const { return reading_; }

    void setContext(const boost::any &context) { context_ = context; }

    const boost::any &getContext() const { return context_; }

    boost::any *getMutableContext() { return &context_; }

    void setConenctionCallback(const ConnectionCallback &cb) {
        connectionCallback_ = cb;
    }

    void setMessageCallback(const MessageCallback &cb) {
        messageCallback_ = cb;
    }

    void setWriteCompleteCallback(const WriteCompleteCallback &cb) {
        writeCompleteCallback_ = cb;
    }

    Buffer *inputBuffer() { return &inputBuffer_; }

    Buffer *outputBuffer() { return &outputBuffer_; }

    void setCloseCallback(const CloseCallback &cb) { closeCallback_ = cb; }

    void connectEstablished();

    void connectDestroyed();

private:
    enum StateE {
        kDisconnected, 
        kConnecting, 
        kConnected, 
        kDisconnecting
    };

    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const std::string &message);
    void sendInLoop(const void *message, size_t len);
    void shutdownInLoop();

    void forceCloseInLoop();
    void setState(StateE s) { state_ = s; };
    const char *stateToString() const;
    void startReadInLoop();
    void stopReadInLoop();

    EventLoop *loop_;
    const std::string name_;
    StateE state_;
    bool reading_;
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel channel_;
    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    CloseCallback closeCallback_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;
    boost::any context_;
};

typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;


} // namespace network