// protocol.hpp -- THE SHARED WIRE PROTOCOL (all 4 members include this file; see protocol_spec.md)
//
// 16-byte common header, every multi-byte field BIG-ENDIAN (network byte order):
//
//   byte  0      : [ version : 2 bits | flags : 6 bits ]   version = top 2 bits, flags = low 6 bits
//   byte  1      : msg_type
//   bytes 2..3   : stream_id
//   bytes 4..5   : payload_len
//   bytes 6..7   : crc16  (CRC-16/CCITT-FALSE over the whole datagram with these 2 bytes = 0)
//   bytes 8..11  : seq_num
//   bytes 12..15 : ack_num   (only meaningful in DATA_ACK; "highest consecutively received seq")
//
// Rules for everybody:
//   * NEVER memcpy a C++ struct to/from the network (padding, endianness and bit-field layout
//     are compiler dependent). Use encodePacket()/decodePacket() and the payload encode()/decode().
//   * ALWAYS check the return value of decodePacket() before touching the packet.
#pragma once

#include "crc16.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace p2p {

// ============================================================================ constants
constexpr uint8_t  PROTOCOL_VERSION = 1;                 // 2-bit field, legal values 0..3
constexpr unsigned VERSION_BITS = 2;
constexpr unsigned FLAG_BITS = 6;
constexpr uint8_t  FLAG_MASK = 0x3F;                     // low 6 bits of byte 0
constexpr unsigned VERSION_SHIFT = FLAG_BITS;            // version lives in bits 7..6

constexpr size_t HEADER_SIZE = 16;
constexpr size_t MAX_CHUNK_DATA_SIZE = 1400;             // file bytes inside one CHUNK_DATA
constexpr size_t CHUNK_META_SIZE = 8;                    // file_id (4) + chunk_index (4)
constexpr size_t MAX_PAYLOAD_SIZE = CHUNK_META_SIZE + MAX_CHUNK_DATA_SIZE;   // 1408
constexpr size_t MAX_DATAGRAM_SIZE = HEADER_SIZE + MAX_PAYLOAD_SIZE;         // 1424
constexpr size_t DEFAULT_CHUNK_SIZE = 1024;              // recommended file chunk (safe on any MTU >= 1280)
constexpr size_t SACK_PAYLOAD_SIZE = 4;
constexpr size_t MEMBER_INFO_SIZE = 9;                   // peer_id 2 + port 2 + status 1 + incarnation 4
constexpr size_t PEER_ENTRY_SIZE = 8;                    // peer_id 2 + ipv4 4 + port 2
constexpr size_t MAX_PEERS_PER_PEER_LIST = (MAX_PAYLOAD_SIZE - 2) / PEER_ENTRY_SIZE;   // 175
constexpr size_t SHA256_SIZE = 32;
constexpr size_t FILE_MANIFEST_MIN_SIZE = 4 + 8 + 2 + 2 + SHA256_SIZE;   // without the file name

constexpr uint32_t WINDOW_SIZE = 32;                     // Selective Repeat window == ring capacity == SACK width

constexpr uint16_t PEER_ID_NONE = 0x0000;
constexpr uint16_t PEER_ID_BROADCAST = 0xFFFF;
constexpr uint16_t DEFAULT_UDP_PORT = 9000;

// ============================================================================ flags (6 bits)
// Byte 0 low 6 bits. Allocation is FINAL: change it only with the whole group's agreement.
constexpr uint8_t F_RELIABLE = 0x20;   // seq_num valid, receiver must ACK, goes through Selective Repeat
constexpr uint8_t F_RETX     = 0x10;   // this datagram is a retransmission (set by the engine only)
constexpr uint8_t F_FIN      = 0x08;   // last packet of a message/stream (free for application use)
constexpr uint8_t F_PRIV     = 0x04;   // private one-to-one chat message
constexpr uint8_t F_ENC      = 0x02;   // payload encrypted (bonus feature)
constexpr uint8_t F_RSV      = 0x01;   // reserved: MUST be 0; a packet with it set is rejected

// ============================================================================ streams
constexpr uint16_t STREAM_CONTROL = 0;      // unreliable control traffic only (never a reliable stream)
constexpr uint16_t STREAM_CHAT = 1;         // reliable, ordered chat
constexpr uint16_t STREAM_FILE_META = 2;    // reliable: manifests, chunk maps, requests
constexpr uint16_t STREAM_FILE_BASE = 16;   // >= 16: one stream per file-transfer session (never reuse ids)

