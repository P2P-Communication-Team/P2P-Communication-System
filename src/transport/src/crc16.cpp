#include "crc16.hpp"

namespace p2p {
namespace crc16 {

namespace {

// 256-entry lookup table: one table lookup replaces 8 shift/xor steps per byte.
struct CrcTable {
    uint16_t entries[256];
    CrcTable() {
        for (unsigned i = 0; i < 256; i++) {
            uint16_t value = static_cast<uint16_t>(i << 8);
            for (int bit = 0; bit < 8; bit++) {
                if (value & 0x8000) value = static_cast<uint16_t>((value << 1) ^ POLYNOMIAL);
                else value = static_cast<uint16_t>(value << 1);
            }
            entries[i] = value;
        }
    }
};

const CrcTable& table() {
    static const CrcTable t;   // built once, thread-safe in C++11 and later
    return t;
}

} // namespace

uint16_t update(uint16_t crc, const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return crc;   // nothing to add (empty payload case)
    const CrcTable& t = table();
    for (size_t i = 0; i < length; i++) {
        uint8_t index = static_cast<uint8_t>((crc >> 8) ^ data[i]);
        crc = static_cast<uint16_t>((crc << 8) ^ t.entries[index]);
    }
    return crc;
}

uint16_t compute(const uint8_t* data, size_t length) {
    return update(INITIAL_VALUE, data, length);
}

uint16_t compute(const std::vector<uint8_t>& data) {
    return compute(data.data(), data.size());
}

uint16_t computeForDatagram(const uint8_t* datagram, size_t length) {
    // Too short to even contain the CRC field: fall back to a plain CRC (caller rejects it anyway).
    if (datagram == nullptr || length < PACKET_CRC_OFFSET + CRC_FIELD_SIZE) {
        return compute(datagram, length);
    }
    static const uint8_t zeros[CRC_FIELD_SIZE] = {0, 0};
    uint16_t crc = update(INITIAL_VALUE, datagram, PACKET_CRC_OFFSET);               // bytes before CRC
    crc = update(crc, zeros, CRC_FIELD_SIZE);                                         // CRC field as zero
    crc = update(crc, datagram + PACKET_CRC_OFFSET + CRC_FIELD_SIZE,
                 length - PACKET_CRC_OFFSET - CRC_FIELD_SIZE);                        // rest of header + payload
    return crc;
}

bool verifyDatagram(const uint8_t* datagram, size_t length, uint16_t expected) {
    return computeForDatagram(datagram, length) == expected;
}

} // namespace crc16
} // namespace p2p
