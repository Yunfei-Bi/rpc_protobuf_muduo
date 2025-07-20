#pragma once 

#include <mutex>
#include "network/TcpConnection.h"

namespace network {

class Connector;
typedef std::shared_ptr<Connector> ConnectorPtr;

class TcpClient {
public:
    TcpClient(EventLoop *loop, const InetAddress &severAddr, const std::string &nameArg);

    ~TcpClient();

    void connect();

    void disconnect();

    void stop();

    TcpConnectionPtr connection() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return connection_;
    }

    EventLoop *getLoop() const { return loop_; }

    bool retry() const { return retry_; }

    void enableRetry() { retry_ = true; }

    const std::string &name() const { return name_; }

    void setConnectionCallback(ConnectionCallback cb) {
        connectionCallback_ = std::move(cb);
    }

    void setMessageCallback(MessageCallback cb) {
        messageCallback_ = std::move(cb);
    }

    void setWriteCompleteCallback(WriteCompleteCallback cb) {
        writeCompleteCallback_ = std::move(cb);
    }

private:
    void newConnection(int sockfd);

    void removeConnection(const TcpConnectionPtr &conn);

    EventLoop *loop_;

    /**
     * connector_ 这个变量的类型是 ConnectorPtr，通常是一个指向 Connector 类的智能指针
     * （比如 std::shared_ptr<Connector>）。它的作用是负责管理和发起 TCP 连接。
     */
    ConnectorPtr connector_;
    const std::string name_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    bool retry_;
    bool connect_;
    int nextConnId_;
    mutable std::mutex mutex_;

    /**
     * 连接建立后，connection 负责管理数据的读写、关闭等操作。
     * connector 负责“怎么连上去”（连接建立前的事），
     * connection 负责“连上以后怎么通信”（连接建立后的事）
     */
    TcpConnectionPtr connection_;
};

} // namespace network