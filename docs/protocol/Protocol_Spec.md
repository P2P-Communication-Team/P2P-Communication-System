# P2P UDP Communication Protocol

**Version:** 1.0  
**Protocol Type:** Application-Layer Protocol  
**Transport:** UDP  
**Header Size:** 16 Bytes  
**Byte Order:** Big-Endian (Network Byte Order)

## 1. Overview

This document describes a lightweight application-layer protocol designed for decentralized Peer-to-Peer (P2P) communication over UDP.

The protocol supports direct communication between peers without requiring a permanent central server. It provides a common packet format for peer discovery, membership management, group messaging, and distributed file sharing.

UDP provides fast, connectionless communication but does not guarantee packet delivery, ordering, or duplicate protection. Therefore, this protocol implements **Selective Repeat ARQ (SR-ARQ)** at the application layer for reliable message delivery.

### Main Features

- Decentralized peer-to-peer communication.
- Lightweight 16-byte packet header.
- Variable number of active peers.
- Independent communication streams.
- Reliable delivery using Selective Repeat ARQ.
- Cumulative acknowledgements and SACK bitmap.
- Packet corruption detection using CRC-16.
- Support for chat and chunk-based file transfer.
- Sequence number wraparound handling.

---

## 2. Common Packet Header

Every packet starts with a fixed-size **16-byte header**, followed by an optional payload.

The header contains the information required to identify the message, determine its size, validate its integrity, and manage reliable communication.

### 2.1 Header Layout

```text
  Byte:    0          1          2-3        4-5        6-7       8-11       12-15
        +----------+----------+----------+----------+----------+----------+----------+
        | Version  | Message  | Stream   | Payload  |  CRC-16  | Sequence |   ACK    |
        | + Flags  |   Type   |   ID     | Length   |          | Number   | Number  |
        +----------+----------+----------+----------+----------+----------+----------+
          1 Byte     1 Byte    2 Bytes    2 Bytes    2 Bytes    4 Bytes    4 Bytes

                                Total = 16 Bytes
```

### 2.2 Header Fields

| Field           | Size    | Description                                        |
|-----------------|---:-----|----------------------------------------------------|
| Version + Flags | 1 byte  | Protocol version and packet control flags          |
| Message Type    | 1 byte  | Identifies the type of message                     |
| Stream ID       | 2 bytes | Identifies an independent communication stream     |
| Payload Length  | 2 bytes | Number of bytes following the header               |
| CRC-16          | 2 bytes | Detects accidental packet corruption               |
| Sequence Number | 4 bytes | Identifies a reliable data packet                  |
| ACK Number      | 4 bytes | Carries the cumulative acknowledgement in DATA_ACK |

**Important rules:**

- All multi-byte fields use Big-Endian byte order.
- The header is always exactly 16 bytes.
- A packet without a payload still contains the complete header.
- The complete datagram length must equal `16 + payload_length`.
- Sequence numbers are meaningful only when the RELIABLE flag is set.
- ACK numbers are meaningful only in DATA_ACK messages.
- Fields that are not applicable to a message must be transmitted as zero.

---

## 3. Version and Flags

The first header byte is divided into two parts:

```text
  Bit:    7  6 | 5  4  3  2  1  0
        +------+-----------------+
        | VER  |      FLAGS      |
        +------+-----------------+
          2 bits      6 bits
```

### 3.1 Protocol Version

The upper two bits represent the protocol version.

- Current version: `01` (Version 1).
- Supported version values: 0–3.
- A receiver must reject unsupported versions.

### 3.2 Flags

The lower six bits control packet behavior.

| Mask   | Flag     | Description                                       |
|--------|----------|---------------------------------------------------|
| `0x20` | RELIABLE | Packet uses Selective Repeat reliability          |
| `0x10` | RETX     | Packet is a retransmission                        | 
| `0x08` | FIN      | Indicates the final packet of a message or stream |
| `0x04` | PRIV     | Indicates a private chat message                  |
| `0x02` | ENC      | Indicates encrypted payload data                  |
| `0x01` | RSV      | Reserved; must always be zero                     |

**Flag rules:**

- RETX is valid only when RELIABLE is set.
- RELIABLE must be set for message types defined as reliable.
- RELIABLE must not be set for unreliable-only message types.
- The reserved bit must always be zero.
- ACK validity is determined by the message type; no separate ACK-valid flag is required.

ACK-only messages are not acknowledged, preventing ACK loops.

---

## 4. CRC-16 Error Detection

CRC-16 is used to detect accidental corruption during packet transmission.
 
The protocol uses **CRC-16/CCITT-FALSE**.

| Parameter         | Value                         |
|-------------------|-------------------------------|
| Polynomial        | `0x1021`                      |
| Initial Value     | `0xFFFF`                      |
| Input Reflection  | False                         |
| Output Reflection | False                         |
| XOR Output        | `0x0000`                      |
| Test Vector       | `CRC16("123456789") = 0x29B1` |

