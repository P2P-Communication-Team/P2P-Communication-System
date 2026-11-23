# P2P Group Communication and Distributed File Sharing Protocol
## Protocol Specification — Version 1.0

**Project:** Serverless P2P Group Communication over UDP  
**Transport:** UDP / IPv4 (IPv6-compatible design)  
**Network size:** 4–10 active peers  
**Topology:** Full-mesh unicast for application data; multicast/broadcast for LAN discovery  
**Header size:** 16-byte fixed base header, aligned to 4 bytes  
**Reliability:** Application-layer Selective Repeat ARQ  
**Status:** Design baseline — implement and test against this document before changing the wire format

---

## 1. Purpose and scope

This document defines the application-layer protocol used by the project. The application provides:

1. Peer discovery and membership without a permanently required central server.
2. Group chat delivered to all active peers.
3. Reliable, ordered-per-sender chat over UDP.
4. Distributed file sharing using fixed-size logical chunks.
5. At least two replicas of every stored chunk, placed on different peers.
6. Multi-source downloads from at least two peers in parallel.
7. Recovery from a source peer leaving or crashing during a download.
8. Message integrity checks for file chunks and complete files.

The protocol is designed for a small group of 4–10 peers. It deliberately avoids the complexity of a general-purpose DHT or large-scale overlay network.

### 1.1 Design principles

- No permanently required central server or central file store.
- Use UDP for application messages.
- Use application-layer reliability where delivery is required.
- Do not implement a second, custom link-layer reliability mechanism.
- Keep the common header small and fixed.
- Put message-specific information in the message payload.
- Use independent reliability state for each direction of every peer connection.
- Keep logical chat ordering separate from packet-level ARQ sequence numbers.
- Make future protocol extensions explicit and version-compatible.

### 1.2 Important terminology

| Term | Meaning |
|---|---|
| Peer | A running instance of the application on a device |
| Peer slot / Peer ID | A short ID from 1 to 10 assigned to a current membership |
| Peer UUID | A persistent, globally unique peer identity carried in membership payloads |
| Session ID | A random value identifying one running incarnation of a peer |
| Packet sequence | Sequence number used by ARQ for one unicast direction |
| Chat sequence | Logical per-sender sequence number used to order chat messages |
| Logical chunk | A portion of a file stored and replicated as a unit |
| Fragment | A portion of a logical chunk carried in one UDP datagram |
| Replica | A copy of a logical chunk stored on another peer |

---

## 2. Network architecture

### 2.1 Peer topology

The application uses a full-mesh communication model for normal peer-to-peer traffic. Each peer sends its own unicast datagram to each intended recipient.

For a group of `N` peers, one group-chat message produces `N - 1` unicast sends from the originating peer. This is acceptable for the target group size of 4–10 peers and avoids a central relay.

### 2.2 Discovery

Peers discover one another using LAN multicast or broadcast. A bootstrap peer may optionally help a new peer learn about existing members, but it is not a permanent server and does not store or relay all application data.

Discovery messages may use peer slot `0`, which means “not yet assigned.” Once admitted, a peer receives a slot from `1` to `10`.

### 2.3 Membership identity

The short Peer ID is a temporary membership slot, not a permanent identity. The membership payload must carry:

- Persistent Peer UUID (recommended 128-bit value).
- Peer slot.
- Session ID.
- IP address and UDP port, where needed.
- Membership generation/version, where needed.
- Capabilities and protocol version information.

A peer restart creates a new Session ID. Receivers must treat a new Session ID for the same Peer UUID as a new peer incarnation and clear old per-session reliability and chat-ordering state.

**Slot allocation is a membership procedure, not something solved by the header.** The implementation must resolve simultaneous claims deterministically (for example, by comparing Peer UUIDs) and reject duplicate active slot assignments.

---

## 3. UDP datagram format

A datagram consists of:

```text
+------------------------------+
| 16-byte Base Header          |
+------------------------------+
| Optional Header Extensions   | 0 or more 4-byte words
+------------------------------+
| Message Payload              |
+------------------------------+
```

The base header is always 16 bytes. If extensions are present, their size is indicated by the `EXT_LEN_WORDS` field. The message payload begins after the base header and all extension bytes.

All multi-byte integer fields use **network byte order (big-endian)**.

Do not transmit a native C++ struct directly. Serialize and parse each field explicitly using fixed-width integer types and byte operations. Do not use C++ bitfields or rely on compiler struct packing.

---

## 4. Fixed 16-byte base header

### 4.1 Header layout

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 byte | `MAGIC` | Protocol marker; fixed value `0xD7` |
| 1 | 1 byte | `VERSION_TYPE` | 3-bit protocol version + 5-bit message type |
| 2 | 1 byte | `PEER_FLAGS` | 4-bit Peer ID + 4-bit flags |
| 3–6 | 4 bytes | `SESSION_ID` | 32-bit peer-incarnation identifier |
| 7–8 | 2 bytes | `PACKET_SEQ` | 16-bit ARQ packet sequence number |
| 9–10 | 2 bytes | `ACK_BASE` | 16-bit cumulative acknowledgement |
| 11–14 | 4 bytes | `ACK_BITMAP` | 32-bit selective acknowledgement bitmap |
| 15 | 1 byte | `EXT_LEN_WORDS` | Number of 4-byte extension words following the base header |
| **Total** | **16 bytes** | | |


### 4.1.1 Exact byte and bit diagram

The following diagram is the wire layout. Each column is exactly one byte. Bit 7 is the most-significant bit (MSB); bit 0 is the least-significant bit (LSB).

```text
BYTE OFFSET    BIT 7                                         BIT 0
             +--------+--------+--------+--------+--------+--------+--------+--------+
0            |                              MAGIC (8)                                |
             +--------+--------+--------+--------+--------+--------+--------+--------+
1            | VERSION (3) |                 MESSAGE_TYPE (5)                        |
             +--------+--------+--------+--------+--------+--------+--------+--------+
2            | PEER_ID (4) |                     FLAGS (4)                           |
             +--------+--------+--------+--------+--------+--------+--------+--------+
3            |                         SESSION_ID [31:24]                             |
4            |                         SESSION_ID [23:16]                             |
5            |                         SESSION_ID [15:8]                              |
6            |                         SESSION_ID [7:0]                               |
             +--------+--------+--------+--------+--------+--------+--------+--------+
7            |                         PACKET_SEQ [15:8]                              |
8            |                         PACKET_SEQ [7:0]                               |
             +--------+--------+--------+--------+--------+--------+--------+--------+
9            |                           ACK_BASE [15:8]                              |
10           |                           ACK_BASE [7:0]                               |
             +--------+--------+--------+--------+--------+--------+--------+--------+
11           |                         ACK_BITMAP [31:24]                             |
12           |                         ACK_BITMAP [23:16]                             |
13           |                         ACK_BITMAP [15:8]                              |
14           |                         ACK_BITMAP [7:0]                               |
             +--------+--------+--------+--------+--------+--------+--------+--------+
15           |                         EXT_LEN_WORDS (8)                              |
             +--------+--------+--------+--------+--------+--------+--------+--------+
             | OPTIONAL EXTENSION BYTES (EXT_LEN_WORDS * 4)                           |
             +------------------------------------------------------------------------+
             | MESSAGE PAYLOAD (message-specific layout)                              |
             +------------------------------------------------------------------------+
```

