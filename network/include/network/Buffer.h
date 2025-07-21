#pragma once 

#include <assert.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "network/Endian.h"

namespace network {

class Buffer {
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024 * 4;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
        readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {
        assert(readableBytes() == 0);
        assert(writableBytes() == initialSize);
        assert(prependableBytes() == kCheapPrepend);
    }

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }

    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    
    size_t prependableBytes() const { return readerIndex_; }

    /**
     * peek() 的作用是获取指向 Buffer 中可读数据的指针，
     * 让你可以读取数据而不改变 Buffer 的读指针位置。
     */
    const char *peek() const { return begin() + readerIndex_; }

    /**
     * 这个函数的作用是丢弃缓冲区前面 len 字节的数据，并调整读指针。
     */
    void retrieve(size_t len) {
        assert(len <= readableBytes());
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveInt64() { retrieve(sizeof(int64_t)); }

    void retrieveInt32() { retrieve(sizeof(int32_t)); }

    void retrieveInt16() { retrieve(sizeof(int16_t)); }

    void retrieveInt8() { retrieve(sizeof(int8_t)); }

    /**
     * retrieveAll() 的作用是快速清空 Buffer 中的所有数据，
     * 通过重置读写指针到初始位置来实现，比逐个删除数据更高效
     */
    void retrieveAll() {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    std::string retrieveAllAsString(size_t len)  {
        return retrieveAsString(readableBytes());
    }

    /**
     * 这个函数的作用是：从 Buffer 中取出指定长度的数据，并转换为 std::string 返回。
     * 1. assert(len <= readableBytes());
     * 断言：要取出的长度不能超过 Buffer 中可读数据的长度。
     * 如果 len > readableBytes()，程序会中断（调试时）。
     * 2. std::string result(peek(), len);peek() 返回指向可读数据起始位置的指针。
     * 用这个指针和长度 len 构造一个 std::string。
     * 这样就把 Buffer 中的数据复制到了 result 中。
     * 3. retrieve(len);
     * 从 Buffer 中移除已经取出的 len 个字节。
     * 相当于把读指针向前移动 len 个位置。
     * 4. return result;
     * 返回包含数据的 std::string。
     */
    std::string retrieveAsString(size_t len) {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    void append(const std::string &str) { append(str.data(), str.size()); }

    void append(const char *data, size_t len) {
        ensureWritableBytes(len);

        /**
         *  源数据: [data] -----> [data + len)
                                |
                                v
         *  Buffer: [已读数据][可写区域]
                                ^
                                |
         *                beginWrite()
         */
        std::copy(data, data + len, beginWrite());
        hasWritten(len);
    }

    void append(const void *data, size_t len) {
        append(static_cast<const char *>(data), len);
    }

    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }

    char *beginWrite() { return begin() + writerIndex_; }

    char *beginWrite() const { return begin() + writerIndex_; }

    void hasWritten(size_t len) {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }
    void unwrite(size_t len) {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }

    /**
     * Append int64_t using network endian
     * 这些函数的作用是：将本地字节序的整数转换为网络字节序，
     * 然后追加到 Buffer 的末尾。
     * 
     * 追加前: 
     * [已读数据]      [可写区域]
     *  ^             ^
     *  |             |
     *  readerIndex_  writerIndex_
     * 追加后:
     * [已读数据]      [新数据]  [可写区域]
     * ^              ^        ^
     * |              |        |
     * readerIndex_            writerIndex_
     */
    void appendInt64(int64_t x) {
        int64_t be64 = sockets::hostToNetwork64(x);
        append(&be64, sizeof be64);
    }
    void appendInt32(int32_t x) {
        int32_t be32 = sockets::hostToNetwork32(x);
        append(&be32, sizeof be32);
    }
    void appendInt16(int16_t x) {
        int16_t be16 = sockets::hostToNetwork16(x);
        append(&be16, sizeof be16);
    }
    void appendInt8(int8_t x) {
        append(&x, sizeof x); 
    }

    /**
     * Read int64_t from network endian
     * 这些函数的作用是：从 Buffer 中读取指定类型的整数，
     * 并自动更新读指针位置。
     * 
     * 与 peekInt32() 的区别
     * peekInt32() - 只读取，不移除
     * int32_t value1 = buffer.peekInt32();  // 读指针不变
     * readInt32() - 读取并移除
     * int32_t value2 = buffer.readInt32();  // 读指针向前移动 4 字节
     * 
     * 这些 read*() 函数是读取并消费操作，会从 Buffer 中移除已读取的数据。
     * 而 peek*() 函数是只读取不消费操作，不会改变 Buffer 的状态。
     */
    int32_t readInt32() {
        int32_t result = peekInt32();
        retrieveInt32();
        return result;
    }

    int16_t readInt16() {
        int16_t result = peekInt16();
        retrieveInt16();
        return result;
    }

    int8_t readInt8() {
        int8_t result = peekInt8();
        retrieveInt8();
        return result;
    }

    /**
     * Peek int64_t from network endian
     * 这个函数的作用是：从 Buffer 中读取一个 64 位整数（网络字节序），
     * 并转换为本地字节序返回。
     */
    int64_t peekInt64() const {
        assert(readableBytes() >= sizeof(int64_t));
        int64_t be64 = 0;
        ::memcpy(&be64, peek(), sizeof be64);
        return sockets::networkToHost64(be64);
    }
    int32_t peekInt32() const {
        assert(readableBytes() >= sizeof(int32_t));
        int32_t be32 = 0;
        ::memcpy(&be32, peek(), sizeof be32);
        return sockets::networkToHost32(be32);
    }
    int16_t peekInt16() const {
        assert(readableBytes() >= sizeof(int16_t));
        int16_t be16 = 0;
        ::memcpy(&be16, peek(), sizeof be16);
        return sockets::networkToHost16(be16);
    }
    /**
     * 1. 性能考虑
     * memcpy(&x, peek(), sizeof(int8_t)) 需要调用函数，有函数调用开销。
     * *peek() 直接解引用指针，编译器可以优化为一条指令，更快。
     * 2. 代码简洁性
     * 对于单字节数据，直接用指针解引用更简洁明了。
     * memcpy 需要额外的变量和函数调用，代码更复杂。
     * 3. 编译器优化
     * 现代编译器对简单的指针解引用有很好的优化。
     * 对于 int8_t（1字节），编译器可能直接生成 mov 指令。
     */
    int8_t peekInt8() const {
        assert(readableBytes() >= sizeof(int8_t));
        int8_t x = *peek();
        return x;
    }


    /**
     * Prepend int64_t using network endian
     * 这些函数的作用是：在 Buffer 的可读数据前面插入数据（前置插入）
     * 
     * 插入前：
     * [预留空间]      [可读数据]      [可写空间]
     * ^              ^             ^
     * |              |             |
     * kCheapPrepend  readerIndex_  writerIndex_
     * 插入后：
     * [预留空间]       [新数据]       [可读数据]      [可写空间]
     * ^               ^             ^              ^
     * |               |             |              |
     * kCheapPrepend                 readerIndex_   writerIndex_
     */
    void prependInt64(int64_t x) {
        int64_t be64 = sockets::hostToNetwork64(x);
        prepend(&be64, sizeof be64);
    }
    void prependInt32(int32_t x) {
        int32_t be32 = sockets::hostToNetwork32(x);
        prepend(&be32, sizeof be32);
    }

    void prependInt16(int16_t x) {
        int16_t be16 = sockets::hostToNetwork16(x);
        prepend(&be16, sizeof be16);
    }
    void prependInt8(int8_t x) { 
        prepend(&x, sizeof x); 
    }
    void prepend(const void *data, size_t len) {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        const char *d = static_cast<const char *>(data);
        /**
         * 这行代码的作用是：将数据复制到 Buffer 的读指针位置。
         */
        std::copy(d, d + len, begin() + readerIndex_);
    }

    /**
     * size_t: long unsigned int
     * sszie_t: long int
     */
    size_t internalCapacity() const { return buffer_.capacity(); }

    ssize_t readFd(int fd, int *savedErrno);

private:
    char *begin() { return &*buffer_.begin(); }

    const char *begin() const { return &*buffer_.begin(); }

    /**
     * 这个函数的作用是：为 Buffer 腾出足够的空间来写入 len 个字节的数据
     * 如果可写字节数 + 可前置字节数 < 需要的字节数 + 预留空间，说明空间不够。
     * 情况一：空间不够，需要扩容，直接扩容 Buffer 到 writerIndex_ + len 大小。
     * 情况二：空间够，但需要整理
     * 整理内存：将可读数据移动到 Buffer 开头（预留空间之后）
     * 更新指针：重新设置读写指针位置。
     * 
     * 内存布局变化
     * 整理前：
     * [预留空间]      [已读数据][可读数据]     [可写空间]
     * ^              ^        ^            ^
     * |              |        |            |
     * kCheapPrepend  已读数据  readerIndex_  writerIndex_
     * 整理后：
     * [预留空间]      [可读数据]     [可写空间]
     * ^              ^              ^
     * |              |              |
     * kCheapPrepend  readerIndex_  writerIndex_
     */
    void makeSpace(size_t len) {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
            buffer_.resize(writerIndex_ + len);
        } else {
            assert(kCheapPrepend < readerIndex_);
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_, begin() + writerIndex_, 
                        begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
            assert(readable == readableBytes());
        }
    }

private:
    std::vector<char> buffer_;
    /**
     * [预留空间]       [已读数据]     [可写区域]
        ^              ^         ^
        |              |             |
        kCheapPrepend  readerIndex_  writerIndex_
     */
    size_t readerIndex_;
    size_t writerIndex_;

    /**
     * 这是一个静态常量字符数组的声明，用于存储 CRLF（回车换行符）。
     */
    static const char kCRLF[];
};

} // namespace network