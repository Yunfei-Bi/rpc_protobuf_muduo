#include <zlib.h>
#include <google/protobuf/message.h>

#include "RpcCodec.h"
#include "network/Endian.h"
#include "network/TcpConnection.h"

using namespace network;
namespace network
{

    void ProtoRpcCodec::send(const TcpConnectionPtr &conn, const ::google::protobuf::Message &message)
    {
        Buffer buf;
        fillEmptyBuffer(&buf, message);
        conn->send(&buf);
    }

    /**
     * 这段代码的作用是循环从缓冲区中解析出完整的 RPC 消息，每解析出一条就回调处理，并从缓冲区移除
     * 能处理粘包、半包等网络常见问题。
     * 如果遇到不合法或不完整的数据，会停止解析，等待更多数据到来。
     */
    void ProtoRpcCodec::onMessage(const TcpConnectionPtr &conn, Buffer *buf)
    {

        // 只要缓冲区中剩余的数据长度大于等于“最小消息长度+头部长度”，就尝试解析消息。
        while (buf->readableBytes() >= static_cast<uint32_t>(kMinMessageLen + kHeaderLen))
        {

            // 读取消息头部的长度字段（通常前4字节），但不移动读指针。
            const int32_t len = buf->peekInt32();

            // 如果消息长度不合法（太大或太小），停止解析，防止异常或攻击
            if (len > kMaxMessageLen || len < kMinMessageLen)
            {
                break;
            }
            else if (buf->readableBytes() >= size_t(kHeaderLen + len))
            { // 如果缓冲区中数据够一条完整消息（头+体），就可以解析。

                // 创建一个新的 RpcMessage 对象。
                // 调用 parse 方法解析消息体内容（跳过头部），把解析结果放到 message 里。
                RpcMessagePtr message(new RpcMessage());
                ErrorCode errorCode = parse(buf->peek() + kHeaderLen, len, message.get());

                // 如果解析成功（kNoError），就调用 messageCallback_ 处理这条消息，并从缓冲区移除这条消息的数据。
                if (errorCode == kNoError)
                {
                    messageCallback_(conn, message);
                    buf->retrieve(kHeaderLen + len);
                }
                else
                { // 如果解析失败，停止解析，等待更多数据到来。
                    break;
                }
            }
            else
            { // 如果缓冲区数据还不够一条完整消息，退出循环，等待更多数据。
                break;
            }
        }
    }

    /**
     * 这个函数的作用就是把一段原始二进制数据反序列化为 protobuf 消息对象。
     * 调用 protobuf 的 ParseFromArray 方法，把 buf 指向的二进制数据解析成一个 protobuf 消息对象
     * 如果解析成功，返回 true；否则返回 false。
     * const void *buf：指向一段二进制数据的指针（通常是网络收到的原始数据）。
     * int len：这段数据的长度（字节数）。
     * google::protobuf::Message *message：一个 protobuf 消息对象指针，准备把数据解析到这里面。
     */
    bool ProtoRpcCodec::parseFromBuffer(const void *buf, int len, google::protobuf::Message *message)
    {
        return message->ParseFromArray(buf, len);
    }

    /**
     * 这段代码是 ProtoRpcCodec::serializeTOBuffer 方法的实现，
     * 作用是把一个 protobuf 消息对象序列化为二进制数据，
     * 并写入到自定义的 Buffer 中。下面详细解释每一步
     */
    int ProtoRpcCodec::serializeToBuffer(const google::protobuf::Message &message, Buffer *buf)
    {

/**
 * 兼容不同版本的 protobuf：
 * 新版用 ByteSizeLong()，老版用 ByteSize()。
 * 得到序列化后消息的字节数。
 */
#if GOOGLE_PROTOBUF_VERSION > 3009002
        int byte_size = google::protobuf::internal::ToIntSize(message.ByteSizeLong());
#else
        int byte_size = message.ByteSize();
#endif

        /**
         * 确保 buf 有足够的可写空间，能容纳序列化后的数据和校验和（kChecksumLen）。
         * kChecksumLen 可能是后面要加的校验和长度。
         */
        buf->ensureWritableBytes(bytes_size + kChecksumLen);

        // 获取缓冲区当前可写区域的起始指针。
        uint8_t *start = reinterpret_cast<uint8_t *>(buf->beginWrite());

        // 把 message 序列化为二进制数据，写入到 start 指向的内存区域。
        // 返回值 end 是写入结束后的指针。
        uint8_t *end = message.SerializeWithCachedSizesToArray(start);

        // 检查实际写入的字节数是否和预期一致（这里没有处理异常，仅做了空判断）。
        if (end - start != byte_size)
        {
        }

        // 通知缓冲区已经写入了 byte_size 字节，更新写指针。
        buf->hasWritten(byte_size);

        // 返回序列化写入的字节数
        return byte_size;
    }