The bit packing inside the two packed bytes is:

```text
Byte 1:
  bit 7  bit 6  bit 5 | bit 4  bit 3  bit 2  bit 1  bit 0
  +-------------------+--------------------------------+
  | VERSION, 3 bits   | MESSAGE_TYPE, 5 bits           |
  +-------------------+--------------------------------+
  VERSION mask     = 0xE0; shift right 5
  MESSAGE_TYPE mask= 0x1F; no shift

Byte 2:
  bit 7  bit 6  bit 5  bit 4 | bit 3  bit 2  bit 1  bit 0
  +---------------------------+-------------------------+
  | PEER_ID, 4 bits           | FLAGS, 4 bits            |
  +---------------------------+-------------------------+
  PEER_ID mask = 0xF0; shift right 4
  FLAGS mask   = 0x0F; no shift
```

### 4.1.2 Field-by-field wire table

| Offset | Field | Width | Wire type | Allowed / interpretation |
|---:|---|---:|---|---|
| 0 | `MAGIC` | 8 bits | `uint8_t` | Must equal `0xD7` |
| 1 bits 7–5 | `VERSION` | 3 bits | packed unsigned | Must equal `1` |
| 1 bits 4–0 | `MESSAGE_TYPE` | 5 bits | packed unsigned | Values listed in Section 6 |
| 2 bits 7–4 | `PEER_ID` | 4 bits | packed unsigned | 0 for discovery/pre-join; 1–10 assigned |
| 2 bits 3–0 | `FLAGS` | 4 bits | packed unsigned | `ACK_VALID`, `RELIABLE`, `RETRANSMIT`, `PRIORITY` |
| 3–6 | `SESSION_ID` | 32 bits | `uint32_t` | Big-endian; sender process incarnation |
| 7–8 | `PACKET_SEQ` | 16 bits | `uint16_t` | Big-endian; ARQ sequence when reliable |
| 9–10 | `ACK_BASE` | 16 bits | `uint16_t` | Big-endian; cumulative ACK |
| 11–14 | `ACK_BITMAP` | 32 bits | `uint32_t` | Big-endian; bit 0 acknowledges `ACK_BASE + 1` |
| 15 | `EXT_LEN_WORDS` | 8 bits | `uint8_t` | Count of 4-byte extension words; zero in baseline v1.0 |

**Exact offsets are relative to the first byte of the UDP application datagram.** Payload offsets in Section 8 are relative to the first byte of the message payload, after the 16-byte base header and any extension bytes.

The base header is 16 bytes, which is a multiple of 4. With extensions, the complete header length is:

```text
HEADER_LENGTH = 16 + (EXT_LEN_WORDS * 4)
```

`EXT_LEN_WORDS = 0` means no extension block is present. Version 1.0 senders should normally send zero unless a negotiated extension is being used.

### 4.2 Byte 0 — MAGIC

- Size: 1 byte.
- Constant: `0xD7`.
- Used to quickly reject unrelated or malformed datagrams.

### 4.3 Byte 1 — VERSION_TYPE

```text
Bit:       7 6 5 | 4 3 2 1 0
           VERSION | MESSAGE_TYPE
```

Packing:

```cpp
byte1 = (version << 5) | (messageType & 0x1F);
```

Unpacking:

```cpp
version     = (byte1 >> 5) & 0x07;
messageType =  byte1       & 0x1F;
```

- Version uses 3 bits and can represent values 0–7.
- Version `1` is used by this specification.
- Message type uses 5 bits and can represent values 0–31.
- Message types 1–18 are currently assigned in this document.
- Unknown message types must be rejected or ignored safely; they must never cause a crash.

### 4.4 Byte 2 — PEER_FLAGS

```text
Bit:       7 6 5 4 | 3 2 1 0
           PEER_ID | FLAGS
```

Packing:

```cpp
byte2 = ((peerID & 0x0F) << 4) | (flags & 0x0F);
```

Unpacking:

```cpp
peerID = (byte2 >> 4) & 0x0F;
flags  =  byte2       & 0x0F;
```

Peer ID values:

| Value | Meaning |
|---:|---|
| 0 | Unassigned / discovery stage only |
| 1–10 | Assigned active peer slots |
| 11–15 | Reserved; invalid in protocol version 1 |

Peer ID `0` is permitted only for discovery and pre-membership messages. Normal chat, heartbeat, and file-transfer messages must use an assigned Peer ID from 1 to 10.

### 4.5 Flags

The low four bits of `PEER_FLAGS` are:

| Bit | Mask | Name | Meaning |
|---:|---:|---|---|
| 0 | `0x01` | `ACK_VALID` | ACK_BASE and ACK_BITMAP contain valid acknowledgement state |
| 1 | `0x02` | `RELIABLE` | Packet belongs to the reliable ARQ stream |
| 2 | `0x04` | `RETRANSMIT` | This is a retransmission of an earlier reliable packet |
| 3 | `0x08` | `PRIORITY` | Message is eligible for priority scheduling |

Rules:

- If `ACK_VALID` is clear, receivers must ignore ACK_BASE and ACK_BITMAP. Senders should set both fields to zero in this case.
- If `RELIABLE` is clear, PACKET_SEQ is not used for ARQ. Senders should set it to zero.
- A retransmission retains the original PACKET_SEQ and message contents; it only sets `RETRANSMIT`.
- `PRIORITY` is a scheduling hint, not a guarantee of delivery or a separate reliability class.
- Unknown flag meanings must not be assumed. Reserved future flags require a negotiated protocol extension/version.

### 4.6 SESSION_ID

- Size: 4 bytes (32 bits).
- Generated randomly when a peer process starts.
- Identifies one running incarnation of a peer.
- A restart should generate a new value.
- The combination `(Peer UUID, SESSION_ID)` identifies a current peer incarnation.

A 32-bit value is a compact engineering compromise, not a permanent globally unique identity. The membership layer should detect an improbable collision and require the peer to generate a new Session ID. If stronger collision resistance is required later, a 64-bit Session ID can be introduced in a future protocol version or negotiated extension.

### 4.7 PACKET_SEQ

- Size: 2 bytes (16 bits).
- Used only when `RELIABLE` is set.
- First reliable packet in a new directional session starts at sequence `1`.
- Sequence numbers wrap modulo 65536: `65535 -> 0 -> 1`.
- Each unicast direction has its own independent sequence space.
- The sender must not have more than 32 unacknowledged packets outstanding.

Packet sequence numbers are not chat sequence numbers. The same logical chat message sent to multiple peers uses one Chat Sequence but has a separate Packet Sequence for each destination.

### 4.8 ACK_BASE