// ============================================================================ message types
enum MsgType : uint8_t {
    DISCOVER      = 1,
    JOIN          = 2,
    JOIN_ACK      = 3,
    LEAVE         = 4,
    PEER_LIST     = 5,
    HEARTBEAT     = 6,
    CHAT_MSG      = 7,
    DATA_ACK      = 8,
    FILE_LIST_REQ = 9,
    FILE_MANIFEST = 10,
    CHUNK_HAVE    = 11,
    CHUNK_REQ     = 12,
    CHUNK_DATA    = 13,
    CHUNK_STORE   = 14,
    CHUNK_NAK     = 15
};
constexpr uint8_t MSG_TYPE_MAX = 15;

inline bool isValidMsgType(uint8_t type) { return type >= DISCOVER && type <= MSG_TYPE_MAX; }

inline const char* msgTypeName(uint8_t type) {
    static const char* names[] = {"UNKNOWN", "DISCOVER", "JOIN", "JOIN_ACK", "LEAVE", "PEER_LIST",
                                  "HEARTBEAT", "CHAT_MSG", "DATA_ACK", "FILE_LIST_REQ",
                                  "FILE_MANIFEST", "CHUNK_HAVE", "CHUNK_REQ", "CHUNK_DATA",
                                  "CHUNK_STORE", "CHUNK_NAK"};
    return isValidMsgType(type) ? names[type] : names[0];
}

// Which message types may / must use the reliable (Selective Repeat) channel.
enum class DeliveryMode { UNRELIABLE_ONLY, RELIABLE_ONLY, EITHER };

inline DeliveryMode deliveryModeOf(uint8_t type) {
    switch (type) {
        case DISCOVER: case JOIN: case JOIN_ACK: case LEAVE: case HEARTBEAT: case DATA_ACK:
            return DeliveryMode::UNRELIABLE_ONLY;   // retried by their own module (or periodic) / ACKs are never ACKed
        case PEER_LIST:
            return DeliveryMode::EITHER;            // membership module decides
        default:
            return DeliveryMode::RELIABLE_ONLY;     // CHAT_MSG, FILE_*, CHUNK_*
    }
}

// Exact payload-size rule per message type (checked on every received packet).
inline bool isPayloadSizeValid(uint8_t type, size_t length) {
    if (length > MAX_PAYLOAD_SIZE) return false;
    switch (type) {
        case DISCOVER: case JOIN: case JOIN_ACK: case LEAVE: return length == MEMBER_INFO_SIZE;
        case PEER_LIST:     return length >= 2 && (length - 2) % PEER_ENTRY_SIZE == 0;
        case HEARTBEAT:     return length == 0;
        case FILE_LIST_REQ: return length == 0;
        case CHAT_MSG:      return length >= 1;
        case DATA_ACK:      return length == SACK_PAYLOAD_SIZE;
        case FILE_MANIFEST: return length >= FILE_MANIFEST_MIN_SIZE;
        case CHUNK_HAVE:    return length >= CHUNK_META_SIZE + 1;
        case CHUNK_REQ: case CHUNK_STORE: return length == CHUNK_META_SIZE;
        case CHUNK_DATA:    return length >= CHUNK_META_SIZE + 1;
        case CHUNK_NAK:     return length == CHUNK_META_SIZE + 1;
        default:            return false;
    }
}

// ============================================================================ byte writer / reader
// Explicit big-endian serialisation. The reader is bounds-checked: after any over-read ok() is
// false and every further read returns 0, so malformed input can never read out of bounds.
class ByteWriter {
public:
    std::vector<uint8_t> buffer;
    void u8(uint8_t v) { buffer.push_back(v); }
    void u16(uint16_t v) { buffer.push_back(static_cast<uint8_t>(v >> 8)); buffer.push_back(static_cast<uint8_t>(v & 0xFF)); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v >> 16)); u16(static_cast<uint16_t>(v & 0xFFFF)); }
    void u64(uint64_t v) { u32(static_cast<uint32_t>(v >> 32)); u32(static_cast<uint32_t>(v & 0xFFFFFFFFu)); }
    void bytes(const uint8_t* data, size_t length) {
        if (data != nullptr && length > 0) buffer.insert(buffer.end(), data, data + length);
    }
    void text(const std::string& s) { bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t length) : data_(data), length_(data ? length : 0) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : data_(v.data()), length_(v.size()) {}
    uint8_t u8() { return need(1) ? data_[pos_++] : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = static_cast<uint16_t>((static_cast<uint16_t>(data_[pos_]) << 8) | data_[pos_ + 1]);
        pos_ += 2;
        return v;
    }
    uint32_t u32() { uint32_t hi = u16(); uint32_t lo = u16(); return (hi << 16) | lo; }
    uint64_t u64() { uint64_t hi = u32(); uint64_t lo = u32(); return (hi << 32) | lo; }
    std::vector<uint8_t> bytes(size_t count) {
        if (!need(count)) return std::vector<uint8_t>();
        std::vector<uint8_t> v(data_ + pos_, data_ + pos_ + count);
        pos_ += count;
        return v;
    }
    std::string text(size_t count) { std::vector<uint8_t> v = bytes(count); return std::string(v.begin(), v.end()); }
    std::vector<uint8_t> rest() { return bytes(length_ - pos_); }
    size_t remaining() const { return length_ - pos_; }
    bool ok() const { return ok_; }
