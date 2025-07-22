#include <unistd.h>
#include <cassert>
#include <glog/logging.h>

#include "network/EventLoop.h"
#include "rpc_framework/RpcServer.h"
#include "monitor.pb.h"

using namespace network;

namespace monitor {

class TestServiceImpl : public TestService {

public:

    /**
     * 这是一个服务端的 RPC 方法实现，通常用于处理客户端发来的 MonitorInfo 请求
     * controller：RPC 控制器（可用于传递错误、取消等信息，通常不用管）
     * request：客户端发来的请求对象（TestRequest），包含了客户端传递的数据
     * response：服务端要返回给客户端的响应对象（TestResponse），你需要填充它
     * done：回调对象，表示处理完成后要调用它（异步通知 RPC 框架）
     */
    virtual void MonitorInfo(::google::protobuf::RpcController *controller,
                            const ::monitor::TestRequest *request, 
                            ::monitor::TestResponse *response, 
                            ::google::protobuf::Closure *done) {
        // 打印收到的请求内容，方便调试。
        LOG(INFO) << " req:\n" << request->DebugString();

        // 设置响应的 status 字段为 true，表示处理成功。
        response->set_status(true);

        // 构造一个字符串 " hight_ " + 请求里的 count 字段，并设置到响应的 cpu_info 字段里
        std::string c = " hight_ " + std::to_string(request->count());
        response->set_cpu_info(c);

        // 通知 RPC 框架处理完成
        done->Run();
    }
};

int main(int argc, char *argv[]) {
    LOG(INFO) << " pid = " << getpid(); // 打印进程 ID
    EventLoop loop; // 创建事件循环对象
    InetAddress listenAddr(9981); // 设置监听地址
    monitor::TestServiceImpl impl; // 创建服务实现对象
    RpcServer server(&loop, listenAddr); // 创建 RPC 服务器对象
    server.registerService(&impl); // 注册服务实现
    server.start(); // 启动服务器
    loop.loop(); // 进入事件循环
    google::protobuf::ShutdownProtobufLibrary();
}

} // namespace monitor

/**
 * services_ 里的每个 service 是什么？
    每个 service 都是一个继承自 google::protobuf::Service 的对象指针。
    这些对象是你用 protobuf（.proto 文件）定义的服务的 C++ 实现类的实例。
    例如，如果你有如下 proto 文件：

    service Greeter {
        rpc SayHello (HelloRequest) returns (HelloReply);
    }

    protoc 编译后会生成一个 Greeter 的 C++ 服务基类（比如 Greeter 或 Greeter::Service），你会写一个继承自它的实现类：

    class GreeterImpl : public Greeter::Service {
        // 实现 SayHello 方法
    };

    你会把 GreeterImpl 的对象注册到 RpcServer，也就是：
    
    GreeterImpl greeter;
    server.registerService(&greeter);

    这样，services_ 里就会有一项，key 是服务的全名（如 "Greeter"），value 是 &greeter，也就是 GreeterImpl 的指针。

    总结
    
    每个 service 是一个继承自 google::protobuf::Service 的对象指针。

    它代表了一个具体的 RPC 服务（比如 Greeter、Calculator、UserService 等）。

    这些对象实现了你在 proto 文件里定义的服务接口，能处理客户端发来的具体 RPC 调用。
 */