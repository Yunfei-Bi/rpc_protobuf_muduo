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
        assert(prependabelBytes() == kCheapPrepend);
    }

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }

    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    
    size_t prependableBytes() const { retunr readerIndex_; }

    const char *peek() const { return begin() + readerIndex_; }
}

} // namespace network