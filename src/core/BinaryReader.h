#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>

class BinaryReader
{
private:
    std::vector<uint8_t> m_byteBuffer;
    size_t m_readPosition{0};

    void requireBytes(size_t bytesNeeded) const {
        if (m_readPosition + bytesNeeded > m_byteBuffer.size()) {
            throw std::out_of_range("BinaryReader: read past end of buffer");
        }
    }

public:
    size_t savePosition() const {
        return m_readPosition;
    }

    void restorePosition(size_t markPosition) {
        if (markPosition > m_byteBuffer.size()) {
            throw std::out_of_range("BinaryReader: restore past end of buffer");
        }
        m_readPosition = markPosition;
    }

    size_t position() const {
        return m_readPosition;
    }

    void skip(size_t n) {
        requireBytes(n);
        m_readPosition += n;
    }

    explicit BinaryReader(const std::vector<uint8_t>& data)
        : m_byteBuffer(data)
    {}

    size_t remaining() const {
        return m_byteBuffer.size() - m_readPosition;
    }

    void seek(size_t position) {
        if (position> m_byteBuffer.size()) {
            throw std::out_of_range("BinaryReader: seek past end of buffer");
        }
        m_readPosition = position;
    }

    uint8_t peekU8() const {
        requireBytes(1);
        return m_byteBuffer[m_readPosition];
    }

    uint8_t readU8() {
        requireBytes(1);
        return m_byteBuffer[m_readPosition++];
    }

    uint16_t peekU16() const {
        requireBytes(2);
        return (m_byteBuffer[m_readPosition + 1] << 8)
             | m_byteBuffer[m_readPosition];
    }

    uint16_t readU16() {
        requireBytes(2);
        uint16_t value = (m_byteBuffer[m_readPosition + 1] << 8)
                       | m_byteBuffer[m_readPosition];

        m_readPosition += 2;
        return value;
    }

    uint32_t peekU32() const {
        requireBytes(4);
        return (m_byteBuffer[m_readPosition + 3] << 24)
             | (m_byteBuffer[m_readPosition + 2] << 16)
             | (m_byteBuffer[m_readPosition + 1] << 8)
             | m_byteBuffer[m_readPosition];
    }

    uint32_t readU32() {
        requireBytes(4);
        uint32_t value = (m_byteBuffer[m_readPosition + 3] << 24)
                       | (m_byteBuffer[m_readPosition + 2] << 16)
                       | (m_byteBuffer[m_readPosition + 1] << 8)
                       | m_byteBuffer[m_readPosition];

        m_readPosition += 4;
        return value;
    }

    uint64_t peekU64() const {
        requireBytes(8);
        return (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 7]) << 56)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 6]) << 48)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 5]) << 40)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 4]) << 32)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 3]) << 24)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 2]) << 16)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 1]) << 8)
             | static_cast<uint64_t>(m_byteBuffer[m_readPosition]);
    }

    uint64_t readU64() {
        requireBytes(8);
        uint64_t value = (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 7]) << 56)
                       | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 6]) << 48)
                       | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 5]) << 40)
                       | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 4]) << 32)
                       | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 3]) << 24)
                       | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 2]) << 16)
                       | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 1]) << 8)
                       | static_cast<uint64_t>(m_byteBuffer[m_readPosition]);
        m_readPosition += 8;
        return value;
    }

    std::vector<uint8_t> peekBytes(size_t n) const {
        requireBytes(n);
        auto start = m_byteBuffer.begin() + m_readPosition;
        return std::vector(start, start + n);
    }

    std::vector<uint8_t> readBytes(size_t n) {
        requireBytes(n);
        auto start = m_byteBuffer.begin() + m_readPosition;
        m_readPosition += n;
        return std::vector(start, start + n);
    }
};