private:
    bool need(size_t count) {
        if (!ok_ || length_ - pos_ < count) { ok_ = false; return false; }
        return true;
    }
    const uint8_t* data_;
    size_t length_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// ============================================================================ header & packet
struct Header {
    uint8_t  version = PROTOCOL_VERSION;
    uint8_t  flags = 0;            // only the low 6 bits are used
    uint8_t  msg_type = 0;
    uint16_t stream_id = 0;
    uint16_t payload_length = 0;
    uint16_t crc = 0;
    uint32_t seq = 0;              // valid only when F_RELIABLE is set
    uint32_t ack = 0;              // valid only in DATA_ACK (highest consecutively received seq)
};

struct Packet {
    Header header;
    std::vector<uint8_t> payload;
    bool isReliable() const { return (header.flags & F_RELIABLE) != 0; }
};

enum class DecodeStatus {
    OK,
    EMPTY_INPUT,           // null pointer or length 0
    TOO_SHORT,             // shorter than the 16-byte header
    UNSUPPORTED_VERSION,
    PAYLOAD_TOO_LARGE,     // payload_len > MAX_PAYLOAD_SIZE
    LENGTH_MISMATCH,       // datagram size != 16 + payload_len (missing OR extra trailing bytes)
    CRC_MISMATCH,
    UNKNOWN_MSG_TYPE,
    INVALID_FLAGS,
    INVALID_STREAM,
    INVALID_PAYLOAD_SIZE
};

inline const char* decodeStatusName(DecodeStatus s) {
    switch (s) {
        case DecodeStatus::OK: return "OK";
        case DecodeStatus::EMPTY_INPUT: return "EMPTY_INPUT";
        case DecodeStatus::TOO_SHORT: return "TOO_SHORT";
        case DecodeStatus::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case DecodeStatus::PAYLOAD_TOO_LARGE: return "PAYLOAD_TOO_LARGE";
        case DecodeStatus::LENGTH_MISMATCH: return "LENGTH_MISMATCH";
        case DecodeStatus::CRC_MISMATCH: return "CRC_MISMATCH";
        case DecodeStatus::UNKNOWN_MSG_TYPE: return "UNKNOWN_MSG_TYPE";
        case DecodeStatus::INVALID_FLAGS: return "INVALID_FLAGS";
        case DecodeStatus::INVALID_STREAM: return "INVALID_STREAM";
        case DecodeStatus::INVALID_PAYLOAD_SIZE: return "INVALID_PAYLOAD_SIZE";
    }
    return "?";
}

// Serialise header + payload into `out` (CRC filled in). Returns false (out untouched) if the
// payload is too large, `payload` is null while payload_length > 0, or flags use more than 6 bits.
// NOTE: it deliberately does NOT validate message semantics, so tests can build odd packets.
inline bool encodePacket(uint8_t msg_type, uint8_t flags, uint16_t stream_id, uint32_t seq, uint32_t ack,
                         const uint8_t* payload, size_t payload_length, std::vector<uint8_t>& out) {
    if (payload_length > MAX_PAYLOAD_SIZE) return false;
    if (payload == nullptr && payload_length > 0) return false;
    if ((flags & ~FLAG_MASK) != 0) return false;

    ByteWriter w;
    w.buffer.reserve(HEADER_SIZE + payload_length);
    w.u8(static_cast<uint8_t>(((PROTOCOL_VERSION & 0x03) << VERSION_SHIFT) | (flags & FLAG_MASK)));
    w.u8(msg_type);
    w.u16(stream_id);
    w.u16(static_cast<uint16_t>(payload_length));
    w.u16(0);                                   // CRC placeholder, bytes 6..7
    w.u32(seq);
    w.u32(ack);
    w.bytes(payload, payload_length);

    uint16_t crc = crc16::computeForDatagram(w.buffer.data(), w.buffer.size());
    w.buffer[crc16::PACKET_CRC_OFFSET] = static_cast<uint8_t>(crc >> 8);
    w.buffer[crc16::PACKET_CRC_OFFSET + 1] = static_cast<uint8_t>(crc & 0xFF);
    out = std::move(w.buffer);
    return true;
}

