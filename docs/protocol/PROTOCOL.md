# P2P UDP Communication Protocol Specification

## 1. Overview

This document defines a simple, generic communication protocol for a
decentralized peer-to-peer (P2P) application that uses UDP for
communication.

The protocol provides a common packet format for peer discovery, group
messaging, and file sharing. Reliable delivery is implemented at the
application layer using Selective Repeat Automatic Repeat reQuest
(Selective Repeat ARQ).

The design is intended for a small-to-medium P2P application in which
peers communicate directly without depending on a permanent central
server or central file storage.

### 1.1 Design Goals

-   Use a compact and consistent packet header.
-   Support different types of P2P messages through a common format.
-   Support multiple communication streams between peers.
-   Provide reliable and ordered delivery where required.
-   Detect corrupted packets using CRC-16.
-   Support peer discovery, group communication, and chunk-based file
    sharing.
-   Allow the number of active peers to grow dynamically.

### 1.2 Protocol Properties

  Property                    Description
  --------------------------- ----------------------------------------
  Network transport           UDP
  Communication model         Decentralized peer-to-peer
  Reliability                 Application-layer Selective Repeat ARQ
  Byte order                  Big-endian
  Common header size          16 bytes
  Maximum UDP datagram size   1424 bytes
  Maximum payload size        1408 bytes

The maximum datagram size includes the 16-byte common header and the
payload.

------------------------------------------------------------------------

## 2. Common Packet Header

Every protocol packet begins with the same 16-byte header. The message
type determines how the payload is interpreted.

### 2.1 Header Layout

  ------------------------------------------------------------------------
  Field                                         Size Description
  --------------------- ---------------------------- ---------------------
  Version + Flags                             1 byte Protocol version and
                                                     packet flags

  Message Type                                1 byte Identifies the packet
                                                     type

  Stream ID                                  2 bytes Identifies the
                                                     communication stream

  Payload Length                             2 bytes Payload size in bytes

  CRC-16                                     2 bytes Detects packet
                                                     corruption

  Sequence Number                            4 bytes Identifies a reliable
                                                     data packet

  ACK Number                                 4 bytes Carries
                                                     acknowledgement
                                                     information when
                                                     applicable

  **Total**                             **16 bytes** **Fixed header size**
  ------------------------------------------------------------------------

All multi-byte header fields are transmitted in **big-endian** byte
order.

### 2.2 Version and Flags Byte

The first header byte is divided into two parts:

-   Bits 7--6: Protocol version
-   Bits 5--0: Packet flags

The current protocol version is `01` in binary.

  Bit       Mask Flag       Meaning
  ----- -------- ---------- -------------------------------------------
  5       `0x20` RELIABLE   Packet uses reliable delivery
  4       `0x10` RETX       Packet is a retransmission
  3       `0x08` FIN        Indicates the end of a stream or transfer
  2       `0x04` PRIV       Indicates a private message
  1       `0x02` ENC        Indicates encrypted payload data
  0       `0x01` RSV        Reserved; must be zero

The `RETX` flag is valid only when `RELIABLE` is set.

The `RELIABLE` flag indicates that the sequence number is valid and that
the packet is handled by the reliability mechanism.

### 2.3 Sequence Number

The sequence number identifies a reliable data packet within a specific
stream and direction.

-   Sequence numbers are 32-bit unsigned integers.
-   The initial sequence number is `1`.
-   Sequence numbers wrap around modulo (2\^{32}).
-   Sequence number comparisons must use modular serial-number
    arithmetic.
-   Normal integer comparisons must not be used across wraparound.

Each peer and stream maintains independent sequence-number state.

### 2.4 ACK Number

The ACK number is meaningful in `DATA_ACK` packets.

It represents the highest consecutively received sequence number. For
example, if packets `1`, `2`, and `3` have been received in order, the
ACK number is `3`.

The initial ACK number is `0`, meaning that packet `1` is expected next.