    /**
     * 这段代码的作用是对收到的二进制数据做校验和检查、类型标记检查，
     * 然后反序列化为 protobuf 消息对象，并返回相应的错误码
     */
    ProtoRpcCodec::ErrorCode ProtoRpcCOdec::parse(const char *buf, int len,
                                                  ::google::protobuf::Message *message)
    {
        ErrorCode error = kNoError;

        // 首先调用 validateCheckSum 检查数据的校验和是否正确，防止数据在传输过程中被破坏。
        if (validateCheckSum(buf, len))
        {

            // 检查数据开头是否有正确的“消息类型标记”（tag_），确保数据格式正确。
            if (memcmp(buf, tag_.data(), tag_.size()) == 0)
            {

                // 取出消息体数据
                const har *data = buf + tag_.size();
                int32_t datalen = len - kCheckSum - static_cast<int>(tag_.size());

                // 调用 parseFromBuffer，把消息体二进制数据反序列化为 protobuf 消息对象。
                if (parseFromBuffer(data, dataLen, message))
                {
                    error = kNoError;
                }
                else
                {
                    error = kParseError;
                }
            }
            else
            {
                error = kUnknownMessageType;
            }
        }
        else
        {
            error = kCheckSumError;
        }

        // 返回解析结果的错误码
        return error;
    }

    /**
     * 这段代码是 ProtoRpcCodec::filEmptyBuffer 方法的实现，
     * 作用是把一个 protobuf 消息对象序列化并封装成完整的网络数据包，写入到空的缓冲区 buf 中。
     */
    void ProtoRpcCodec::filEmptyBuffer(Buffer *buf, const google::protobuf::Message &message)
    {
        assert(buf->readableBytes() == 0);

        // 把消息类型标记（tag_，通常是一个固定字符串或字节序列）写入缓冲区开头，用于后续识别消息类型
        buf->append(tag_);

        // 把 protobuf 消息对象序列化为二进制数据，追加到缓冲区，并返回写入的字节数。
        int byte_size = serializeToBuffer(message, buf);

        // 对当前缓冲区（tag + 消息体）做校验和，得到 checkSum。
        int32_t checkSum = checksum(buf->peek(), static_cast<int>(buf->readableBytes()));

        // 把校验和（4字节整数）追加到缓冲区末尾，用于后续校验数据完整性。
        buf->appendInt32(checkSum);

        // 断言当前缓冲区长度等于 tag 长度 + 消息体长度 + 校验和长度，确保数据包格式正确。
        assert(buf->readableBytes() == tag_.size() + byte_size + kChecksumLen);
        (void)byte_size;

        // 计算整个数据包的长度（tag + 消息体 + 校验和），并转换为网络字节序（大端）
        int32_t len = sockets::hostToNetwork32(static_cast<int32_t>(buf->readableBytes()));

        // 把长度字段插入到缓冲区最前面，作为包头，方便接收方知道一条完整消息有多长。
        buf->prepend(&len, sizeof len);
    }

    /**
     * 这段代码是 ProtoRpcCodec::asInt32 方法的实现，
     * 作用是把一段二进制数据（4字节）按网络字节序（大端）解析为本地的 int32_t 整数
     */
    int32_t ProtoRpcCodec::asInt32(const char *buf)
    {
        int32_t be32 = 0;
        ::memcpy(&be32, buf, sizeof(be32));
        return sockets::networkToHost(be32);
    }

    /**
     * 这个函数的作用是对一段数据生成一个 32 位的校验码，
     * 可以用来在网络通信中校验数据包的完整性
     * 发送方和接收方都用同样的算法计算校验和，
     * 如果结果不一致，说明数据被破坏或篡改。
     *
     * const void *buf：指向要计算校验和的数据的指针。
     * int len：数据的长度（字节数）。
     * ::adler32 是一个常用的校验和算法，通常来自 zlib 库。
     */
    int32_t ProtoRpcCodec::checksum(const void *buf, int len)
    {
        return static_cast<int32_t>(::adler32(1, static_cast<const Bytef *>(buf), len));
    }

    /**
     *
     *
     * const char *buf ，指向包含校验和的数据包。
     * int len：整个数据包的长度（包括校验和）
     */
    bool ProtoRpcCodec::validateChecksum(const char *buf, int len)
    {

        /**
         * 提取期望的校验和
         * 从数据包的末尾提取出期望的校验和。
         * buf + len - kChecksumLen 指向校验和字段的起始位置。
         * asInt32 把4字节的校验和转换为 int32_t
         */
        int32_t expectedCheckSum = asInt32(buf + len - kChecksumLen);

        /**
         * 计算实际的校验和
         * 对数据包中除了校验和之外的部分计算校验和。
         * buf 到 buf + len - kChecksumLen 是数据部分
         */
        int32_t checkSum = checksum(buf, len - kChecksumLen);

        // 如果计算出的校验和和期望的校验和一致，返回 true，说明数据完整；否则返回 false
        return checkSum == expectedCheckSum;
    }

} // namespace network