- Size: 2 bytes (16 bits).
- Cumulative acknowledgement.
- Represents the highest consecutively received reliable packet sequence number.
- Initial value is `0`.
- For example, `ACK_BASE = 5` means packets through sequence 5 have been received in order, with no gap before 5.

### 4.9 ACK_BITMAP

- Size: 4 bytes (32 bits).
- Selective acknowledgement (SACK) bitmap.
- Bit 0 represents `ACK_BASE + 1`.
- Bit 1 represents `ACK_BASE + 2`.
- ...
- Bit 31 represents `ACK_BASE + 32`.
- All additions are modulo 65536.

A set bit means that the corresponding packet has been received, even if an earlier packet is missing.

Example:

```text
Received: 1, 2, 4, 5
Missing:  3

ACK_BASE   = 2
ACK_BITMAP = 0x00000006
```

Here bit 1 acknowledges packet 4 and bit 2 acknowledges packet 5, because the bitmap starts at `ACK_BASE + 1` (packet 3).

### 4.10 EXT_LEN_WORDS — reserved extension length

- Size: 1 byte.
- Value: number of 4-byte words in the optional extension block immediately after the 16-byte base header.
- Value `0`: no extension block; message payload begins at byte offset 16.
- Value `1`: 4 extension bytes; message payload begins at offset 20.
- Maximum representable extension area: 255 × 4 = 1020 bytes.

This byte is deliberately included for future protocol growth without changing the 16-byte base-header size.

Version 1.0 implementation requirements:

1. Support `EXT_LEN_WORDS = 0`.
2. If extensions are not implemented, reject datagrams with a non-zero value rather than misinterpreting extension bytes as payload.
3. Do not invent extension meanings locally. Extensions must be documented and negotiated.
4. The complete datagram must contain at least `16 + 4 * EXT_LEN_WORDS` bytes.
5. The extension block must be a multiple of 4 bytes.

---

## 5. Optional header extension format

The extension area is reserved for future needs such as negotiated capabilities, larger identifiers, tracing, or future reliability options. It is not required for the current project baseline.

When extensions are introduced, use a sequence of padded TLV records:

| Field | Size | Description |
|---|---:|---|
| `EXT_TYPE` | 1 byte | Extension identifier |
| `EXT_LENGTH` | 1 byte | Number of value bytes |
| `EXT_VALUE` | Variable | Extension data |
| Padding | 0–3 bytes | Zero bytes to align the next TLV to a 4-byte boundary |

The extension area as a whole must occupy exactly `EXT_LEN_WORDS * 4` bytes.

Extension rules:

- Extension type assignments must be documented centrally in this protocol file.
- Unknown non-critical extensions may be skipped using their length.
- An unknown critical extension must cause the packet to be rejected.
- The critical/non-critical convention must be defined when the first extension type is assigned; do not infer it from an arbitrary bit without updating this specification.
- Extensions must not silently change the meaning of existing base-header fields.
- Extension use should be negotiated through peer capability information during joining.
- Version 1.0 baseline messages use no extensions.

---

## 6. Message type registry

`MESSAGE_TYPE` is the lower five bits of byte 1.

| Value | Name | Purpose | Normally reliable? |
|---:|---|---|---|
| `0x01` | `DISCOVER` | Announce/search for peers on the LAN | No |
| `0x02` | `DISCOVER_REPLY` | Reply to a discovery announcement | No |
| `0x03` | `JOIN_REQUEST` | Request membership | Yes, after a peer is identified |
| `0x04` | `JOIN_ACCEPT` | Accept a joining peer and assign membership information | Yes |
| `0x05` | `PEER_LIST` | Share current membership information | Yes |
| `0x06` | `JOIN_ANNOUNCE` | Notify members about a newly joined peer | Yes |
| `0x07` | `HEARTBEAT` | Indicate that a peer is active | Usually no |
| `0x08` | `LEAVE` | Graceful departure announcement | Yes |
| `0x09` | `CHAT` | Group-chat message | Yes |
| `0x0A` | `ACK` | Optional standalone acknowledgement | No |
| `0x0B` | `FILE_ADVERTISE` | Advertise a shared file/manifest | Yes |
| `0x0C` | `MANIFEST_REQUEST` | Request file manifest metadata | Yes |
| `0x0D` | `MANIFEST_RESPONSE` | Send manifest metadata | Yes |
| `0x0E` | `HAVE_BITMAP` | Advertise available chunks | Yes or periodically refreshed |
| `0x0F` | `CHUNK_REQUEST` | Request a chunk fragment | Yes |
| `0x10` | `CHUNK_DATA` | Deliver a chunk fragment | Yes |
| `0x11` | `TRANSFER_CANCEL` | Cancel a transfer/request | Yes |
| `0x12` | `ERROR_MSG` | Report a protocol/application error | No or best effort |

“Normally reliable” is a design recommendation. The implementation may define selected discovery or heartbeat exchanges as best-effort because they are repeated periodically. Any message whose loss would break application correctness must use reliable delivery or an explicit retry/refresh procedure.

Message type values 19–31 are unassigned and reserved for future use.

---

## 7. Application-layer reliability: Selective Repeat ARQ

### 7.1 Reliability model

Use Selective Repeat ARQ with:

- 16-bit packet sequence numbers.
- 32-packet sender/receiver window.
- 16-bit cumulative ACK (`ACK_BASE`).
- 32-bit SACK bitmap (`ACK_BITMAP`).
- Retransmission of missing packets only.
- Duplicate detection.
- Out-of-order receive buffering.
- ACK information piggybacked on outgoing packets whenever possible.

The 32-packet window is intentionally matched to the 32-bit SACK bitmap. The sender must never exceed this window.

### 7.2 Directional state

For each pair of peers, maintain independent state in each direction:

- Sender next sequence number.
- Sender unacknowledged packet buffer.
- Retransmission timer/state.
- Receiver cumulative ACK base.
- Receiver SACK bitmap.
- Out-of-order receive buffer.

For example, A→B and B→A are separate ARQ streams. An ACK carried by a packet from A to B acknowledges B→A traffic, not A→B traffic.

### 7.3 ACK processing

When a packet is received:

1. Validate the header and identify the sender/session.
2. Process the packet's ACK fields if `ACK_VALID` is set.
3. If `RELIABLE` is set, process PACKET_SEQ in the receive window.
4. If it is a new in-window packet, buffer/process it as appropriate.
5. If it is a duplicate, do not deliver its application content twice.
6. Send or piggyback updated ACK state.

ACK processing:

- All packets at or below ACK_BASE are cumulatively acknowledged, using modular sequence arithmetic.
- For each set bit `i`, packet `(ACK_BASE + 1 + i) mod 65536` is selectively acknowledged.
- The sender removes acknowledged packets from its retransmission buffer.
- Missing packets are retransmitted after the configured timeout.
- Retransmissions use the same PACKET_SEQ and message content.

### 7.4 Lost ACK handling