### 2.5 Payload Length

The payload length specifies the number of bytes following the common
header.

The receiver must verify that the actual datagram length matches:

`Header Size + Payload Length`

Packets with inconsistent lengths must be rejected.

### 2.6 CRC-16

CRC-16 is used to detect accidental corruption in the header and
payload.

**CRC-16/CCITT-FALSE parameters:**

  Parameter                       Value
  ------------------------------- ----------
  Polynomial                      `0x1021`
  Initial value                   `0xFFFF`
  Reflected input                 No
  Reflected output                No
  Final XOR                       `0x0000`
  Check value for `"123456789"`   `0x29B1`

During CRC calculation, the CRC field in the header is treated as zero.
The calculation covers the complete header and payload.

A packet with an invalid CRC must be discarded.

------------------------------------------------------------------------

## 3. Message Types

The message type field identifies the purpose and payload format of a
packet.

    Value Message Type    Delivery
  ------- --------------- ------------
        1 DISCOVER        Unreliable
        2 JOIN            Unreliable
        3 JOIN_ACK        Unreliable
        4 LEAVE           Unreliable
        5 PEER_LIST       Either
        6 HEARTBEAT       Unreliable
        7 CHAT_MSG        Reliable
        8 DATA_ACK        Unreliable
        9 FILE_LIST_REQ   Reliable
       10 FILE_MANIFEST   Reliable
       11 CHUNK_HAVE      Reliable
       12 CHUNK_REQ       Reliable
       13 CHUNK_DATA      Reliable
       14 CHUNK_STORE     Reliable
       15 CHUNK_NAK       Reliable

**Reliable** messages use the application-layer Selective Repeat
mechanism. **Unreliable** messages are sent without retransmission by
that mechanism.

`PEER_LIST` may be sent using either delivery mode, depending on the
application requirement.

------------------------------------------------------------------------

## 4. Payload Formats

The common header is followed by a payload whose structure depends on
the message type.

All multi-byte payload fields use big-endian byte order unless stated
otherwise.

### 4.1 Peer Discovery and Membership

#### DISCOVER

Used by a peer to announce its presence or search for other peers.

Payload: No fixed payload is required.

#### JOIN

Used by a peer to request membership in the P2P group.

The membership implementation may include the joining peer's information
in the payload.

#### JOIN_ACK

Used to acknowledge a join request.

The membership implementation may include assigned peer information or
current membership details.

#### LEAVE

Used by a peer to announce that it is leaving the group.

#### HEARTBEAT

Used periodically to indicate that a peer is active.

Payload: Empty.

#### PEER_LIST

Used to share information about known peers.

Payload format:

  Field                    Size Description
  -------------- -------------- -----------------------------
  Peer Count            2 bytes Number of peer entries
  Peer Entries     8 bytes each Information about each peer

Each peer entry has the following structure:

  Field               Size Description
  -------------- --------- -------------------
  Peer ID          2 bytes Peer identifier
  IPv4 Address     4 bytes Peer IPv4 address
  UDP Port         2 bytes Peer UDP port

With a maximum payload of 1408 bytes, one packet can contain up to 175
peer entries.

### 4.2 Group Communication

#### CHAT_MSG

Carries a chat message between peers.

The payload contains the application message data. The application may
define additional fields, such as sender information or message
formatting, if required.

Chat messages use reliable delivery to support ordered message delivery
within the relevant stream.

### 4.3 Acknowledgement

#### DATA_ACK

Acknowledges reliable data packets.

The ACK number in the common header contains the highest consecutively
received sequence number.

The payload contains a 32-bit Selective Acknowledgement (SACK) bitmap.

Payload format:

  Field              Size Description
  ------------- --------- -----------------------------------------
  SACK Bitmap     4 bytes Indicates received out-of-order packets

ACK-only packets are unreliable. They are not acknowledged themselves,
which prevents acknowledgement loops.