### CRC Calculation

1. Set the CRC field in the header to zero.
2. Calculate CRC over the complete header and payload.
3. Insert the calculated CRC into the header.
4. At the receiver, temporarily treat the CRC field as zero and calculate CRC again.
5. Compare the calculated value with the received CRC.

If the values differ, the packet is rejected.

**Note:** CRC detects accidental corruption. It does not provide encryption, authentication, or protection against malicious modification.

---

## 5. Packet Size and Payload

The protocol uses a maximum payload size of 1408 bytes.

| Component            | Size       |
|----------------------|------------|
| Common Header        | 16 bytes   |
| Maximum Payload      | 1408 bytes |
| Maximum UDP Datagram | 1424 bytes |

For file transfers, a CHUNK_DATA payload contains:

- File ID: 4 bytes
- Chunk Index: 4 bytes
- File Data: up to 1400 bytes

### MTU Considerations

The maximum 1424-byte datagram fits within a standard 1500-byte MTU:

- IPv4: 1424 + 8-byte UDP header + 20-byte IPv4 header = 1452 bytes.
- IPv6: 1424 + 8-byte UDP header + 40-byte IPv6 header = 1472 bytes.

However, VPNs, tunnels, and some wireless networks may use smaller MTUs.

For better compatibility:

- Default file chunk size: 1024 bytes.
- Maximum file data in one CHUNK_DATA payload: 1400 bytes.
- Maximum datagram size can be reduced through configuration.

The reliability layer does not fragment application messages. Applications must divide large data into smaller messages.

---

## 6. Message Types

The Message Type field identifies the purpose of a packet.

### 6.1 Message Registry

| Value | Message Type  | Delivery   | Purpose                                         |
|-------|---------------|------------|-------------------------------------------------|
| 1     | DISCOVER      | Unreliable | Discover available peers                        |
| 2     | JOIN          | Unreliable | Request to join the network                     |
| 3     | JOIN_ACK      | Unreliable | Accept or reject a join request                 | 
| 4     | LEAVE         | Unreliable | Notify peers before leaving                     |
| 5     | PEER_LIST     | Either     | Share available peer information                |
| 6     | HEARTBEAT     | Unreliable | Indicate that a peer is active                  |
| 7     | CHAT_MSG      | Reliable   | Send chat messages                              |
| 8     | DATA_ACK      | Unreliable | Acknowledge reliable packets                    |
| 9     | FILE_LIST_REQ | Reliable   | Request the available file list                 |
| 10    | FILE_MANIFEST | Reliable   | Send file information                           |
| 11    | CHUNK_HAVE    | Reliable   | Advertise available file chunks                 |
| 12    | CHUNK_REQ     | Reliable   | Request a particular chunk                      |
| 13    | CHUNK_DATA    | Reliable   | Transfer file chunk data                        |
| 14    | CHUNK_STORE   | Reliable   | Request another peer to become a replica holder |
| 15    | CHUNK_NAK     | Reliable   | Reject a chunk request with a reason            |

### 6.2 Payload Formats

**MemberInfo — 9 bytes**

| Field       | Size    |
|-------------|---------| 
| Peer ID     | 2 bytes |
| UDP Port    | 2 bytes |
| Status      | 1 byte  |
| Incarnation | 4 bytes |

The incarnation value changes when a peer process restarts. It helps distinguish a restarted peer from its previous session.

**PEER_LIST**

```text
peer_count (2 bytes)
+
N × Peer Entry (8 bytes)
```

Each peer entry contains:

| Field        | Size    |
|--------------|---------|
| Peer ID      | 2 bytes |
| IPv4 Address | 4 bytes |
| UDP Port     | 2 bytes |

Maximum entries in one packet: 175.

**FILE_MANIFEST**

```text
file_id (4)
file_size (8)
chunk_size (2)
name_length (2)
file_name (variable)
sha256 (32)
```

The SHA-256 value represents the complete file's hash and can be used to verify the reconstructed file.

**CHUNK_HAVE**

```text
file_id (4)
first_chunk (4)
bitmap (1–1400 bytes)
```

Each bitmap bit indicates whether the peer stores a particular chunk.

- Bit 0 represents `first_chunk`.
- Bit 1 represents `first_chunk + 1`.
- Bits are stored least-significant-bit first within each byte.

**CHUNK_REQ / CHUNK_STORE / CHUNK_NAK**

These messages use the following common fields:

- File ID: 4 bytes
- Chunk Index: 4 bytes

CHUNK_NAK additionally includes a one-byte reason:

| Reason | Meaning                        |
|--------|--------------------------------|
| 1      | File does not exist            |
| 2      | Requested chunk is unavailable |
| 3      | Peer is busy                   |

