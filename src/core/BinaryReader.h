#pragma once
/*
    BinaryReader

    Responsibility:
    - Provides low-level binary reading
*/
#include <vector>
#include <cstdint>
#include <stdexcept>

class BinaryReader
{
private:
    std::vector<uint8_t> m_byteBuffer; // The binary data buffer
    size_t m_readPosition; // Current cursor position

    //helper: check we won't read past the end of the buffer
    void checkBounds(size_t bytesNeeded) const {
        if (m_readPosition + bytesNeeded > m_byteBuffer.size()) {
            throw std::out_of_range("BinaryReader: read past end of buffer");
        }
    }

public:
    size_t savePosition() const {
        return m_readPosition;
    }

    // restore cursor to a previous mark
    void restorePosition(size_t markPosition) {
        if (markPosition > m_byteBuffer.size()) {
            throw std::out_of_range("BinaryReader: restore past end of buffer");
        }
        m_readPosition = markPosition;
    }

    // remembers the position of the cursor
    size_t position() const {
        return m_readPosition;
    }

    // lets you skip ahead
    void skip(size_t n) {
        checkBounds(n);
        m_readPosition += n;
    }

    // Constructor: takes the buffer and stores it
    BinaryReader(const std::vector<uint8_t>& data)
        : m_byteBuffer(data), m_readPosition(0) 
    {}

    // How many bytes are left to read
    size_t remaining() const {
        return m_byteBuffer.size() - m_readPosition;
    }

    // move the cursor to a specific position
    void seek(size_t position) {
        if (position> m_byteBuffer.size()) {
            throw std::out_of_range("BinaryReader: seek past end of buffer");
        }
        m_readPosition = position;
    }

    // lets you peek at the next byte without advancing the cursor
    uint8_t peekU8() const {
        checkBounds(1);
        return m_byteBuffer[m_readPosition];
    }

    // read a single byte
    uint8_t readU8() {
        checkBounds(1);
        return m_byteBuffer[m_readPosition++]; 
    }

    //lets you peek at the next 2 bytes without advancing the cursor
    uint16_t peekU16() const {
        checkBounds(2);
        return (m_byteBuffer[m_readPosition + 1] << 8)
             | m_byteBuffer[m_readPosition];
    }

    // read a uint16 (2 bytes Little Endian)
    uint16_t readU16() {
        checkBounds(2);
        uint16_t value = (m_byteBuffer[m_readPosition + 1] << 8)
                       | m_byteBuffer[m_readPosition];

        m_readPosition += 2;
        return value;
    }

    // lets you peek at the next 4 bytes without advancing the cursor
    uint32_t peekU32() const {
        checkBounds(4);
        return (m_byteBuffer[m_readPosition + 3] << 24)
             | (m_byteBuffer[m_readPosition + 2] << 16)
             | (m_byteBuffer[m_readPosition + 1] << 8)
             | m_byteBuffer[m_readPosition];
    }
    
    // read a uint32 (4 bytes Little Endian)
    uint32_t readU32() {
        checkBounds(4);
        uint32_t value = (m_byteBuffer[m_readPosition + 3] << 24)
                       | (m_byteBuffer[m_readPosition + 2] << 16)
                       | (m_byteBuffer[m_readPosition + 1] << 8)
                       | m_byteBuffer[m_readPosition];

        m_readPosition += 4;
        return value;
    }

    // lets you peek at the next 8 bytes without advancing the cursor
    uint64_t peekU64() const {
        checkBounds(8);
        return (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 7]) << 56) 
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 6]) << 48) 
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 5]) << 40) 
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 4]) << 32)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 3]) << 24)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 2]) << 16)
             | (static_cast<uint64_t>(m_byteBuffer[m_readPosition + 1]) << 8)
             | static_cast<uint64_t>(m_byteBuffer[m_readPosition]);
    }

    // read a uint64 (8 bytes Little Endian)
    uint64_t readU64() {
        checkBounds(8);
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

    // lets you peek at the next n bytes without advancing the cursor
    std::vector<uint8_t> peekBytes(size_t n) const {
        checkBounds(n);
        auto start = m_byteBuffer.begin() + m_readPosition;
        return std::vector(start, start + n);
    }

    // read n raw bytes
    std::vector<uint8_t> readBytes(size_t n) {
        checkBounds(n);
        auto start = m_byteBuffer.begin() + m_readPosition;
        m_readPosition += n;
        return std::vector(start, start + n);
    }
};