### 4.4 File Sharing

#### FILE_LIST_REQ

Requests the list of files available from a peer.

Payload: Empty.

#### FILE_MANIFEST

Provides metadata about a shared file.

Payload format:

  Field                    Size Description
  ------------------ ---------- ---------------------------
  File ID               4 bytes Unique file identifier
  File Size             8 bytes Total file size in bytes
  Chunk Size            2 bytes Size of each file chunk
  File Name Length      2 bytes Length of the file name
  File Name            Variable File name bytes
  SHA-256              32 bytes Hash of the complete file

The fixed-size fields occupy 48 bytes, excluding the file name.

#### CHUNK_HAVE

Advertises which chunks of a file are available at a peer.

Payload format:

  Field                          Size Description
  ------------------- --------------- -----------------------------------
  File ID                     4 bytes Identifies the file
  First Chunk Index           4 bytes Index represented by bitmap bit 0
  Chunk Bitmap          1--1400 bytes Indicates available chunks

Bitmap bits are mapped least-significant-bit first. Bit 0 represents the
first chunk index, bit 1 represents the next chunk index, and so on.

#### CHUNK_REQ

Requests a specific file chunk.

Payload format:

  Field              Size Description
  ------------- --------- -----------------------
  File ID         4 bytes Identifies the file
  Chunk Index     4 bytes Requested chunk index

#### CHUNK_DATA

Carries file chunk data.

Payload format:

  Field               Size Description
  ------------- ---------- ----------------------
  File ID          4 bytes Identifies the file
  Chunk Index      4 bytes Identifies the chunk
  Chunk Data      Variable File data bytes

The maximum chunk data carried in one `CHUNK_DATA` packet is 1400 bytes.

#### CHUNK_STORE

Indicates that a peer should store or retain a specified chunk.

Payload format:

  Field              Size Description
  ------------- --------- ----------------------
  File ID         4 bytes Identifies the file
  Chunk Index     4 bytes Identifies the chunk

#### CHUNK_NAK

Indicates that a requested chunk cannot be provided.

Payload format:

  Field              Size Description
  ------------- --------- ---------------------------------
  File ID         4 bytes Identifies the file
  Chunk Index     4 bytes Identifies the chunk
  Reason           1 byte Explains why the request failed

Reason codes:

    Value Meaning
  ------- -------------------
        1 File not found
        2 Chunk unavailable
        3 Peer busy

------------------------------------------------------------------------

## 5. Reliable Delivery: Selective Repeat ARQ

UDP does not guarantee packet delivery, ordering, or duplicate
protection. Therefore, reliable messages use Selective Repeat ARQ at the
application layer.

The reliability mechanism is responsible for:

-   Detecting missing packets.
-   Retransmitting unacknowledged packets.
-   Detecting duplicate packets.
-   Buffering out-of-order packets.
-   Delivering data to the application in order.
-   Maintaining independent reliability state for each peer and stream.

### 5.1 Send and Receive Windows

The send and receive window size is **32 packets**.

Each stream maintains its own send and receive state. The window must
not be shared globally between unrelated peers or streams.

The receiver accepts packets that fall within its current receive
window. Out-of-order packets are buffered until all earlier packets have
arrived.

### 5.2 ACK and SACK Behaviour

The ACK number represents the highest consecutively received sequence
number.

The receive window contains exactly 32 positions:

`[ACK + 1, ACK + 32]`

The 32-bit SACK bitmap maps to these positions as follows:

    Bitmap Bit Represents
  ------------ ------------
         Bit 0 ACK + 1
         Bit 1 ACK + 2
         Bit 2 ACK + 3
           ... ...
        Bit 31 ACK + 32

A set bit (`1`) means that the corresponding packet has been received
and buffered.

A clear bit (`0`) means that the corresponding packet is missing or has
not been received.

Because the ACK number is the highest consecutively received packet, bit
0 is normally zero: `ACK + 1` is the next missing packet. The remaining
bits report out-of-order packets received beyond that gap.