A standalone ACK is not itself reliable and does not need an ACK. If an ACK is lost, the sender retransmits the original reliable packet after timeout. The receiver recognizes the duplicate and sends/piggybacks the current ACK state again.

### 7.5 Sequence wrap-around

Never compare 16-bit sequence numbers using ordinary `a > b` comparisons across wrap-around. Use modular arithmetic. The maximum window must remain smaller than half the sequence-number space; the selected window of 32 satisfies this requirement.

A common comparison helper is:

```cpp
bool seqBefore(uint16_t a, uint16_t b)
{
    return a != b &&
           static_cast<uint16_t>(b - a) < 0x8000;
}
```

All send and receive window checks must use consistent modular arithmetic.

### 7.6 Chat ordering and duplicate prevention

Packet-level ARQ ordering is not sufficient for group chat because each recipient has a separate packet sequence stream.

Each CHAT payload therefore includes a separate 32-bit `CHAT_SEQUENCE`. The receiver tracks the expected chat sequence independently for each `(Peer UUID, Session ID)`:

- A new expected chat sequence is delivered.
- A later chat sequence is buffered until missing earlier messages arrive.
- An already delivered chat sequence is discarded as a duplicate.
- Chat messages are delivered in order per sender.
- The protocol does not promise one global total order across all senders.

The sender uses the same logical Chat Sequence when sending one chat message to all recipients, but each recipient-specific unicast packet uses its own PACKET_SEQ.

---

## 8. Exact message payload wire layouts

This section defines the exact byte layouts that C++ modules must implement. The offsets in every table are **payload-relative**: offset `0` means the first byte after the complete header.

### 8.1 Rules applying to every payload

1. All multi-byte integers use network byte order (big-endian).
2. `uint8`, `uint16`, `uint32`, and `uint64` mean unsigned integers of exactly 1, 2, 4, and 8 bytes.
3. UUIDs are exactly 16 bytes in the agreed canonical byte order. For this project, transmit the 16 UUID bytes in their stored/network order; do not serialize a platform-specific `GUID`/`UUID` struct directly.
4. IPv4 addresses are 4 bytes in network order. IPv6 addresses are 16 bytes.
5. Strings are UTF-8 byte sequences, not null-terminated C strings. Their length is given by the preceding length field.
6. A receiver must check the complete payload length before reading any field.
7. For a fixed-size payload, the payload length must equal the specified size. For a variable-size payload, the length must exactly match the length formula.
8. Unless a field explicitly says otherwise, no implicit padding or alignment bytes exist between payload fields.
9. The common header's `SESSION_ID` identifies the sender's current process. A repeated Session ID inside a payload is not required unless the payload is describing another peer.
10. `ACK_VALID`/`RELIABLE` behavior is controlled by the common header, not by duplicate fields in a payload.

### 8.2 DISCOVER payload — message type `0x01`

Purpose: announce a peer on the local network before it has an assigned Peer ID.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `PEER_UUID` | 16-byte array | 16 | Persistent identity of discovering peer |
| 16–17 | `UDP_PORT` | `uint16` | 2 | Port on which this peer receives UDP |
| 18 | `ADDRESS_FAMILY` | `uint8` | 1 | `4` = IPv4, `6` = IPv6 |
| 19 | `CAPABILITIES` | `uint8` | 1 | Bit 0 = supports file sharing; bit 1 = supports chat; other bits reserved, set to 0 |
| **Total** | | | **20** | |

Header requirements: `PEER_ID = 0`, `RELIABLE = 0`, `PACKET_SEQ = 0`. The UDP source address is the observed endpoint; `UDP_PORT` is the listening port advertised by the peer.

### 8.3 DISCOVER_REPLY payload — message type `0x02`

Purpose: tell the discovering peer that an existing peer is available.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `PEER_UUID` | 16-byte array | 16 | UUID of replying peer |
| 16–17 | `UDP_PORT` | `uint16` | 2 | Replying peer's listening port |
| 18 | `ASSIGNED_PEER_ID` | `uint8` | 1 | Existing peer's current slot, 1–10 |
| 19 | `ADDRESS_FAMILY` | `uint8` | 1 | `4` = IPv4, `6` = IPv6 |
| 20–23 | `MEMBERSHIP_EPOCH` | `uint32` | 4 | Current membership generation |
| **Total** | | | **24** | |

Header requirements: `PEER_ID` is the replying peer's assigned slot (1–10); `RELIABLE = 0`.

### 8.4 JOIN_REQUEST payload — message type `0x03`

Purpose: request admission from an existing peer.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `PEER_UUID` | 16-byte array | 16 | Joining peer's persistent UUID |
| 16–17 | `UDP_PORT` | `uint16` | 2 | Joining peer's listening port |
| 18 | `ADDRESS_FAMILY` | `uint8` | 1 | `4` = IPv4, `6` = IPv6 |
| 19–22 | `CAPABILITIES` | `uint32` | 4 | Capability bitmask; unknown bits set to 0 |
| **Total** | | | **23** | |

Header requirements: `PEER_ID = 0`; use reliable delivery after a specific existing peer has been selected. The joining peer's Session ID is in the common header.

### 8.5 JOIN_ACCEPT payload — message type `0x04`

Purpose: accept the joining peer and assign its membership slot.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `JOINED_PEER_UUID` | 16-byte array | 16 | UUID of joining peer |
| 16 | `ASSIGNED_PEER_ID` | `uint8` | 1 | Assigned slot, 1–10 |
| 17–20 | `MEMBERSHIP_EPOCH` | `uint32` | 4 | Membership generation after join |
| 21 | `ACTIVE_PEER_COUNT` | `uint8` | 1 | Number of active peers after join |
| 22–23 | `STATUS` | `uint16` | 2 | `0` = accepted; non-zero = rejection code |
| **Total** | | | **24** | |

The receiver must verify that `JOINED_PEER_UUID` matches its own UUID before accepting the assignment.

### 8.6 PEER_LIST payload — message type `0x05`

Purpose: send the current membership snapshot to a joining or existing peer.

Fixed header:

| Payload offset | Field | Type | Size |
|---:|---|---|---:|
| 0–3 | `MEMBERSHIP_EPOCH` | `uint32` | 4 |
| 4 | `PEER_COUNT` | `uint8` | 1 |

Then exactly `PEER_COUNT` entries follow. Each entry is 24 bytes:

| Entry-relative offset | Field | Type | Size |
|---:|---|---|---:|
| 0–15 | `PEER_UUID` | 16-byte array | 16 |
| 16 | `PEER_ID` | `uint8` | 1 |
| 17–20 | `SESSION_ID` | `uint32` | 4 |
| 21–22 | `UDP_PORT` | `uint16` | 2 |
| 23 | `ADDRESS_FAMILY` | `uint8` | 1 |

The IP address is learned from the observed UDP source address during membership exchange. This version's PEER_LIST does not carry an IP address. If routable addresses must be advertised explicitly in a later version, add a documented variable-length address field (4 bytes for IPv4 or 16 bytes for IPv6) and update the format/version rules.

Exact total payload length:

