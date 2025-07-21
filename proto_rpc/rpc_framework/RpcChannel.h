#pragma once 

#include <map>
#include <mutex>
#include <googl/protobuf/service.h>

#include "RpcCodec.h"
#include "rpc.pb.h"

namespace google {

namespace protobuf {

class Descriptor;
class ServiceDescriptor;
class MethodDescriptor;
class Message;

class Closure;

class RpcController;
class Service;

} // namespace protobuf
} // namespace google

namespace network {

class RpcChannel : public ::google::protobuf::RpcChannel {
public:
    RpcChannel();

    explicit RpcChannel(const TcpConnectionPtr &conn);

    ~RpcChannel() override;

    void setConnection(const TcpConnectionPtr &conn) { conn_ = conn; }

    void setServices(const std::map<std::string, ::gogle::protobuf::Service *>services) {
        services_ = services;
    }

    void CallMethod(const ::google::protobuf::MethodDescriptor *method, 
                    ::google::protobuf::RpcController *controller,
                    const ::google:protobuf::Message *request, 
                    ::google::protobuf::Message *response,
                    ::google::protobuf::Closure *done) override;

    void onMessage(const TcpConnectionPtr &conn, Buffer *buf);

private:
    void onRpcMessage(const TcpConnectionPtr &conn, const RpcMessagePtr &messagePtr);

    void doneCallback(::google::protobuf::Message *response, int64_t id);

    void handle_response_msg(const RpcMessagePtr &messagePtr);

    void handle_request_msg(const TcpConnectionPtr &conn, const RpcMessagePtr &messagePtr);

    struct OutstandingCall {
        ::google::protobuf::Message *response;
        ::google::protbuf::Closure *done;
    };

    ProtoRpcCodec codec_;
    TcpConnectionPtr conn_;
    std::atomic<int64_t> id_;

    std::mutex mutex_;
    std::map<int64_t, OutstandingCall> outstandings_;
    const std::map<std::string, ::google::protobuf::Service *> services_;
};

typedef std::shared_ptr<RpcChannel> RpcChannelPtr;

} // namespace network