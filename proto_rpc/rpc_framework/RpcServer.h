#pragma once

#include "network/TcpServer.h"

namespace google {
namespace protobuf {

class Service;


} // namespace protobuf
} // namespace google

namespace network {

class RpcServer {
public:
    RpcServer(EventLoop *loop, const InetAddress &listenAddr);

    void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

    void registerService(::google::protobuf::Service *);

    void start();
private:
    void onConnection(const TcpConenctionPtr &conn);

    TcpServer server_;
    std::map<std::string. ::google::protobuf::Service *> services_;
};

} // namespace network