```text
5 + (PEER_COUNT * 24) bytes
```

`PEER_COUNT` must be between 1 and 10. The list must not contain duplicate Peer IDs or duplicate Peer UUIDs.

### 8.7 JOIN_ANNOUNCE payload — message type `0x06`

Purpose: announce a newly admitted peer to existing members.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `PEER_UUID` | 16-byte array | 16 | New peer UUID |
| 16 | `PEER_ID` | `uint8` | 1 | Assigned slot, 1–10 |
| 17–20 | `SESSION_ID` | `uint32` | 4 | New peer's session |
| 21–24 | `MEMBERSHIP_EPOCH` | `uint32` | 4 | Updated membership generation |
| 25–26 | `UDP_PORT` | `uint16` | 2 | New peer's listening port |
| **Total** | | | **27** | |

### 8.8 LEAVE payload — message type `0x08`

Purpose: announce graceful departure.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `PEER_UUID` | 16-byte array | 16 | Departing peer UUID |
| 16–17 | `REASON_CODE` | `uint16` | 2 | `0` = normal shutdown; other values reserved |
| 18–21 | `MEMBERSHIP_EPOCH` | `uint32` | 4 | Membership generation at departure |
| **Total** | | | **22** | |

The common header must contain the departing peer's assigned Peer ID and current Session ID.

### 8.9 HEARTBEAT payload — message type `0x07`

Purpose: indicate that a peer is still active.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–3 | `HEARTBEAT_COUNTER` | `uint32` | 4 | Increases by one for each heartbeat from this session |
| 4–7 | `MEMBERSHIP_EPOCH` | `uint32` | 4 | Sender's current membership generation |
| **Total** | | | **8** | |

Header requirements: assigned Peer ID and current Session ID. Heartbeats are normally unreliable and periodically repeated. A receiver must not declare a peer dead after one missed heartbeat.

### 8.10 CHAT payload — message type `0x09`

Purpose: deliver one chat message.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–3 | `CHAT_SEQUENCE` | `uint32` | 4 | Per-sender logical chat sequence, starts at 1 |
| 4–5 | `TEXT_LENGTH` | `uint16` | 2 | Number of UTF-8 bytes in TEXT |
| 6 onward | `TEXT` | byte array | `TEXT_LENGTH` | UTF-8 message text |
| **Total** | | | **6 + TEXT_LENGTH** | |

The payload length must equal `6 + TEXT_LENGTH`. Do not include a null terminator. For version 1.0, one chat message must fit in one UDP datagram. The receiver orders messages by `(PEER_UUID, SESSION_ID, CHAT_SEQUENCE)`, not by PACKET_SEQ.

### 8.11 ACK payload — message type `0x0A`

**ACK has a zero-byte payload.** The acknowledgement information is carried entirely in the common header fields `ACK_BASE` and `ACK_BITMAP`, with `ACK_VALID = 1`.

| Payload offset | Field | Type | Size |
|---|---|---|---:|
| — | No payload fields | — | **0** |

Header requirements: `ACK_VALID = 1`, `RELIABLE = 0`, `PACKET_SEQ = 0`, `EXT_LEN_WORDS = 0` for baseline v1.0. An ACK packet does not itself require an ACK. Implementations may also piggyback ACK fields on other outgoing packets and omit standalone ACK packets when a timely outgoing packet can carry the same acknowledgement.

### 8.12 FILE_META payload — message type `0x0B` (`FILE_ADVERTISE`)

For compatibility with the existing message registry, wire message type `0x0B` remains named `FILE_ADVERTISE`; its payload schema is named `FILE_META`. Do not assign a second message type for the same operation.

Purpose: advertise one file and its manifest summary.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `FILE_ID` | 16-byte array | 16 | Stable file identifier |
| 16–17 | `FILE_NAME_LENGTH` | `uint16` | 2 | UTF-8 filename length |
| 18 onward | `FILE_NAME` | byte array | variable | UTF-8 filename; no null terminator |
| after name | `FILE_SIZE` | `uint64` | 8 | Original file size in bytes |
| next | `CHUNK_SIZE` | `uint32` | 4 | Logical chunk size in bytes |
| next | `CHUNK_COUNT` | `uint32` | 4 | Number of logical chunks |
| next | `FILE_SHA256` | 32-byte array | 32 | SHA-256 of complete file |
| next | `REPLICATION_FACTOR` | `uint8` | 1 | Required replica count; baseline minimum is 2 |
| **Total** | | | **67 + FILE_NAME_LENGTH** | |

The `FILE_META` payload advertises metadata only. If the complete per-chunk hash list is too large, transfer it using MANIFEST_RESPONSE messages. The final chunk may be shorter than `CHUNK_SIZE`.

### 8.13 MANIFEST_REQUEST payload — message type `0x0C`

| Payload offset | Field | Type | Size |
|---:|---|---|---:|
| 0–15 | `FILE_ID` | 16-byte array | 16 |
| **Total** | | | **16** |

### 8.14 MANIFEST_RESPONSE payload — message type `0x0D`

This message carries a section of the manifest's per-chunk hash list.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `FILE_ID` | 16-byte array | 16 | File identifier |
| 16–19 | `MANIFEST_TRANSFER_ID` | `uint32` | 4 | Identifies this manifest transfer |
| 20–23 | `FIRST_CHUNK_INDEX` | `uint32` | 4 | Index of first hash in this payload |
| 24–25 | `HASH_COUNT` | `uint16` | 2 | Number of 32-byte hashes following |
| 26 onward | `CHUNK_HASHES` | byte array | `32 * HASH_COUNT` | SHA-256 hashes in chunk-index order |
| **Total** | | | **26 + 32 * HASH_COUNT** | |

### 8.15 HAVE_BITMAP payload — message type `0x0E`

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–15 | `FILE_ID` | 16-byte array | 16 | File identifier |
| 16–19 | `BITMAP_START` | `uint32` | 4 | First chunk index represented |
| 20–21 | `BITMAP_LENGTH` | `uint16` | 2 | Number of bitmap bytes |
| 22 onward | `BITMAP` | byte array | `BITMAP_LENGTH` | One bit per chunk; bit 1 means peer has chunk |
| **Total** | | | **22 + BITMAP_LENGTH** | |

Within each bitmap byte, bit 0 (least-significant bit) represents the lowest chunk index in that byte. For byte `j`, bit `b` represents chunk index `BITMAP_START + (j * 8) + b`.

### 8.16 CHUNK_REQ payload — message type `0x0F`

Purpose: request a fragment of one logical file chunk. This is the same operation previously called `CHUNK_REQUEST`; the wire type remains `0x0F`.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–3 | `TRANSFER_ID` | `uint32` | 4 | Download operation identifier |
| 4–19 | `FILE_ID` | 16-byte array | 16 | File identifier |
| 20–23 | `CHUNK_INDEX` | `uint32` | 4 | Logical chunk index |
| 24–27 | `FRAGMENT_OFFSET` | `uint32` | 4 | Offset within logical chunk |
| 28–29 | `REQUESTED_LENGTH` | `uint16` | 2 | Requested data length |
| **Total** | | | **30** | |