inline bool encodePacket(uint8_t msg_type, uint8_t flags, uint16_t stream_id, uint32_t seq, uint32_t ack,
                         const std::vector<uint8_t>& payload, std::vector<uint8_t>& out) {
    return encodePacket(msg_type, flags, stream_id, seq, ack, payload.data(), payload.size(), out);
}

// Check order (cheap -> expensive, structural -> semantic):
//  empty -> header size -> version -> payload_len limit -> exact datagram size -> CRC ->
//  msg_type -> flags -> stream id -> payload size for this type.
// The output packet is only written when the result is OK.
inline DecodeStatus decodePacket(const uint8_t* data, size_t length, Packet& out) {
    if (data == nullptr || length == 0) return DecodeStatus::EMPTY_INPUT;
    if (length < HEADER_SIZE) return DecodeStatus::TOO_SHORT;

    ByteReader r(data, HEADER_SIZE);
    uint8_t first = r.u8();
    Header h;
    h.version = static_cast<uint8_t>(first >> VERSION_SHIFT);
    h.flags = static_cast<uint8_t>(first & FLAG_MASK);
    h.msg_type = r.u8();
    h.stream_id = r.u16();
    h.payload_length = r.u16();
    h.crc = r.u16();
    h.seq = r.u32();
    h.ack = r.u32();

    if (h.version != PROTOCOL_VERSION) return DecodeStatus::UNSUPPORTED_VERSION;
    if (h.payload_length > MAX_PAYLOAD_SIZE) return DecodeStatus::PAYLOAD_TOO_LARGE;
    // UDP keeps datagram boundaries, so the size must match EXACTLY. Extra trailing bytes are not
    // covered by the CRC, so accepting them would let unprotected garbage through.
    if (length != HEADER_SIZE + static_cast<size_t>(h.payload_length)) return DecodeStatus::LENGTH_MISMATCH;
    if (!crc16::verifyDatagram(data, length, h.crc)) return DecodeStatus::CRC_MISMATCH;

    if (!isValidMsgType(h.msg_type)) return DecodeStatus::UNKNOWN_MSG_TYPE;

    const bool reliable = (h.flags & F_RELIABLE) != 0;
    const bool retransmission = (h.flags & F_RETX) != 0;
    const DeliveryMode mode = deliveryModeOf(h.msg_type);
    if ((h.flags & F_RSV) != 0) return DecodeStatus::INVALID_FLAGS;
    if (retransmission && !reliable) return DecodeStatus::INVALID_FLAGS;
    if (mode == DeliveryMode::UNRELIABLE_ONLY && reliable) return DecodeStatus::INVALID_FLAGS;
    if (mode == DeliveryMode::RELIABLE_ONLY && !reliable) return DecodeStatus::INVALID_FLAGS;

    // Stream 0 is control-only. Reliable data and ACKs always belong to a real reliable stream.
    if ((reliable || h.msg_type == DATA_ACK) && h.stream_id == STREAM_CONTROL) return DecodeStatus::INVALID_STREAM;

    if (!isPayloadSizeValid(h.msg_type, h.payload_length)) return DecodeStatus::INVALID_PAYLOAD_SIZE;

    out.header = h;
    out.payload.assign(data + HEADER_SIZE, data + length);
    return DecodeStatus::OK;
}

inline DecodeStatus decodePacket(const std::vector<uint8_t>& datagram, Packet& out) {
    return decodePacket(datagram.data(), datagram.size(), out);
}

// ============================================================================ message payloads
// Every payload type has   std::vector<uint8_t> encode(const T&)   and   bool decode(const vector&, T&).
// decode() returns false for any malformed payload and never reads out of bounds.

// ---- DISCOVER / JOIN / JOIN_ACK / LEAVE : fixed 9 bytes ------------------------------------------
// peer_id(2) udp_port(2) status(1) incarnation(4)
// incarnation = random number chosen once per process start. The membership module uses it to
// tell "same peer again" from "peer restarted" and then calls ReliabilityEngine::resetPeerSession().
constexpr uint8_t JOIN_STATUS_OK = 0;
constexpr uint8_t JOIN_STATUS_ID_TAKEN = 1;

