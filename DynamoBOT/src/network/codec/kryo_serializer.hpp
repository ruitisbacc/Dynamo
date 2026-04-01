#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <stdexcept>
#include <concepts>
#include <type_traits>

namespace dynamo {

// Kryo-compatible binary serializer
// Implements variable-length integer encoding (varint) and other Kryo conventions

class KryoBuffer {
public:
    KryoBuffer() = default;
    explicit KryoBuffer(size_t capacity) { buffer_.reserve(capacity); }
    explicit KryoBuffer(std::vector<uint8_t> data) : buffer_(std::move(data)) {}
    
    // Writing
    void writeInt(int32_t value);
    void writeVarInt(int32_t value, bool optimizePositive = true);
    void writeLong(int64_t value);
    void writeVarLong(int64_t value, bool optimizePositive = true);
    void writeKryoInt(int32_t value) { writeVarInt(value, false); }
    void writeKryoLong(int64_t value) { writeVarLong(value, false); }
    void writeFloat(float value);
    void writeDouble(double value);
    void writeBool(bool value);
    void writeByte(uint8_t value);
    void writeBytes(std::span<const uint8_t> data);
    void writeString(const std::string& str);
    void writeStringOrNull(const std::string* str);
    
    // Reading
    int32_t readInt();
    int32_t readVarInt(bool optimizePositive = true);
    int64_t readLong();
    int64_t readVarLong(bool optimizePositive = true);
    int32_t readKryoInt() { return readVarInt(false); }
    int64_t readKryoLong() { return readVarLong(false); }
    float readFloat();
    double readDouble();
    bool readBool();
    uint8_t readByte();
    std::vector<uint8_t> readBytes(size_t count);
    std::string readString();
    std::string readStringOrNull(bool& isNull);
    
    // Buffer access
    const std::vector<uint8_t>& data() const { return buffer_; }
    std::vector<uint8_t>& data() { return buffer_; }
    size_t size() const { return buffer_.size(); }
    size_t position() const { return readPos_; }
    void setPosition(size_t pos) { readPos_ = pos; }
    void reset() { readPos_ = 0; }
    void clear() { buffer_.clear(); readPos_ = 0; }
    bool eof() const { return readPos_ >= buffer_.size(); }
    size_t remaining() const { return buffer_.size() - readPos_; }
    
private:
    std::vector<uint8_t> buffer_;
    size_t readPos_{0};
    
    void ensureRead(size_t count) const {
        if (readPos_ + count > buffer_.size()) {
            throw std::runtime_error("Buffer underflow");
        }
    }
};

// KryoNet frame format: [4-byte length][payload]
class KryoFrameCodec {
public:
    // Encode a packet into a frame (adds length prefix)
    static std::vector<uint8_t> encode(const KryoBuffer& packet);
    
    // Decode frames from stream, returns complete packets
    // Partial data is kept in internal buffer
    std::vector<std::vector<uint8_t>> decode(std::span<const uint8_t> data);
    
    // Check if we have incomplete data
    bool hasPartial() const { return !partialBuffer_.empty(); }
    
    void reset() { partialBuffer_.clear(); }
    
private:
    std::vector<uint8_t> partialBuffer_;
};

// Base class for serializable packets
class ISerializable {
public:
    virtual ~ISerializable() = default;
    virtual void serialize(KryoBuffer& buffer) const = 0;
    virtual void deserialize(KryoBuffer& buffer) = 0;
};

} // namespace dynamo