`REQUESTED_LENGTH` must be greater than zero. The requested range must remain within the logical chunk and file bounds.

### 8.17 CHUNK_DATA payload — message type `0x10`

Purpose: return the requested fragment.

| Payload offset | Field | Type | Size | Meaning |
|---:|---|---|---:|---|
| 0–3 | `TRANSFER_ID` | `uint32` | 4 | Matches the request |
| 4–19 | `FILE_ID` | 16-byte array | 16 | File identifier |
| 20–23 | `CHUNK_INDEX` | `uint32` | 4 | Logical chunk index |
| 24–27 | `FRAGMENT_OFFSET` | `uint32` | 4 | Offset within logical chunk |
| 28–29 | `DATA_LENGTH` | `uint16` | 2 | Number of following data bytes |
| 30 onward | `DATA` | byte array | `DATA_LENGTH` | File fragment bytes |
| **Total** | | | **30 + DATA_LENGTH** | |

The payload length must equal `30 + DATA_LENGTH`. The receiver must validate that the data range does not exceed the logical chunk or file length. With a 1200-byte maximum datagram, a 16-byte base header, and no extensions, `DATA_LENGTH` must be at most 1154 bytes.

### 8.18 TRANSFER_CANCEL payload — message type `0x11`

| Payload offset | Field | Type | Size |
|---:|---|---|---:|
| 0–3 | `TRANSFER_ID` | `uint32` | 4 |
| 4–19 | `FILE_ID` | 16-byte array | 16 |
| 20–23 | `CHUNK_INDEX` | `uint32` | 4 |
| 24–25 | `REASON_CODE` | `uint16` | 2 |
| **Total** | | | **26** |

### 8.19 ERROR_MSG payload — message type `0x12`

| Payload offset | Field | Type | Size |
|---:|---|---|---:|
| 0–1 | `ERROR_CODE` | `uint16` | 2 |
| 2 | `RELATED_MESSAGE_TYPE` | `uint8` | 1 |
| 3–4 | `DETAIL_LENGTH` | `uint16` | 2 |
| 5 onward | `DETAIL` | UTF-8 byte array | `DETAIL_LENGTH` |
| **Total** | | | **5 + DETAIL_LENGTH** |

### 8.20 Full datagram example: CHAT "Hello"

This example is a complete UDP application datagram with no optional header extensions. The header is exactly 16 bytes and the CHAT payload is exactly 11 bytes.

Header values:

```text
MAGIC         = D7
VERSION       = 1
MESSAGE_TYPE  = CHAT = 09
PEER_ID       = 3
FLAGS         = ACK_VALID | RELIABLE = 03
SESSION_ID    = 12345678
PACKET_SEQ    = 0102
ACK_BASE      = 0002
ACK_BITMAP    = 00000006
EXT_LEN_WORDS = 00
```

CHAT payload values:

```text
CHAT_SEQUENCE = 0000002A
TEXT_LENGTH   = 0005
TEXT          = "Hello" = 48 65 6C 6C 6F
```

Complete datagram, byte offsets from the first UDP application byte:

```text
Offset  Hex bytes
------  ------------------------------------------------
00      D7
01      29
02      33
03      12
04      34
05      56
06      78
07      01
08      02
09      00
10      02
11      00
12      00
13      00
14      06
15      00
16      00
17      00
18      00
19      2A
20      00
21      05
22      48
23      65
24      6C
25      6C
26      6F
```

Same datagram as one hex string:

```text
D7 29 33 12 34 56 78 01 02 00 02 00 00 00 06 00
00 00 00 2A 00 05 48 65 6C 6C 6F
```

Interpretation:
- Bytes 0–15 are the common header.
- Byte 1 `0x29` = `(1 << 5) | 0x09`.
- Byte 2 `0x33` = `(3 << 4) | 0x03`.
- Bytes 3–6 are Session ID `0x12345678`.
- Bytes 7–8 are packet sequence `0x0102`.
- Bytes 9–10 are ACK base `0x0002`.
- Bytes 11–14 are ACK bitmap `0x00000006`.
- Byte 15 is zero, so there are no extensions.
- Bytes 16–19 are Chat Sequence `42`.
- Bytes 20–21 are text length `5`.
- Bytes 22–26 are UTF-8 `Hello`.

Every team member must make their C++ packer produce this exact byte sequence and make their unpacker recover the same field values.


## 9. File distribution and fault tolerance

### 9.1 Logical chunking and replication

- Split each shared file into fixed-size logical chunks.
- A practical initial logical chunk size is 256 KiB; this is a tunable application setting, not a UDP datagram size.
- Every logical chunk must be stored on at least two different peers.
- No single peer should store the complete file.
- The replica placement algorithm must ensure that the two copies are on different peers.
- The manifest records file metadata and chunk hashes; the membership/availability system records which peers have each chunk.

Replication factor 2 is the minimum baseline. Higher replication can be considered if storage and bandwidth allow.

### 9.2 Multi-source download

- The downloader obtains the manifest and chunk availability information.
- It schedules rare chunks first.
- It downloads from at least two peers in parallel when at least two sources are available.
- Each requested fragment is associated with a transfer ID, file ID, chunk index, and byte offset.
- The downloader tracks received ranges and avoids treating duplicate data as new data.
- Verify each complete logical chunk against its SHA-256 hash before marking it complete.

### 9.3 Source failure during download

If a source becomes unavailable:

1. Detect timeout or membership departure.
2. Mark that source as unavailable for the current transfer.
3. Find another peer advertising the required chunk.
4. Reissue the missing fragment request to the alternate peer.
5. Continue assembling the same logical chunk.
6. Verify the completed chunk hash.

The transfer manager handles source failover. It does not require a special common-header field.

### 9.4 File integrity

- Verify every logical chunk using its manifest SHA-256 hash.
- After all chunks are assembled, verify the complete file hash.
- Do not mark a file complete until the full-file hash matches.
- A failed hash check should cause the affected chunk to be downloaded again from another replica.

### 9.5 UDP fragmentation policy

Do not rely on IP fragmentation for large file data. Use application-level fragments carried in separate UDP datagrams.

A conservative initial maximum UDP application datagram size is **1200 bytes**, including the complete protocol header. This reduces the chance of IP fragmentation on common networks.

With a 16-byte base header and no extensions:

```text
Maximum datagram size       = 1200 bytes
Base header                 =   16 bytes
Maximum message payload     = 1184 bytes
CHUNK_DATA metadata         =   30 bytes
Maximum fragment data       = 1154 bytes
```

If header extensions are present, subtract their length as well. The logical chunk size (for example, 256 KiB) is independent of the per-datagram fragment size.

---

## 10. Discovery, join, leave, and failure flow

### 10.1 Discovery flow

