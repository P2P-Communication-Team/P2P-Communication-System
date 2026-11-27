// crc16.hpp -- CRC-16/CCITT-FALSE
//
// Parameters: polynomial 0x1021, initial value 0xFFFF, no input/output reflection, xor-out 0x0000.
// Known test vector: crc16("123456789") == 0x29B1.
//
// IMPORTANT: a CRC only detects ACCIDENTAL corruption (bit flips on the wire). It is NOT a
// signature: anybody can recompute it, so it gives no protection against a malicious sender.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p2p {
namespace crc16 {

constexpr uint16_t INITIAL_VALUE = 0xFFFF;
constexpr uint16_t POLYNOMIAL = 0x1021;

// Byte offset of the 2-byte CRC field inside a datagram (see protocol_spec.md, header layout).
constexpr size_t PACKET_CRC_OFFSET = 6;
constexpr size_t CRC_FIELD_SIZE = 2;

// Continue a running CRC with more bytes (data may be null only when length == 0).
uint16_t update(uint16_t crc, const uint8_t* data, size_t length);

// CRC of a whole buffer. An empty buffer returns INITIAL_VALUE (0xFFFF).
uint16_t compute(const uint8_t* data, size_t length);
uint16_t compute(const std::vector<uint8_t>& data);

// CRC of a complete datagram (header + payload) where the 2 CRC bytes inside the header are
// treated as ZERO. The sender computes it this way before writing the CRC into the header, and
// the receiver recomputes it the same way to compare. No copy of the buffer is needed.
uint16_t computeForDatagram(const uint8_t* datagram, size_t length);

// true if `expected` equals computeForDatagram(datagram, length).
bool verifyDatagram(const uint8_t* datagram, size_t length, uint16_t expected);

} // namespace crc16
} // namespace p2p