HEARTBEAT and FILE_LIST_REQ have empty payloads.

---

## 7. Peers and Communication Streams

### 7.1 Peer Identification

The common header does not contain a sender Peer ID.

The sender is identified through the UDP source endpoint (IP address and port). The membership layer maintains the association between network endpoints and peer identities.

Messages are sent directly between their originating peers. Group communication is implemented by sending a separate unicast packet to each intended peer.

### 7.2 Stream Identification

The Stream ID identifies an independent communication flow.
---------------------------------------------------
| Stream ID    | Purpose                           |
|--------------|-----------------------------------|
| 0            | Control traffic (unreliable only) |
| 1            | Chat                              |
| 2            | File metadata                     |
| 16 and above | Individual file-transfer streams  |

Each reliable stream maintains its own sequence numbers, send window, receive window, and acknowledgement state.

Reliability state is maintained independently for each:

`(Peer, Stream ID, Direction)`

For example, A → B and B → A have separate reliability states.

Stream IDs must not be reused within the same session unless the stream is explicitly reset on both sides.

---

## 8. Reliable Delivery — Selective Repeat ARQ

UDP does not guarantee delivery or packet ordering. The protocol implements Selective Repeat ARQ to provide reliable delivery.

### 8.1 Sequence Numbers

- Sequence number size: 32 bits.
- Sequence numbers count packets, not bytes.
- Initial sequence number: 1.
- Sequence numbers wrap modulo \(2^{32}\).
- Sequence numbers are maintained independently for every peer and stream direction.

All comparisons must use modular serial-number arithmetic rather than ordinary integer comparisons.

The 32-packet window is safely smaller than half the 32-bit sequence-number space, avoiding ambiguity between old and new packets.

### 8.2 Acknowledgement Convention

The ACK number represents the **highest consecutively received sequence number**.

For example:

If packets 1, 2, 3, and 5 have arrived, but packet 4 is missing:

```text
ACK Number = 3
```

The receiver has received packets 1–3 in order and packet 5 out of order.

The receiver is waiting for packet 4.

Initially:

```text
ACK Number = 0
Next Expected Packet = 1
```

### 8.3 SACK Bitmap

The DATA_ACK payload contains a 32-bit Selective Acknowledgement bitmap.

It reports packets received out of order beyond the cumulative ACK.

| Bitmap Bit | Represents |
|------------|----------- |
| Bit 0      | ACK + 1    |
| Bit 1      | ACK + 2    |
| Bit 2      | ACK + 3    |
| ...        | ...        |
| Bit 31     | ACK + 32   |

Interpretation:

- `1` = Packet received and buffered.
- `0` = Packet missing or not received.
- Bit 0 must always be zero because ACK already represents the highest consecutively received packet.

The receive window contains exactly 32 positions:

```text
[ACK + 1, ACK + 32]
```

Example:

```text
ACK Number = 3

Received packets:
4, 5, 7

Packet 6 is missing.

SACK:
Bit 0 = 0   (Packet 4)
Bit 1 = 0   (Packet 5)
Bit 2 = 0   (Packet 6)
Bit 3 = 1   (Packet 7)
```

The receiver advances the ACK when the missing next packet arrives. The bitmap shifts right by the number of positions advanced.

If the ACK advances by 32 or more positions, the bitmap becomes zero.

The sender uses ACK and SACK together to identify which packets have been received and which still require retransmission.

### 8.4 Sender Behavior

The sender:

1. Assigns a sequence number to every reliable packet.
2. Stores the packet in its send buffer.
3. Transmits the packet.
4. Waits for DATA_ACK.
5. Processes cumulative ACK and SACK information.
6. Marks acknowledged packets.
7. Releases acknowledged buffer slots.
8. Retransmits only missing or unacknowledged packets after timeout.
9. Sends queued packets when window space becomes available.

The maximum send window is 32 packets and can be configured between 1 and 32.

The ring buffer has exactly 32 slots.

### 8.5 Receiver Behavior

For each incoming reliable packet:

- If it is already received or delivered, treat it as a duplicate and send the current ACK again.
- If it falls inside the receive window, buffer it if it has not already been buffered.
- Update the SACK bitmap.
- Deliver consecutive packets in order.
- Advance the cumulative ACK.
- Shift the bitmap when the ACK advances.
- If it is outside the receive window, discard it.

The application must never receive the same reliable packet twice.

### 8.6 Retransmission Timeout

Each unacknowledged packet has its own retransmission timer.

The protocol uses Jacobson/Karels RTT estimation:

\[
RTO = SRTT + 4 \times RTTVAR
\]