1. A new peer starts and creates a Peer UUID and Session ID.
2. It sends DISCOVER using Peer ID `0`.
3. Existing peers reply with DISCOVER_REPLY.
4. The new peer selects a reachable member and sends JOIN_REQUEST.
5. Membership information is exchanged.
6. A free Peer ID slot is assigned using the membership conflict-resolution procedure.
7. The joining peer receives PEER_LIST and JOIN_ACCEPT.
8. Existing peers receive JOIN_ANNOUNCE.
9. The new peer refreshes manifests and chunk availability.

### 10.2 Graceful leave

1. Peer sends LEAVE to active members.
2. Members remove the peer from active membership after validating the sender/session.
3. Chunk availability is updated.
4. If a chunk drops below the required replica count, the implementation may schedule re-replication.

### 10.3 Crash detection

- Peers periodically exchange HEARTBEAT messages.
- Use a timeout based on several heartbeat intervals, not a single missed heartbeat.
- On timeout, mark the peer suspected, then unavailable after the configured failure policy.
- Remove its advertised availability from scheduling decisions.
- A later peer restart is recognized by a new Session ID.

---

## 11. Recommended message sequences

### 11.1 Group chat

```text
Peer A                         Peer B
  |                              |
  |---- CHAT (reliable) -------->|
  |<--- ACK / piggyback ACK -----|
  |                              |
```

For a group of peers, A sends a separate unicast CHAT packet to every other active peer. The Chat Sequence is common to all recipients; Packet Sequence is independent for each destination.

### 11.2 Reliable packet exchange

```text
Sender                         Receiver
  |                               |
  |---- DATA, seq = n ----------->|
  |                               |
  |<--- ACK_BASE / SACK ----------|
  |                               |
```

If the packet or ACK is lost, retransmission and duplicate detection recover the exchange.

### 11.3 Multi-source chunk download

```text
Downloader          Peer B             Peer C
    |                  |                  |
    |-- CHUNK_REQ ---->|                  |
    |-- CHUNK_REQ ----------------------->|
    |<-- CHUNK_DATA ---|                  |
    |<-------------------- CHUNK_DATA ----|
    |                  |                  |
    | verify chunk hash / assemble        |
```

### 11.4 Source failure and failover

```text
Downloader          Peer B             Peer C
    |                  |                  |
    |-- CHUNK_REQ ---->|                  |
    |                  X  (peer fails)    |
    |                                     |
    |-- CHUNK_REQ ----------------------->|
    |<-------------------- CHUNK_DATA ----|
    | verify and continue                 |
```

---

## 12. C++ serialization requirements

### 12.1 Logical header structure

The C++ structure is an in-memory representation only. Its layout is not the wire format.

```cpp
#include <cstdint>

struct Header {
    uint8_t  version;
    uint8_t  messageType;
    uint8_t  peerID;
    uint8_t  flags;
    uint32_t sessionID;
    uint16_t packetSeq;
    uint16_t ackBase;
    uint32_t ackBitmap;
    uint8_t  extLenWords;
};
```

Constants:

```cpp
constexpr uint8_t MAGIC = 0xD7;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr std::size_t BASE_HEADER_SIZE = 16;
constexpr std::size_t MAX_DATAGRAM_SIZE = 1200;
constexpr uint16_t ARQ_WINDOW_SIZE = 32;
```

### 12.2 Byte offsets

```text
out[0]      = MAGIC
out[1]      = (version << 5) | (messageType & 0x1F)
out[2]      = (peerID << 4) | (flags & 0x0F)
out[3..6]   = SESSION_ID, big-endian
out[7..8]   = PACKET_SEQ, big-endian
out[9..10]  = ACK_BASE, big-endian
out[11..14] = ACK_BITMAP, big-endian
out[15]     = EXT_LEN_WORDS
```

Use explicit shifts or `htons`/`htonl` plus `memcpy` for multi-byte fields. Ensure the implementation includes the appropriate platform headers. Do not send `sizeof(Header)` bytes as a packet header.

### 12.3 Validation requirements

Before accepting a datagram:

1. Check that the received length is at least 16 bytes.
2. Check MAGIC.
3. Check supported protocol version.
4. Check message type is assigned or handle it as an unknown type.
5. Check Peer ID is in the permitted range for that message.
6. Check reserved Peer ID values are rejected.
7. Check the complete extension length is present.
8. If extensions are unsupported, require `EXT_LEN_WORDS == 0`.
9. Check message payload length and all message-specific fields.
10. Check the datagram does not exceed the configured maximum.
11. Validate sender endpoint and session against membership state before accepting state-changing messages.

Use a receive method that can detect datagram truncation (for example, `recvmsg()` with truncation reporting) or allocate a buffer large enough for the maximum UDP datagram and reject lengths above the protocol's configured maximum.

---

## 13. Security and trust considerations

This project has basic network-security learning objectives. The baseline protocol is not a complete secure protocol.

At minimum:

- Validate every received field and length.
- Do not trust a Peer ID alone; validate Peer UUID, Session ID, and observed endpoint.
- Reject malformed or out-of-window sequence numbers.
- Limit the rate of discovery, join, and error messages to reduce accidental flooding.
- Do not write file fragments outside the intended file/chunk bounds.
- Verify file hashes before accepting stored or downloaded content.
- Treat filenames as untrusted input; prevent path traversal when saving files.
- Do not execute received files automatically.

Optional future security work:

- Authenticate peers using a shared group secret or public-key identities.
- Encrypt chat and file-transfer payloads.
- Add message authentication (for example, an authenticated-encryption construction) rather than inventing a custom cipher.
- Add replay protection tied to peer sessions and message sequence numbers.

The 32-bit Session ID is not an authentication mechanism. Hashes detect accidental or malicious content changes only if the manifest itself is trusted; they do not authenticate the sender.

---

## 14. Configuration values

The following are recommended initial values. They should be measured and tuned during testing.

| Parameter | Initial value | Notes |
|---|---:|---|
| Maximum active peers | 10 | Project requirement |
| Minimum active peers for demo | 4 | Project demonstration requirement |
| Base header size | 16 bytes | Fixed |
| ARQ window | 32 packets | Must match 32-bit SACK bitmap |
| Packet sequence | 16 bits | Modular arithmetic |
| Session ID | 32 bits | Random per process start |
| Logical file chunk size | 256 KiB | Tunable |
| Maximum UDP datagram | 1200 bytes | Conservative initial cap |
| Replication factor | 2 | Minimum required |
| Parallel download sources | At least 2 | When available |
| File hash | SHA-256 | Per chunk and complete file |
| Chat sequence | 32 bits | Per sender/session |

Timeouts, heartbeat interval, retransmission timeout, and retry limits should be selected through measurement and documented in the final report. Do not treat the suggested values as universal constants.

---

## 15. Testing and interoperability checklist

All team members must use the same field widths, byte offsets, message type values, byte order, and bitmap interpretation.

### 15.1 Header tests