When the ACK number advances, the bitmap is shifted right by the same
number of positions. If the ACK advances by 32 or more positions, the
bitmap becomes zero.

### 5.3 SACK Example

Assume:

-   ACK number = `3`
-   Packet `4` is missing.
-   Packets `5` and `7` have been received.
-   Packet `6` is missing.

The bitmap positions are:

      Bit   Packet Status           Bit Value
  ------- -------- -------------- -----------
        0        4 Missing                  0
        1        5 Received                 1
        2        6 Missing                  0
        3        7 Received                 1
    4--31    8--35 Not received             0

The first four bitmap bits, shown from bit 0 to bit 3, are:

`0101`

The sender uses the cumulative ACK and SACK bitmap to identify which
packets still need retransmission.

### 5.4 Retransmission

A sender stores each outstanding reliable packet until it is
acknowledged.

If an acknowledgement is not received before the retransmission timeout
(RTO), the packet is retransmitted.

The sender retransmits only packets that remain unacknowledged. Packets
reported as received by SACK do not need to be retransmitted.

The `RETX` flag is set on retransmitted packets.

### 5.5 Duplicate and Out-of-Order Packets

The receiver must:

-   Discard duplicate packets that have already been accepted.
-   Avoid delivering the same packet to the application more than once.
-   Buffer valid out-of-order packets.
-   Deliver buffered packets in sequence when missing earlier packets
    arrive.
-   Reject packets outside the current receive window.

### 5.6 Sequence Number Wraparound

Sequence numbers are 32-bit unsigned values and wrap from `0xFFFFFFFF`
to `0`.

All sequence-number comparisons must use modular serial-number
arithmetic.

The window size of 32 is much smaller than half of the 32-bit
sequence-number space, which makes the Selective Repeat window safe for
wraparound when serial-number arithmetic is implemented correctly.

------------------------------------------------------------------------

## 6. RTT and Retransmission Timeout

The reliability mechanism estimates round-trip time (RTT) and calculates
a retransmission timeout (RTO).

The estimator uses the Jacobson/Karels algorithm.

### 6.1 Default Values

  Parameter                   Default
  ------------------------- ---------
  Initial RTO                  200 ms
  Minimum RTO                   50 ms
  Maximum RTO                 2000 ms
  Alpha                           1/8
  Beta                            1/4
  Maximum retransmissions           8

These are configurable design defaults.

### 6.2 RTO Calculation

The retransmission timeout is calculated as:

`RTO = SRTT + 4 × RTTVAR`

Where:

-   `SRTT` is the smoothed round-trip time.
-   `RTTVAR` is the smoothed variation in round-trip time.

The calculated RTO is limited by the configured minimum and maximum
values.

### 6.3 Karn's Algorithm and Backoff

RTT samples must not be taken from retransmitted packets because the
acknowledgement may correspond to either the original packet or its
retransmission. This is known as Karn's algorithm.

After a timeout, the RTO is increased using exponential backoff.

After the configured maximum number of retransmissions is reached, the
reliability mechanism reports a delivery failure to the application.

------------------------------------------------------------------------

## 7. Peer and Stream Management

The protocol supports a dynamically changing number of peers.

There is no fixed maximum peer count in the common header. Peer
management is handled by the membership component.

### 7.1 Peer Identification

A peer is identified by its membership information and network endpoint.

The common header does not contain a Peer ID. The receiver uses the
source IP address and UDP port to identify the sending peer through its
membership table.

A peer's incarnation value can be used by the membership component to
distinguish a restarted peer from an earlier instance.

### 7.2 Stream Identification

The Stream ID identifies the logical communication stream.

Suggested stream IDs:

       Stream ID Purpose
  -------------- ----------------------------------
               0 Control traffic; unreliable only
               1 Group chat
               2 File metadata
    16 and above Individual file-transfer streams