struct MemberInfo {
    uint16_t peer_id = PEER_ID_NONE;
    uint16_t udp_port = 0;
    uint8_t status = JOIN_STATUS_OK;
    uint32_t incarnation = 0;
};
inline std::vector<uint8_t> encode(const MemberInfo& m) {
    ByteWriter w; w.u16(m.peer_id); w.u16(m.udp_port); w.u8(m.status); w.u32(m.incarnation);
    return std::move(w.buffer);
}
inline bool decode(const std::vector<uint8_t>& p, MemberInfo& m) {
    if (p.size() != MEMBER_INFO_SIZE) return false;
    ByteReader r(p);
    m.peer_id = r.u16(); m.udp_port = r.u16(); m.status = r.u8(); m.incarnation = r.u32();
    return r.ok();
}

// ---- PEER_LIST : peer_count(2) + N x [peer_id(2) ipv4(4) port(2)]  = 2 + 8N bytes --------------------
struct PeerEntry {
    uint16_t peer_id = 0;
    uint32_t ipv4 = 0;     // HOST byte order in memory (written big-endian on the wire)
    uint16_t port = 0;
};
inline std::vector<uint8_t> encode(const std::vector<PeerEntry>& peers) {
    size_t count = peers.size() > MAX_PEERS_PER_PEER_LIST ? MAX_PEERS_PER_PEER_LIST : peers.size();
    ByteWriter w; w.u16(static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; i++) { w.u16(peers[i].peer_id); w.u32(peers[i].ipv4); w.u16(peers[i].port); }
    return std::move(w.buffer);
}
inline bool decode(const std::vector<uint8_t>& p, std::vector<PeerEntry>& peers) {
    ByteReader r(p);
    uint16_t count = r.u16();
    if (!r.ok() || r.remaining() != static_cast<size_t>(count) * PEER_ENTRY_SIZE) return false;
    peers.clear();
    for (uint16_t i = 0; i < count; i++) {
        PeerEntry e; e.peer_id = r.u16(); e.ipv4 = r.u32(); e.port = r.u16();
        peers.push_back(e);
    }
    return r.ok();
}

// ---- FILE_MANIFEST : file_id(4) file_size(8) chunk_size(2) name_len(2) name sha256(32) ------------------
struct FileManifest {
    uint32_t file_id = 0;
    uint64_t file_size = 0;
    uint16_t chunk_size = static_cast<uint16_t>(DEFAULT_CHUNK_SIZE);
    std::string name;
    uint8_t sha256[SHA256_SIZE] = {0};    // whole-file integrity hash, verified after download

    uint32_t chunkCount() const {
        return chunk_size == 0 ? 0 : static_cast<uint32_t>((file_size + chunk_size - 1) / chunk_size);
    }
    size_t chunkLength(uint32_t index) const {    // the last chunk may be shorter
        uint64_t offset = static_cast<uint64_t>(index) * chunk_size;
        if (offset >= file_size) return 0;
        uint64_t left = file_size - offset;
        return static_cast<size_t>(left < chunk_size ? left : chunk_size);
    }
};
inline std::vector<uint8_t> encode(const FileManifest& m) {
    ByteWriter w;
    w.u32(m.file_id); w.u64(m.file_size); w.u16(m.chunk_size);
    w.u16(static_cast<uint16_t>(m.name.size())); w.text(m.name); w.bytes(m.sha256, SHA256_SIZE);
    return std::move(w.buffer);
}
inline bool decode(const std::vector<uint8_t>& p, FileManifest& m) {
    ByteReader r(p);
    m.file_id = r.u32(); m.file_size = r.u64(); m.chunk_size = r.u16();
    uint16_t name_length = r.u16();
    m.name = r.text(name_length);
    std::vector<uint8_t> hash = r.bytes(SHA256_SIZE);
    if (!r.ok() || r.remaining() != 0) return false;
    std::memcpy(m.sha256, hash.data(), SHA256_SIZE);
    return m.chunk_size > 0 && m.chunk_size <= MAX_CHUNK_DATA_SIZE;
}

