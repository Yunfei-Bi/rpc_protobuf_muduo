#include <errno.h>
#include <sys/uio.h>

#include "network/Buffer.h"
#include "network/SocketsOps.h"

namespace network {

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

ssize_t Buffer::readFd(int fd, int *savedErrno) {

    /**
     * 定义一个大小为 1MB 的额外缓冲区，用于处理 Buffer 可写空间不足的情况
     * 定义一个 iovec 结构体数组，用于 readv 系统调用
     * 获取 Buffer 当前的可写字节数
     */
    char extrabuf[1024 * 1024];
    struct iovec vec[2];
    const size_t writable = writableBytes();

    /**
     * 设置 iovec 结构体的第一个元素，指向 Buffer 的可写区域
     * 设置 iovec 结构体的第二个元素，指向额外缓冲区
     */
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    /**
     * 根据 Buffer 的可写空间决定使用几个缓冲区进行读取
     * 调用 SocketsOps 中的 readv 方法进行数据读取
     */
    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
    const ssize_t n =sockets::readv(fd, vec, iovcnt);

    /**
     * 读取失败，保存错误码
     * 读取的数据量小于等于 Buffer 的可写空间，直接更新 writerIndex
     * 读取的数据量超过了 Buffer 的可写空间
     * 先将 writerIndex 设为 Buffer 的最大大小
     * 将额外缓冲区中的数据追加到 Buffer 中
     */
    if (n < 0) {
        *savedErrno = errno;
    } else if (size_t(n) <= writable) {
        writerIndex_ += n;
    } else {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    return n;
}

} // namespace network