The application may define additional stream IDs when needed.

### 7.3 Independent Reliability State

Reliability state is maintained independently for each:

-   Peer
-   Stream ID
-   Direction of communication

For example, peer A sending to peer B on stream 1 has separate state
from peer B sending to peer A on stream 1.

Different streams between the same peers also maintain separate sequence
numbers, windows, buffers, and timeout information.

A peer restart or membership incarnation change should cause the
relevant reliability state to be reset.

------------------------------------------------------------------------

## 8. File Transfer and Replication Responsibilities

File sharing uses chunk-based transfer.

A file is divided into chunks, and peers can request different chunks
from different sources.

The file-sharing component is responsible for:

-   Maintaining file manifests.
-   Tracking chunk availability.
-   Selecting chunk sources.
-   Selecting chunks for download.
-   Maintaining the required number of replicas.
-   Finding alternate peers when a source becomes unavailable.
-   Re-replicating chunks when a replica peer leaves or crashes.

The reliability component is responsible for:

-   Reliable packet delivery over UDP.
-   Retransmission of lost packets.
-   Duplicate detection.
-   In-order delivery within a reliable stream.
-   Reporting delivery failures to the application.

These responsibilities are different:

1.  **Transport retransmission:** Resends a lost UDP packet.
2.  **Chunk retry:** Requests a missing file chunk again, potentially
    from another peer.
3.  **Chunk replication:** Maintains copies of a chunk on multiple
    peers.

A successful transport acknowledgement only confirms that the remote
reliability mechanism received the packet. It does not prove that the
file chunk was permanently stored or replicated.

The file-sharing component must ensure that each required chunk has at
least two replicas on different peers, according to the project
requirements.

The file ID and chunk index identify file content. They are different
from the transport sequence number.

------------------------------------------------------------------------

## 9. Packet Validation

Every received datagram must be validated before its payload is
processed.

The receiver should check:

-   The datagram is at least 16 bytes long.
-   The protocol version is supported.
-   The message type is recognized.
-   Reserved flag bits are zero.
-   The `RETX` flag is used only with `RELIABLE`.
-   The payload length matches the actual datagram length.
-   The datagram does not exceed the maximum supported size.
-   The CRC-16 value is valid.
-   The payload format is valid for the message type.
-   Required fields and bitmap lengths are within their allowed ranges.

Malformed or corrupted packets must be discarded safely.

------------------------------------------------------------------------

## 10. Design Limitations and Notes

-   UDP does not provide reliable delivery; reliability is implemented
    by this protocol at the application layer.
-   ACK-only packets are not reliable and are not themselves
    acknowledged.
-   The common header does not contain a session or incarnation
    identifier. Membership changes should reset reliability state, but
    delayed packets from an earlier peer instance may still be a concern
    after a restart.
-   The maximum datagram size is 1424 bytes. Networks with smaller path
    MTUs, tunnels, or additional encapsulation may require a lower
    configured limit to avoid IP fragmentation.
-   CRC-16 detects accidental corruption; it does not provide
    authentication or protection against malicious packet modification.
-   The `ENC` flag indicates encrypted payload data, but encryption and
    key management are outside the scope of this specification.
-   Peer discovery, membership decisions, file replica placement, and
    chunk-source selection are handled by their respective application
    components.

------------------------------------------------------------------------

## 11. Summary

This protocol defines a common 16-byte UDP packet header and a set of
message types for decentralized peer communication.

It supports:

-   Peer discovery and membership messages.
-   Reliable group chat.
-   File metadata and chunk transfer.
-   Application-layer Selective Repeat ARQ.
-   A 32-packet send and receive window.
-   Cumulative acknowledgements and a 32-bit SACK bitmap.
-   CRC-16 packet integrity checking.
-   Independent reliability state for each peer, stream, and direction.

The protocol provides the communication foundation for a decentralized
P2P group messaging and file-sharing application.