// ---- CHUNK_REQ / CHUNK_STORE : file_id(4) chunk_index(4) -------------------------------------------------
// CHUNK_STORE means "you are a replica holder of this chunk: fetch it from me with CHUNK_REQ".
struct ChunkRef { uint32_t file_id = 0; uint32_t chunk_index = 0; };
inline std::vector<uint8_t> encode(const ChunkRef& c) {
    ByteWriter w; w.u32(c.file_id); w.u32(c.chunk_index); return std::move(w.buffer);
}
inline bool decode(const std::vector<uint8_t>& p, ChunkRef& c) {
    if (p.size() != CHUNK_META_SIZE) return false;
    ByteReader r(p); c.file_id = r.u32(); c.chunk_index = r.u32(); return r.ok();
}

// ---- CHUNK_DATA : file_id(4) chunk_index(4) bytes(1..1400) -----------------------------------------------
struct ChunkData {
    uint32_t file_id = 0;
    uint32_t chunk_index = 0;
    std::vector<uint8_t> data;
};
inline std::vector<uint8_t> encode(const ChunkData& c) {
    ByteWriter w; w.u32(c.file_id); w.u32(c.chunk_index);
    w.bytes(c.data.data(), c.data.size() > MAX_CHUNK_DATA_SIZE ? MAX_CHUNK_DATA_SIZE : c.data.size());
    return std::move(w.buffer);
}
inline bool decode(const std::vector<uint8_t>& p, ChunkData& c) {
    ByteReader r(p);
    c.file_id = r.u32(); c.chunk_index = r.u32();
    if (!r.ok()) return false;
    c.data = r.rest();
    return !c.data.empty() && c.data.size() <= MAX_CHUNK_DATA_SIZE;
}

// ---- CHUNK_HAVE : file_id(4) first_chunk(4) bitmap ----------------------------------------------------------
// bit i (byte i/8, bit position i%8, LSB first) == "I store chunk first_chunk + i".
struct ChunkHave {
    uint32_t file_id = 0;
    uint32_t first_chunk = 0;
    std::vector<uint8_t> bitmap;
    bool get(uint32_t i) const { return i / 8 < bitmap.size() && ((bitmap[i / 8] >> (i % 8)) & 1u) != 0; }
    void set(uint32_t i, bool value = true) {
        if (i / 8 >= bitmap.size()) bitmap.resize(i / 8 + 1, 0);
        if (value) bitmap[i / 8] = static_cast<uint8_t>(bitmap[i / 8] | (1u << (i % 8)));
        else bitmap[i / 8] = static_cast<uint8_t>(bitmap[i / 8] & ~(1u << (i % 8)));
    }
};
inline std::vector<uint8_t> encode(const ChunkHave& c) {
    ByteWriter w; w.u32(c.file_id); w.u32(c.first_chunk);
    w.bytes(c.bitmap.data(), c.bitmap.size() > MAX_CHUNK_DATA_SIZE ? MAX_CHUNK_DATA_SIZE : c.bitmap.size());
    return std::move(w.buffer);
}
inline bool decode(const std::vector<uint8_t>& p, ChunkHave& c) {
    ByteReader r(p);
    c.file_id = r.u32(); c.first_chunk = r.u32();
    if (!r.ok()) return false;
    c.bitmap = r.rest();
    return !c.bitmap.empty();
}

// ---- CHUNK_NAK : file_id(4) chunk_index(4) reason(1) --------------------------------------------------------
constexpr uint8_t NAK_NO_FILE = 1;
constexpr uint8_t NAK_NO_CHUNK = 2;
constexpr uint8_t NAK_BUSY = 3;
struct ChunkNak { uint32_t file_id = 0; uint32_t chunk_index = 0; uint8_t reason = NAK_NO_CHUNK; };
inline std::vector<uint8_t> encode(const ChunkNak& c) {
    ByteWriter w; w.u32(c.file_id); w.u32(c.chunk_index); w.u8(c.reason); return std::move(w.buffer);
}
inline bool decode(const std::vector<uint8_t>& p, ChunkNak& c) {
    if (p.size() != CHUNK_META_SIZE + 1) return false;
    ByteReader r(p); c.file_id = r.u32(); c.chunk_index = r.u32(); c.reason = r.u8(); return r.ok();
}

// ---- CHAT_MSG : raw UTF-8 text (1..1408 bytes) ---------------------------------------------------------------
inline std::vector<uint8_t> encodeText(const std::string& s) {
    size_t n = s.size() > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : s.size();
    return std::vector<uint8_t>(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}
inline std::string decodeText(const std::vector<uint8_t>& p) { return std::string(p.begin(), p.end()); }

// (DATA_ACK payload = SackBitmap, see sack_bitmap.hpp.  HEARTBEAT / FILE_LIST_REQ payload is empty.)

} // namespace p2p
