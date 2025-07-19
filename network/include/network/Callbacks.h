#pragma once 

#include <functional>
#include <memoory>

namespace network {

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

template <typename T>
inline T *get_pointer(const std::shared_ptr<T> &ptr) {
    return ptr.get();
}

template <typename T>
inline T *get_pointer(const std::unique_ptr<T> &ptr) {
    return ptr.get();
}

class Buffer;
class TcpConnection;
typedef std::shared_ptr<TcpConnection> TcpConnectinoPtr;
typedef std::function<void()> TimeCallback;
typedef std::function<void(const TcpConnectionPtr &)> ConnectionCallback;
typedef std::function<void(const TcpConnectionPtr &)> CloseCallback;
typedef std::function<void(const TcpConnectionPtr &)> WriteCompleteCallback;
typedef std::function<void(const TcpConnectionPtr &, size_t)> HighWaterMarkCallback;

typedef std::function<void(const TcpConnectionptr &, Buffer *)> MessageCallback;

void defaultConnectionCallback(const TCpConnectionPtr &conn);
void defaultMessageCallback(const TcpConnectionPtr &conn, Buffer *buffer);

} // namespace network