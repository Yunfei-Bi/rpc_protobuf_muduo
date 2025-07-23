### 提问：
​	也就是说，client和server通信时，两端个维护着一个RpcChannel，这个RpcChannel中有很多方法，对于client的RpcChannel来说，能够向远程服务器发送一个监控信息请求，并且通过调用RpcCodec中的send序列话要发送的内容；对于server的RpcChannel来说，能够根据实现注册好的服务，检查client发来的请求是否属于服务范围，如果属于服务范围，就通过RpcChannel调用handle_request_msg来解析请求，并通过RpcCodec的send将resposne返回给client。client通过handle_response_msg首先检查本地是否有发送前对应的id，如果有，就反序列化server的response，并通过Rpc框架，找到closure函数，打印reponse



### 回答：
1. 双方各自维护一个 RpcChannel
-  client 端和server 端各自有自己的 RpcChannel，它们分别负责本地的协议处理和消息分发。

2. client 端 RpcChannel 的主要职责
- 发起 RPC 请求：比如向服务器发送监控信息请求。
- 序列化请求：通过 RpcCodec::send 把请求消息序列化为字节流，写入 TCP 连接。
- 记录请求回调：在 outstandings_ 里登记请求 id 和回调（closure），以便后续响应到达时分发。
- 处理响应：收到服务器响应后，handle_response_msg 检查 id 是否存在于 outstandings_，如果有，反序列化 response，调用对应的 closure 处理结果（如打印 response）。

3. server 端 RpcChannel 的主要职责
- 接收并解析请求：通过 RpcCodec 解包收到的字节流，还原为 RpcMessage。
- 服务分发：检查请求的服务名和方法名是否在注册的服务表（services_）中。
- 处理请求：如果服务存在，调用 handle_request_msg，反序列化请求参数，分发到对应的服务方法（如 MonitorInfo）。
- 生成响应：服务方法处理完毕后，填充 response，并通过 closure 回调（如 doneCallback）。
- 序列化响应：通过 RpcCodec::send 把 response 序列化为字节流，写回给 client。



整个**Rpc**部分的流程图如下：
![Rpc](./img/Rpc调用流程图.png)