- [ ] Header serializes to exactly 16 bytes when `EXT_LEN_WORDS = 0`.
- [ ] Golden test vector below matches byte-for-byte.
- [ ] Version and message type are packed/unpacked correctly.
- [ ] Peer ID and flags are packed/unpacked correctly.
- [ ] SESSION_ID, PACKET_SEQ, ACK_BASE, and ACK_BITMAP use big-endian order.
- [ ] Non-zero extension length is correctly parsed or rejected if unsupported.
- [ ] Short headers, bad magic, unsupported versions, and invalid Peer IDs are rejected.
- [ ] Unknown message types are handled safely.

### 15.2 Reliability tests

- [ ] Normal delivery.
- [ ] Packet loss and retransmission.
- [ ] ACK loss and duplicate re-ACK.
- [ ] Duplicate packet delivery.
- [ ] Out-of-order packet delivery.
- [ ] SACK bitmap interpretation.
- [ ] Sequence wrap-around (`65535 -> 0`).
- [ ] Sender never exceeds the 32-packet window.
- [ ] Independent A→B and B→A sequence/ACK state.
- [ ] Chat messages delivered once and in per-sender order.

### 15.3 Membership tests

- [ ] Four or more peers discover one another.
- [ ] Peer joins while others are active.
- [ ] Two peers attempt to claim the same slot.
- [ ] Graceful LEAVE updates membership.
- [ ] Crashed peer is detected after timeout.
- [ ] Restarted peer receives a new Session ID.
- [ ] Stale packets from an old session are rejected.

### 15.4 File-sharing tests

- [ ] At least two files of at least 2 MB are shared.
- [ ] Every chunk has at least two replicas on different peers.
- [ ] No peer stores the complete file.
- [ ] Downloader obtains manifest and availability information.
- [ ] Rarest-first scheduling is observed.
- [ ] At least two sources serve chunks in parallel.
- [ ] Duplicate fragments do not corrupt the output.
- [ ] Chunk hash and whole-file hash pass.
- [ ] A source is terminated during download and the downloader switches to another replica.
- [ ] Missing replica data is detected and handled.

### 15.5 Interoperability rule

Each team member should compile a small independent header test using the shared protocol definitions. The serialized output must match the golden vector exactly. Do not integrate modules until all members pass the same test.

---

## 16. Golden header test vector

Input values:

```text
MAGIC         = 0xD7
VERSION       = 1
MESSAGE_TYPE  = CHAT (0x09)
PEER_ID       = 3
FLAGS         = ACK_VALID | RELIABLE = 0x03
SESSION_ID    = 0x12345678
PACKET_SEQ    = 0x0102
ACK_BASE      = 0x0002
ACK_BITMAP    = 0x00000006
EXT_LEN_WORDS = 0
```

Expected 16-byte header:

```text
D7 29 33 12 34 56 78 01 02 00 02 00 00 00 06 00
```

Byte-by-byte interpretation:

| Offset | Hex | Meaning |
|---:|---:|---|
| 0 | `D7` | MAGIC |
| 1 | `29` | Version 1, CHAT type 9 |
| 2 | `33` | Peer ID 3, flags 3 |
| 3–6 | `12 34 56 78` | Session ID |
| 7–8 | `01 02` | Packet sequence |
| 9–10 | `00 02` | ACK base |
| 11–14 | `00 00 00 06` | ACK bitmap |
| 15 | `00` | No extensions |

Every implementation must produce these exact bytes for the test input.

---

## 17. Header-size decision and alternatives

The selected base header is **16 bytes**:

- Previous compact proposal: 16 bytes.
- Added one byte: `EXT_LEN_WORDS`.
- New fixed base header: 16 bytes.
- The added byte provides a controlled extension mechanism while preserving 4-byte alignment.

The current design uses a 32-bit SACK bitmap and therefore a 32-packet ARQ window.

If the team later decides that a 64-packet ARQ window is necessary, the protocol would need a 64-bit ACK bitmap. That change adds 4 bytes and must be introduced as a new negotiated protocol version or explicitly negotiated extension; do not silently reinterpret the version 1 header.

Other future additions should normally go into message payloads or negotiated header extensions rather than increasing the base header:

- 64-bit Session ID.
- Authentication tag or key identifier.
- Capability negotiation.
- Trace/correlation identifier.
- Additional reliability or priority metadata.
- Optional timestamps.

Do not add fields “just in case” without a concrete requirement. Extra fields increase parsing complexity and can create compatibility problems.

---

## 18. Implementation responsibilities by module

| Module | Main responsibilities |
|---|---|
| Header/Codec | Serialize, parse, validate the 16-byte base header and extensions |
| UDP Transport | Socket setup, bind, send/receive datagrams, endpoint management |
| Peer Manager | Discovery, slot assignment, join/leave, active peer list, session validation |
| Reliability Manager | Per-peer directional ARQ state, ACK/SACK, retransmission, duplicate filtering |
| Chat Manager | Chat sequence, per-sender ordering, buffering, group fan-out |
| File Manager | File metadata, manifests, logical chunking, hashing |
| Chunk/Replica Manager | Replica placement, availability bitmap, rarest-first scheduling |
| Transfer Manager | Parallel downloads, fragment tracking, timeout, source failover |
| UI/CLI | User interaction and display of peers, chat, files, and transfer progress |

Use one main UDP receive loop to parse incoming datagrams and dispatch validated messages to the appropriate module/worker queue. Avoid multiple unrelated threads reading from the same socket without a clear synchronization and dispatch design.

---

## 19. Change-control policy

This document is the shared protocol contract for all team members.

Before changing any wire-format detail:

1. Update this `protocol.md`.
2. Explain why the change is needed.
3. Check compatibility with every module.
4. Update message diagrams and test vectors.
5. Update the shared C++ header/codec.
6. Add or update interoperability tests.
7. Record whether the change is backward-compatible.

Never independently change a field size, byte order, message type, sequence rule, or bitmap interpretation in one module.

---

## 20. Final baseline summary

- **Transport:** UDP.
- **Topology:** Full-mesh unicast for chat and file transfers.
- **Discovery:** LAN multicast/broadcast; optional bootstrap assistance.
- **Peers:** 4–10 active peers; Peer IDs 1–10, ID 0 for discovery only.
- **Base header:** 16 bytes, multiple of 4; byte 15 is `EXT_LEN_WORDS`.
- **Payload layouts:** exact payload-relative offsets and sizes are defined in Section 8, including a complete CHAT hex test vector.
- **Future extension support:** `EXT_LEN_WORDS` at byte 15; optional padded TLV extension area.
- **Byte order:** Big-endian.
- **Reliability:** Selective Repeat ARQ, 16-bit sequence, 32-packet window, cumulative ACK + 32-bit SACK bitmap.
- **Chat ordering:** Separate 32-bit per-sender Chat Sequence.
- **File sharing:** Fixed logical chunks, replication factor at least 2, rarest-first, multi-source downloads.
- **Fault tolerance:** Switch to another replica when a source fails.
- **Integrity:** SHA-256 per chunk and whole file.
- **Link layer:** No custom link-layer ARQ.
- **Future compatibility:** Use negotiated extensions or a new protocol version; do not silently change version 1 field meanings.