Default parameters:
-------------------------------------
| Parameter               | Value   |
|-------------------------|---------|
| Initial RTO             | 200 ms  |
| Minimum RTO             | 50 ms   |
| Maximum RTO             | 2000 ms |
| Alpha                   | 1/8     |
| Beta                    | 1/4     |
| Maximum Retransmissions | 8       |

After each timeout, the retransmission timeout is exponentially increased, up to the configured maximum.

**Karn's Algorithm:** RTT samples are collected only from packets that have never been retransmitted. This avoids ambiguous RTT measurements.

After the maximum retransmission count is reached, the peer is reported as unreachable and the affected stream receives a delivery-failure notification.

### 8.7 ACK Behavior

DATA_ACK messages are unreliable and are never acknowledged.

- Every accepted reliable data packet receives a DATA_ACK.
- Duplicate reliable packets receive the current DATA_ACK again.
- ACK-only packets do not trigger ACK responses.
- ACK information is not piggybacked on data packets.

This prevents acknowledgement loops.

---

## 9. Peer Sessions and Failure Handling

Each peer's reliability state is created when it is registered with the communication system.

When a peer leaves or becomes unavailable:

- Its reliability state is removed.
- Pending retransmissions are stopped.
- Outstanding messages are reported as failed.
- Associated stream buffers are cleaned up.

When a peer restarts, it announces a new incarnation value. Its previous reliability state must be reset.

The common header does not contain a session identifier. Therefore, a delayed packet from an old session could theoretically be accepted after a restart if its sequence number falls inside the new receive window. This is a known limitation of the current header design.

---

## 10. File Transfer and Replication

Large files are divided into smaller chunks and transferred through reliable streams.

The file-sharing application is responsible for:

- Selecting chunk sources.
- Maintaining chunk availability information.
- Distributing chunks between peers.
- Maintaining multiple replicas.
- Selecting another replica when a source becomes unavailable.
- Reconstructing and verifying completed files.

The reliability layer is responsible for:

- Reliable packet delivery.
- Retransmission of missing packets.
- Duplicate protection.
- Correct packet ordering within a stream.
- Reporting delivery failures.

These three operations must remain separate:

1. **Transport retransmission:** Resending a lost UDP packet.
2. **Application chunk retry:** Requesting a chunk from another peer.
3. **Chunk replication:** Maintaining copies of chunks on different peers.

An acknowledged packet only confirms that the remote reliability engine received it. It does not prove that the file chunk has been permanently stored or replicated.

---

## 11. Packet Validation

Every received datagram must be validated before application delivery.

Recommended validation order:

1. Reject empty datagrams.
2. Reject datagrams shorter than 16 bytes.
3. Validate protocol version.
4. Validate payload length.
5. Ensure the actual datagram length equals `16 + payload_length`.
6. Verify CRC-16.
7. Validate message type.
8. Validate flags.
9. Validate stream ID.
10. Validate the payload size and format for the message type.

Malformed packets must be discarded safely.

The receiver must not:

- Read beyond the received buffer.
- Accept an incorrect payload length.
- Accept invalid reserved flags.
- Accept an invalid SACK bitmap.
- Process ACKs for packets that were never sent.
- Create reliability state for unknown peers.

Invalid packets are dropped without generating an ACK.

---

## 12. Design Limitations and Security Notes

This protocol is designed for application-level P2P communication over UDP. It is not intended to replace a standardized transport protocol such as TCP or QUIC.

Current design limitations:

- CRC-16 detects accidental corruption but does not authenticate peers.
- The ENC flag indicates encrypted payloads, but encryption requires a separately defined secure encryption and authentication mechanism.
- Peer identity and endpoint validation depend on the membership system.
- The protocol does not implement IP fragmentation.
- A peer failure can cause delivery failure; application-level retry and replica selection are handled by the file-sharing layer.
- The protocol's 16-byte header does not carry a session identifier, leaving a small risk from delayed packets belonging to an earlier peer session.

These limitations should be considered when extending the protocol.

---

## 13. Summary

The protocol uses a compact 16-byte header to support decentralized communication over UDP.

Its main design choices are:

| Feature               | Design                          |
|-----------------------|---------------------------------|
| Transport             | UDP                             |
| Architecture          | Peer-to-Peer                    |
| Common Header         | 16 bytes                        |
| Sequence Number       | 32 bits                         |
| ACK Number            | 32 bits                         |
| SACK Bitmap           | 32 bits                         |
| Maximum Window        | 32 packets                      |
| Reliability           | Selective Repeat ARQ            |
| Error Detection       | CRC-16/CCITT-FALSE              |
| Byte Order            | Big-Endian                      |
| Peer Count            | Variable                        |
| Stream Management     | Per peer, stream, and direction |
| File Transfer         | Chunk-based                     |
| Packet Retransmission | Per-packet timeout              |

The design separates packet formatting, peer management, reliable delivery, and file-sharing responsibilities while providing a common communication format for all participating peers.
