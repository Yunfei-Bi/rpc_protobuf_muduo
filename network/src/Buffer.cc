#include "network/Buffer.h"

#include <errno.h>
#include <sys/uio.h>

#include "network/SocketsOps.h"


// 这段代码实现了 network 命名空间下 Buffer 类的 readFd 方法，
//其主要功能是从指定的文件描述符 fd 中读取数据到 Buffer 对象中。

namespace network {

// 定义常量 \r\n，通常用于网络协议中的换行符
const char Buffer::kCRLF[] = "\r\n";

// 声明类静态常量，这些常量在类的实现文件中进行定义
const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

// 实现 readFd 方法
ssize_t Buffer::readFd(int fd, int *savedErrno) {
  // saved an ioctl()/FIONREAD call to tell how much to read
  // const size_t writable = writableBytes();
  // char *buf = begin() + writerIndex_;
  // const size_t buf_size = writable;

  // // Read data directly into the buffer
  // const ssize_t n = sockets::read(fd, buf, buf_size);
  // if (n < 0) {
  //   *savedErrno = errno;
  // } else {
  //   writerIndex_ += n;
  // }

  // return n;
  // 注释掉的代码部分展示了最初的实现思路，使用 read 系统调用直接读取数据到 Buffer 中
  // 但当前代码采用了更高效的 readv 方法

  // // saved an ioctl()/FIONREAD call to tell how much to read
  
  // 定义一个大小为 1MB 的额外缓冲区，用于处理 Buffer 可写空间不足的情况
  char extrabuf[1024 * 1024];
  struct iovec vec[2]; // 定义一个 iovec 结构体数组，用于 readv 系统调用
  const size_t writable = writableBytes(); // 获取 Buffer 当前的可写字节数
  
  // 设置 iovec 结构体的第一个元素，指向 Buffer 的可写区域
  vec[0].iov_base = begin() + writerIndex_;
  vec[0].iov_len = writable;
  // 设置 iovec 结构体的第二个元素，指向额外缓冲区
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
  // when there is enough space in this buffer, don't read into extrabuf.
  // when extrabuf is used, we read 128k-1 bytes at most.

  // 根据 Buffer 的可写空间决定使用几个缓冲区进行读取
  const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
  // 调用 SocketsOps 中的 readv 方法进行数据读取
  const ssize_t n = sockets::readv(fd, vec, iovcnt);
  if (n < 0) {
    // 读取失败，保存错误码
    *savedErrno = errno;
  } else if (size_t(n) <= writable) {
    // 读取的数据量小于等于 Buffer 的可写空间，直接更新 writerIndex
    writerIndex_ += n;
  } else {
    // 读取的数据量超过了 Buffer 的可写空间
    // 先将 writerIndex 设为 Buffer 的最大大小
    writerIndex_ = buffer_.size();
    // 将额外缓冲区中的数据追加到 Buffer 中
    append(extrabuf, n - writable);
  }
  // if (n == writable + sizeof extrabuf)
  // {
  //   goto line_30;
  // }
  // 返回读取的字节数
  return n;
}

}  // namespace network