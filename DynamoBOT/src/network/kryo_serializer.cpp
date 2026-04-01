#include "kryo_serializer.hpp"
#include <cstring>
#include <bit>

namespace dynamo {

// Kryo varint encoding:
// Positive values 0-127: 1 byte (value)
// Values 128-16383: 2 bytes (0x80 | (value & 0x7F), value >> 7)
// And so on for larger values

void KryoBuffer::writeVarInt(int32_t value, bool optimizePositive) {
    uint32_t uvalue;
    if (optimizePositive) {
        uvalue = static_cast<uint32_t>(value);
    } else {
        // ZigZag encoding for signed values
        uvalue = static_cast<uint32_t>((value << 1) ^ (value >> 31));
    }
    
    while (uvalue > 0x7F) {
        buffer_.push_back(static_cast<uint8_t>((uvalue & 0x7F) | 0x80));
        uvalue >>= 7;
    }
    buffer_.push_back(static_cast<uint8_t>(uvalue));
}

int32_t KryoBuffer::readVarInt(bool optimizePositive) {
    uint32_t result = 0;
    int shift = 0;
    uint8_t b;
    
    do {
        ensureRead(1);
        b = buffer_[readPos_++];
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    
    if (optimizePositive) {
        return static_cast<int32_t>(result);
    } else {
        // ZigZag decoding
        const auto decoded = static_cast<int32_t>(result >> 1);
        const auto sign = -static_cast<int32_t>(result & 1u);
        return decoded ^ sign;
    }
}

void KryoBuffer::writeVarLong(int64_t value, bool optimizePositive) {
    uint64_t uvalue;
    if (optimizePositive) {
        uvalue = static_cast<uint64_t>(value);
    } else {
        uvalue = static_cast<uint64_t>((value << 1) ^ (value >> 63));
    }
    
    while (uvalue > 0x7F) {
        buffer_.push_back(static_cast<uint8_t>((uvalue & 0x7F) | 0x80));
        uvalue >>= 7;
    }
    buffer_.push_back(static_cast<uint8_t>(uvalue));
}

int64_t KryoBuffer::readVarLong(bool optimizePositive) {
    uint64_t result = 0;
    int shift = 0;
    uint8_t b;
    
    do {
        ensureRead(1);
        b = buffer_[readPos_++];
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    
    if (optimizePositive) {
        return static_cast<int64_t>(result);
    } else {
        const auto decoded = static_cast<int64_t>(result >> 1);
        const auto sign = -static_cast<int64_t>(result & 1ull);
        return decoded ^ sign;
    }
}

void KryoBuffer::writeInt(int32_t value) {
    buffer_.push_back(static_cast<uint8_t>(value >> 24));
    buffer_.push_back(static_cast<uint8_t>(value >> 16));
    buffer_.push_back(static_cast<uint8_t>(value >> 8));
    buffer_.push_back(static_cast<uint8_t>(value));
}

int32_t KryoBuffer::readInt() {
    ensureRead(4);
    int32_t value = (static_cast<int32_t>(buffer_[readPos_]) << 24) |
                    (static_cast<int32_t>(buffer_[readPos_ + 1]) << 16) |
                    (static_cast<int32_t>(buffer_[readPos_ + 2]) << 8) |
                    static_cast<int32_t>(buffer_[readPos_ + 3]);
    readPos_ += 4;
    return value;
}

void KryoBuffer::writeLong(int64_t value) {
    for (int i = 56; i >= 0; i -= 8) {
        buffer_.push_back(static_cast<uint8_t>(value >> i));
    }
}

int64_t KryoBuffer::readLong() {
    ensureRead(8);
    int64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | buffer_[readPos_++];
    }
    return value;
}

void KryoBuffer::writeFloat(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    writeInt(static_cast<int32_t>(bits));
}

float KryoBuffer::readFloat() {
    int32_t bits = readInt();
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void KryoBuffer::writeDouble(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    writeLong(static_cast<int64_t>(bits));
}

double KryoBuffer::readDouble() {
    int64_t bits = readLong();
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void KryoBuffer::writeBool(bool value) {
    buffer_.push_back(value ? 1 : 0);
}

bool KryoBuffer::readBool() {
    ensureRead(1);
    return buffer_[readPos_++] != 0;
}

void KryoBuffer::writeByte(uint8_t value) {
    buffer_.push_back(value);
}

uint8_t KryoBuffer::readByte() {
    ensureRead(1);
    return buffer_[readPos_++];
}

void KryoBuffer::writeBytes(std::span<const uint8_t> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

std::vector<uint8_t> KryoBuffer::readBytes(size_t count) {
    ensureRead(count);
    std::vector<uint8_t> result(buffer_.begin() + readPos_, 
                                 buffer_.begin() + readPos_ + count);
    readPos_ += count;
    return result;
}

void KryoBuffer::writeString(const std::string& str) {
    // Kryo-compatible string encoding (Output.writeString):
    // - null: 0x80 (handled by writeStringOrNull)
    // - empty: 0x81
    // - non-empty:
    //   - ASCII fast path (not required here), or
    //   - UTF8-length header with high-bit marker + UTF-8 bytes
    if (str.empty()) {
        writeByte(0x81);  // empty string
        return;
    }

    // Write UTF8-length header (Kryo Output.writeUtf8Length, n = charCount + 1).
    // We use byte length as char count; this is correct for ASCII payloads (URIs/JSON),
    // which are the dominant protocol strings.
    uint32_t n = static_cast<uint32_t>(str.size() + 1);
    if ((n >> 6) == 0) {
        writeByte(static_cast<uint8_t>(n | 0x80));
    } else if ((n >> 13) == 0) {
        writeByte(static_cast<uint8_t>(n | 0x40 | 0x80));
        writeByte(static_cast<uint8_t>(n >> 6));
    } else if ((n >> 20) == 0) {
        writeByte(static_cast<uint8_t>(n | 0x40 | 0x80));
        writeByte(static_cast<uint8_t>((n >> 6) | 0x80));
        writeByte(static_cast<uint8_t>(n >> 13));
    } else if ((n >> 27) == 0) {
        writeByte(static_cast<uint8_t>(n | 0x40 | 0x80));
        writeByte(static_cast<uint8_t>((n >> 6) | 0x80));
        writeByte(static_cast<uint8_t>((n >> 13) | 0x80));
        writeByte(static_cast<uint8_t>(n >> 20));
    } else {
        writeByte(static_cast<uint8_t>(n | 0x40 | 0x80));
        writeByte(static_cast<uint8_t>((n >> 6) | 0x80));
        writeByte(static_cast<uint8_t>((n >> 13) | 0x80));
        writeByte(static_cast<uint8_t>((n >> 20) | 0x80));
        writeByte(static_cast<uint8_t>(n >> 27));
    }

    buffer_.insert(buffer_.end(), str.begin(), str.end());
}

void KryoBuffer::writeStringOrNull(const std::string* str) {
    if (str == nullptr) {
        writeByte(0x80);  // null marker in Kryo string encoding
        return;
    }
    writeString(*str);
}

std::string KryoBuffer::readString() {
    ensureRead(1);
    uint8_t first = buffer_[readPos_++];

    // ASCII fast-path (Input.readAscii): first byte had high bit unset.
    if ((first & 0x80) == 0) {
        const size_t start = readPos_ - 1;
        size_t end = readPos_;
        while (true) {
            if (end >= buffer_.size()) {
                throw std::runtime_error("Buffer underflow");
            }
            const uint8_t b = buffer_[end++];
            if ((b & 0x80) != 0) {
                break;
            }
        }

        std::string result(buffer_.begin() + start, buffer_.begin() + end);
        result.back() = static_cast<char>(static_cast<uint8_t>(result.back()) & 0x7F);
        readPos_ = end;
        return result;
    }

    // UTF8-length header (Input.readUtf8Length*).
    uint32_t length = first & 0x3F;
    if ((first & 0x40) != 0) {
        ensureRead(1);
        uint8_t b = buffer_[readPos_++];
        length |= static_cast<uint32_t>(b & 0x7F) << 6;
        if (b & 0x80) {
            ensureRead(1);
            b = buffer_[readPos_++];
            length |= static_cast<uint32_t>(b & 0x7F) << 13;
            if (b & 0x80) {
                ensureRead(1);
                b = buffer_[readPos_++];
                length |= static_cast<uint32_t>(b & 0x7F) << 20;
                if (b & 0x80) {
                    ensureRead(1);
                    b = buffer_[readPos_++];
                    length |= static_cast<uint32_t>(b & 0x7F) << 27;
                }
            }
        }
    }

    if (length == 0) {
        throw std::runtime_error("Unexpected null string");
    }
    if (length == 1) {
        return "";
    }

    const uint32_t charCount = length - 1;
    std::string result;
    result.reserve(charCount);

    // Decode UTF-8 payload while preserving bytes as UTF-8 string.
    for (uint32_t i = 0; i < charCount; ++i) {
        ensureRead(1);
        const uint8_t b0 = buffer_[readPos_++];
        result.push_back(static_cast<char>(b0));

        if ((b0 & 0x80) == 0) {
            continue;
        }

        if ((b0 & 0xE0) == 0xC0) {
            ensureRead(1);
            result.push_back(static_cast<char>(buffer_[readPos_++]));
        } else if ((b0 & 0xF0) == 0xE0) {
            ensureRead(2);
            result.push_back(static_cast<char>(buffer_[readPos_++]));
            result.push_back(static_cast<char>(buffer_[readPos_++]));
        } else if ((b0 & 0xF8) == 0xF0) {
            ensureRead(3);
            result.push_back(static_cast<char>(buffer_[readPos_++]));
            result.push_back(static_cast<char>(buffer_[readPos_++]));
            result.push_back(static_cast<char>(buffer_[readPos_++]));
        } else {
            throw std::runtime_error("Invalid UTF-8 lead byte");
        }
    }

    return result;
}

std::string KryoBuffer::readStringOrNull(bool& isNull) {
    ensureRead(1);
    uint8_t first = buffer_[readPos_++];

    // ASCII fast-path (non-null, non-empty).
    if ((first & 0x80) == 0) {
        isNull = false;
        const size_t start = readPos_ - 1;
        size_t end = readPos_;
        while (true) {
            if (end >= buffer_.size()) {
                throw std::runtime_error("Buffer underflow");
            }
            const uint8_t b = buffer_[end++];
            if ((b & 0x80) != 0) {
                break;
            }
        }

        std::string result(buffer_.begin() + start, buffer_.begin() + end);
        result.back() = static_cast<char>(static_cast<uint8_t>(result.back()) & 0x7F);
        readPos_ = end;
        return result;
    }

    // UTF8-length header.
    uint32_t length = first & 0x3F;
    if ((first & 0x40) != 0) {
        ensureRead(1);
        uint8_t b = buffer_[readPos_++];
        length |= static_cast<uint32_t>(b & 0x7F) << 6;
        if (b & 0x80) {
            ensureRead(1);
            b = buffer_[readPos_++];
            length |= static_cast<uint32_t>(b & 0x7F) << 13;
            if (b & 0x80) {
                ensureRead(1);
                b = buffer_[readPos_++];
                length |= static_cast<uint32_t>(b & 0x7F) << 20;
                if (b & 0x80) {
                    ensureRead(1);
                    b = buffer_[readPos_++];
                    length |= static_cast<uint32_t>(b & 0x7F) << 27;
                }
            }
        }
    }

    if (length == 0) {
        isNull = true;
        return "";
    }

    isNull = false;
    if (length == 1) {
        return "";
    }

    const uint32_t charCount = length - 1;
    std::string result;
    result.reserve(charCount);

    for (uint32_t i = 0; i < charCount; ++i) {
        ensureRead(1);
        const uint8_t b0 = buffer_[readPos_++];
        result.push_back(static_cast<char>(b0));

        if ((b0 & 0x80) == 0) {
            continue;
        }

        if ((b0 & 0xE0) == 0xC0) {
            ensureRead(1);
            result.push_back(static_cast<char>(buffer_[readPos_++]));
        } else if ((b0 & 0xF0) == 0xE0) {
            ensureRead(2);
            result.push_back(static_cast<char>(buffer_[readPos_++]));
            result.push_back(static_cast<char>(buffer_[readPos_++]));
        } else if ((b0 & 0xF8) == 0xF0) {
            ensureRead(3);
            result.push_back(static_cast<char>(buffer_[readPos_++]));
            result.push_back(static_cast<char>(buffer_[readPos_++]));
            result.push_back(static_cast<char>(buffer_[readPos_++]));
        } else {
            throw std::runtime_error("Invalid UTF-8 lead byte");
        }
    }

    return result;
}

// Frame codec

std::vector<uint8_t> KryoFrameCodec::encode(const KryoBuffer& packet) {
    const auto& data = packet.data();
    std::vector<uint8_t> frame;
    frame.reserve(4 + data.size());
    
    // Big-endian 4-byte length
    uint32_t length = static_cast<uint32_t>(data.size());
    frame.push_back(static_cast<uint8_t>(length >> 24));
    frame.push_back(static_cast<uint8_t>(length >> 16));
    frame.push_back(static_cast<uint8_t>(length >> 8));
    frame.push_back(static_cast<uint8_t>(length));
    
    frame.insert(frame.end(), data.begin(), data.end());
    return frame;
}

std::vector<std::vector<uint8_t>> KryoFrameCodec::decode(std::span<const uint8_t> data) {
    std::vector<std::vector<uint8_t>> packets;
    
    // Append to partial buffer
    partialBuffer_.insert(partialBuffer_.end(), data.begin(), data.end());
    
    size_t pos = 0;
    while (pos + 4 <= partialBuffer_.size()) {
        // Read length (big-endian)
        uint32_t length = (static_cast<uint32_t>(partialBuffer_[pos]) << 24) |
                          (static_cast<uint32_t>(partialBuffer_[pos + 1]) << 16) |
                          (static_cast<uint32_t>(partialBuffer_[pos + 2]) << 8) |
                          static_cast<uint32_t>(partialBuffer_[pos + 3]);
        
        // Check if we have the complete packet
        if (pos + 4 + length > partialBuffer_.size()) {
            break;  // Incomplete packet
        }
        
        // Extract packet data
        packets.emplace_back(
            partialBuffer_.begin() + pos + 4,
            partialBuffer_.begin() + pos + 4 + length
        );
        
        pos += 4 + length;
    }
    
    // Remove processed data
    if (pos > 0) {
        partialBuffer_.erase(partialBuffer_.begin(), partialBuffer_.begin() + pos);
    }
    
    return packets;
}

} // namespace dynamo
