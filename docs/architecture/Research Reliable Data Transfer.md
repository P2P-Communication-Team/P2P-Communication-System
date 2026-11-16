# Architecture Decision Records

## ADR-01: Reliability Scheme and Multi-Flow Framing Over UDP

  -----------------------------------------------------------------------
  Field                               Value
  ----------------------------------- -----------------------------------
  **ADR ID**                          ADR-01

  **Title**                           Reliability Scheme and Multi-Flow
                                      Framing Over UDP

  **Status**                          Proposed --- pending team review

  **Decision Area**                   UDP Transport, Reliability, and
                                      Packet Multiplexing

  **Related Jira Task**               P2P-10: Research Reliable Data
                                      Transfer (RDT) Over UDP and
                                      Multi-Flow Framing

  **Related Follow-up Task**          P2P-16: PROTOCOL_SPEC.md

  **Project**                         Decentralized UDP-Based P2P Group
                                      Communication and Distributed File
                                      Sharing System

  **Project Duration**                5 weeks --- Agile Scrum, one-week
                                      sprints
  -----------------------------------------------------------------------

------------------------------------------------------------------------

## 1. Context and Constraints

### 1.1 Problem Context

The project implements a decentralized, multi-party peer-to-peer
communication and file-sharing application using UDP sockets.

Unlike TCP, UDP does not guarantee packet delivery, ordering, duplicate
suppression, or retransmission. Therefore, the application must
implement reliability mechanisms at the application layer for operations
that require dependable delivery.

The system must support two primary communication workloads:

1.  **Group communication:** Messages sent by a peer must be delivered
    to other available group members in sender-specific order and
    without duplicate application processing.
2.  **Distributed file transfer:** Files are divided into logical
    chunks, distributed across multiple peers, and retrieved through
    multi-source downloads. Missing or damaged data must be detected and
    recovered where a valid replica is available.

The reliability design must support both workloads without introducing a
centralized communication server or central file-storage dependency.

### 1.2 Architectural Constraints

The following constraints are derived from the Project Statement and
Project Charter.

  -----------------------------------------------------------------------
  Constraint                          Architectural implication
  ----------------------------------- -----------------------------------
  Decentralized P2P architecture      Every participating peer can send,
                                      receive, store, and serve data.
                                      Reliability must operate between
                                      peers without depending on a
                                      permanent central server.

  UDP as the underlying transport     The application must implement
                                      acknowledgements, sequence
                                      tracking, timeout detection,
                                      retransmission, and duplicate
                                      suppression where required.

  No TCP-style congestion control     The project will implement
                                      reliability without implementing
                                      TCP congestion-control algorithms.

  Single-port operation               The proposed transport design uses
                                      one application UDP port and one
                                      non-blocking UDP socket per peer
                                      process to carry control, chat, and
                                      file-transfer traffic. This is an
                                      ADR implementation choice, not an
                                      explicitly specified port
                                      requirement in the original
                                      Charter.

  Existing 38-byte packet header      The established proposed header
                                      layout must remain unchanged.
                                      Reliability and multiplexing must
                                      use its existing fields.

  Logical file chunks are distinct    A logical file chunk may be
  from UDP datagrams                  transferred through multiple UDP
                                      datagrams. Datagram retransmission
                                      must not be confused with logical
                                      chunk reconstruction.

  SHA-256 integrity verification      SHA-256 verifies logical file
                                      chunks and the reconstructed file.
                                      Packet-level error detection serves
                                      a separate, lower-level purpose.

  Multi-source downloading            Different logical chunks can be
                                      retrieved from different peers,
                                      with transfers proceeding
                                      concurrently where practical.

  Source failure recovery             If a source becomes unavailable,
                                      the downloader must be able to
                                      request missing data from another
                                      available replica.

  Five-week Scrum schedule            The design must be implementable
                                      and testable within the existing
                                      five one-week sprints.
  -----------------------------------------------------------------------

### 1.3 Existing 38-Byte Header Baseline

The Project Charter defines the following initial packet-header
structure:

  ------------------------------------------------------------------------
  Field                                         Size Purpose
  --------------------- ---------------------------- ---------------------
  Protocol Version                            1 byte Identifies the
                                                     application protocol
                                                     version.

  Message Type                                1 byte Identifies the packet
                                                     operation, such as
                                                     CHAT, ACK, or
                                                     CHUNK_DATA.

  Flags                                      2 bytes Carries
                                                     protocol-defined
                                                     packet modifiers and
                                                     control indicators.

  Header Length                              2 bytes Specifies the header
                                                     length.

  Sender Peer ID                            16 bytes Identifies the
                                                     originating peer.

  Sequence Number                            4 bytes Identifies a packet
                                                     within its
                                                     reliability stream.

  ACK Number                                 4 bytes Identifies the packet
                                                     being acknowledged or
                                                     carries
                                                     acknowledgement
                                                     information according
                                                     to the message type.

  Stream ID                                  4 bytes Identifies the
                                                     logical communication
                                                     or transfer stream.

  Payload Length                             2 bytes Specifies the
                                                     application payload
                                                     length.

  Header Checksum                            2 bytes Detects corruption in
                                                     the packet header.

  **Total**                             **38 bytes** **Existing proposed
                                                     header size.**
  ------------------------------------------------------------------------

All multi-byte numeric fields use network byte order (big-endian), as
specified in the Project Charter.

**This ADR does not replace, expand, or rearrange the 38-byte header.**
The meanings of the existing fields are clarified below. Exact numeric
message-type values, flag-bit assignments, reserved stream IDs, and
complete serialization rules will be finalized in
`P2P-16: PROTOCOL_SPEC.md`.

------------------------------------------------------------------------

## 2. ARQ Scheme Comparison and Evaluation

### 2.1 Alternatives Considered

Three commonly used Automatic Repeat reQuest (ARQ) approaches were
evaluated.

#### A. Stop-and-Wait ARQ

The sender transmits one data packet and waits for its acknowledgement
before transmitting the next packet.

**Advantages** - Simple implementation. - Small sender and receiver
buffers. - Straightforward acknowledgement and timeout handling. - Easy
to test and debug.

**Disadvantages** - Only one unacknowledged packet can be in transit. -
Network utilization is low when round-trip time is significant. -
File-transfer throughput is limited because the sender waits after every
packet.

**Assessment:** Stop-and-Wait is easy to implement but does not provide
sufficient parallelism for the project's multi-chunk file-transfer
workload.

#### B. Go-Back-N ARQ

Go-Back-N uses a sliding transmission window, allowing multiple packets
to be transmitted before acknowledgements are received.

If a packet is lost, the sender retransmits the missing packet and
subsequent outstanding packets within the affected transmission window.

**Advantages** - Better throughput than Stop-and-Wait. - Supports
multiple outstanding packets. - Uses comparatively simple receiver-side
buffering. - Can use cumulative acknowledgements.

**Disadvantages** - A single lost packet can cause several correctly
received packets to be retransmitted. - Unnecessary retransmissions
increase bandwidth consumption. - Packet loss can reduce efficiency when
transferring large files over UDP.

**Assessment:** Go-Back-N provides a reasonable improvement over
Stop-and-Wait, but retransmitting correctly received packets is
undesirable for multi-chunk file transfers.

#### C. Selective Repeat ARQ

Selective Repeat uses a sliding window but retransmits only packets that
have not been acknowledged.

The receiver can accept and temporarily buffer packets that arrive out
of order. Missing packets are retransmitted individually.

**Advantages** - Retransmits only missing or unacknowledged packets. -
Supports multiple packets in transit. - Handles out-of-order packet
arrival. - Avoids unnecessary retransmission of correctly received
packets. - Suitable for parallel and multi-source file transfers.

**Disadvantages** - Requires sender-side acknowledgement tracking. -
Requires receiver-side out-of-order buffering. - Needs more memory and
state management than Stop-and-Wait or Go-Back-N. - Requires careful
sequence-number and transmission-window management.

**Assessment:** Selective Repeat provides the best balance for this
project's file-transfer requirements because it improves throughput
while limiting retransmission overhead.

### 2.2 Comparison Summary

  ------------------------------------------------------------------------------
  Evaluation        Stop-and-Wait     Go-Back-N         Selective Repeat
  criterion                                             
  ----------------- ----------------- ----------------- ------------------------
  Multiple          No                Yes               Yes
  outstanding                                           
  packets                                               

  Out-of-order      Limited           Normally          Buffered
  reception                           discarded         

  Retransmission    One packet        Missing packet    Only
  behavior                            and subsequent    missing/unacknowledged
                                      outstanding       packets
                                      packets           

  Sender complexity Low               Medium            Higher

  Receiver          Low               Low               Higher
  buffering                                             

  Bandwidth         Low               Medium            High
  efficiency under                                      
  packet loss                                           

  Suitability for   Low               Medium            High
  multi-chunk                                           
  downloads                                             
  ------------------------------------------------------------------------------

### 2.3 Selected Approach

**Decision: Select Selective Repeat ARQ with a sliding transmission
window.**

Selective Repeat is selected as the primary reliability mechanism for
file-data transmission.

This decision is consistent with the Project Charter, which already
identifies Selective Repeat as the proposed approach for file chunk
transfer because it avoids retransmitting every packet in a transmission
window when only one packet is missing.

The mechanism will also provide the foundation for reliable chat
delivery, but the application-level ordering and delivery behavior will
remain specific to each message stream.

The selected approach addresses the project's WP2 trade-off between
transfer speed and reliability:

-   Stop-and-Wait favors simplicity but limits throughput.
-   Go-Back-N provides better throughput but can waste bandwidth after
    packet loss.
-   Selective Repeat provides better recovery efficiency at the cost of
    additional buffering and state management.

This is particularly important for file transfers because a logical file
chunk may require multiple UDP datagrams. Losing one datagram should not
force the sender to retransmit every successfully received datagram
belonging to the same transmission window.

### 2.4 Initial Window Configuration

The following values are proposed as initial implementation defaults.
They are not measured performance guarantees and must be evaluated
during integration and testing.

  ------------------------------------------------------------------------
  Stream type                    Initial window size Reason
  --------------------- ---------------------------- ---------------------
  Chat                                     8 packets Limits outstanding
                                                     messages while
                                                     allowing reliable
                                                     message delivery
                                                     without unnecessarily
                                                     delaying subsequent
                                                     messages.

  File transfer                           32 packets Allows multiple
                                                     file-data datagrams
                                                     to remain in flight,
                                                     reducing idle waiting
                                                     during transfer.
  ------------------------------------------------------------------------

The sender and receiver will use compatible configured receive-window
limits for each stream.

The window size will be measured and evaluated during Week 5. The
implementation may be adjusted based on observed latency, throughput,
retransmission count, and memory usage, provided the protocol remains
consistent across peers.

### 2.5 Sequence Number and Acknowledgement Behavior

The existing 32-bit Sequence Number field will identify individual
packets within a reliability stream.

The sequence-number space is maintained independently for each sender
and stream.

The receiver will maintain the following state for each active reliable
stream:

-   Expected next in-order sequence number.
-   Received-packet tracking within the receive window.
-   Temporary buffer for out-of-order packets.
-   Duplicate-packet detection information.

The sender will maintain:

-   Next sequence number to transmit.
-   Current transmission-window boundaries.
-   Outstanding packet records.
-   Acknowledgement status for each outstanding packet.
-   Retransmission timer information.

**Selective acknowledgement behavior:**

1.  The sender transmits a data packet with its assigned sequence
    number.
2.  The receiver validates the packet.
3.  If the packet is valid and falls within the receive window, the
    receiver records it and sends an ACK.
4.  The ACK identifies the received data packet through the existing ACK
    Number field.
5.  The sender marks only that packet as acknowledged.
6.  If a packet is missing, its acknowledgement does not arrive, and its
    retransmission timer eventually expires.
7.  The sender retransmits only that unacknowledged packet.
8.  The receiver suppresses duplicate application processing and
    acknowledges valid retransmissions again.
9.  Once missing earlier packets arrive, buffered packets can be
    delivered to the application in sequence.

The existing ACK message type and ACK Number field are sufficient for
individual packet acknowledgements. A new selective-acknowledgement
bitmap or additional header field is not required by this design.

### 2.6 Chat Ordering and Duplicate Suppression

Group chat has a specific requirement: messages from the same sender
must be delivered to each receiving peer in order and without duplicate
application processing.

Each sender maintains a sequence space for its chat stream.

A receiving peer uses the sender identity and stream identity to
maintain independent ordering state. It must not assume that packets
from different senders share one global sequence space.

The receiver will:

-   Accept valid packets within the receive window.
-   Buffer packets that arrive ahead of the expected sequence number.
-   Discard duplicate packets from application delivery.
-   Acknowledge valid duplicate retransmissions so that the sender can
    stop retransmitting.
-   Deliver messages to the chat application in sender-specific sequence
    order.

The design guarantees sender-specific ordering, not total ordering
across all senders. A global total-ordering mechanism is outside the
requirements unless separately designed and implemented.

For group delivery, acknowledgement and outstanding-delivery state must
be tracked for each intended recipient. One recipient's missing
acknowledgement must not prevent successful delivery to other available
recipients.

------------------------------------------------------------------------

## 3. Retransmission Timeout and Checksum Strategy

### 3.1 Retransmission Timeout: Alternatives

Two general timeout strategies were considered.

**Fixed timeout**

A fixed retransmission timeout uses the same configured delay for all
packets.

Advantages: - Simple to implement. - Predictable initial behavior. - No
RTT estimation state is required.

Disadvantages: - A short timeout can cause unnecessary retransmissions
on slower paths. - A long timeout delays recovery on fast paths. - It
does not adapt to changing network conditions.

**Adaptive RTT-based timeout**

An adaptive timeout estimates round-trip time from transmitted packets
and their acknowledgements.

Advantages: - Adapts to different peer response times. - Reduces
unnecessary retransmissions when network delay is variable. - Can
improve recovery speed when the observed RTT is low.

Disadvantages: - Requires RTT estimation state. - Requires careful
handling of retransmitted packets. - Is more complex than a fixed
timeout.

### 3.2 Selected RTO Strategy

**Decision: Use a Jacobson/Karels-style adaptive RTT estimator with
Karn's algorithm.**

This approach balances recovery speed and reliability without
introducing congestion control.

The RTT estimator will be maintained per remote peer. Individual
outstanding packets and retransmission timers remain independent within
each stream.

This avoids maintaining a separate estimator for every packet while
allowing the transport layer to adapt to the response time of each peer.

### 3.3 RTT Estimation Formula

Let:

-   `R` = newly measured RTT sample.
-   `SRTT` = smoothed round-trip time.
-   `RTTVAR` = round-trip time variation.
-   `RTO` = retransmission timeout.
-   `G` = timer granularity.

For the first valid RTT sample:

``` text
SRTT   = R
RTTVAR = R / 2
RTO    = SRTT + max(G, 4 * RTTVAR)
```

For each subsequent valid RTT sample:

``` text
RTTVAR = (1 - beta) * RTTVAR
         + beta * abs(SRTT - R)

SRTT   = (1 - alpha) * SRTT
         + alpha * R

RTO    = SRTT + max(G, 4 * RTTVAR)
```

The constants are:

``` text
alpha = 1/8
beta  = 1/4
```

The estimator updates `RTTVAR` using the previous `SRTT`, before
updating `SRTT`.

### 3.4 Initial and Clamped Timeout Values

The initial implementation will use:

  Parameter                 Initial value
  ----------------------- ---------------
  Initial RTO                      500 ms
  Minimum RTO                      100 ms
  Maximum RTO                     2000 ms
  Timer granularity (G)             10 ms
  Alpha                               1/8
  Beta                                1/4

The calculated timeout is clamped:

``` text
RTO = min(MAX_RTO, max(MIN_RTO, calculated_RTO))
```

The initial values are engineering starting points for the expected
project environment. They must be evaluated during testing rather than
treated as universal network-performance guarantees.

### 3.5 Exponential Backoff

When a packet's retransmission timer expires:

1.  Mark the packet as unacknowledged.
2.  Retransmit the packet.
3.  Increase its retransmission timeout using exponential backoff.
4.  Apply the maximum RTO limit.

The backoff rule is:

``` text
RTO_after_timeout = min(2 * current_RTO, MAX_RTO)
```

This reduces repeated retransmission attempts at excessively short
intervals when a peer or network path is not responding.

A bounded retry policy will be used to prevent an indefinitely stalled
transfer from consuming resources. After the configured retry limit is
reached, the affected peer or stream is treated as unavailable for that
operation.

For file downloads, the download module should attempt to obtain the
missing data from another available replica. Peer membership and
failure-detection mechanisms remain responsible for updating peer
availability.

### 3.6 Karn's Algorithm

An RTT sample must not be taken from an acknowledgement for a
retransmitted packet.

If a packet is retransmitted, the sender cannot reliably determine
whether its acknowledgement corresponds to the original transmission or
the retransmission.

Therefore:

-   RTT samples are taken only from packets transmitted once.
-   Retransmitted packets do not update the RTT estimator.
-   The estimator continues using valid non-retransmitted samples.

### 3.7 Packet-Level Checksum

The existing 38-byte header contains a 2-byte Header Checksum field.

**Decision: Use the 16-bit Internet checksum (one's-complement checksum)
for the header.**

Calculation rules:

1.  Set the Header Checksum field to zero during calculation.
2.  Calculate the one's-complement checksum over the complete 38-byte
    header.
3.  Store the resulting 16-bit value in network byte order.
4.  At reception, validate the header checksum before trusting header
    fields for packet processing.

This detects accidental corruption in the header, including fields used
for packet routing and reliability state.

### 3.8 Payload-Level Checksum

The Charter specifies the header checksum but does not prescribe a
separate payload checksum algorithm.

**ADR decision: Use CRC-32/ISO-HDLC for each UDP datagram's application
payload.**

The CRC parameters are:

  Parameter                Value
  ------------------------ ---------------------------
  Algorithm                CRC-32/ISO-HDLC
  Width                    32 bits
  Polynomial               0x04C11DB7
  Reflected input/output   Yes
  Initial value            0xFFFFFFFF
  Final XOR                0xFFFFFFFF
  Stored representation    4-byte network byte order

The CRC is appended as a 4-byte trailer after the application payload.
It is not added to the 38-byte header.

For this framing:

``` text
UDP Datagram =
    38-byte Header
    + Application Payload
    + 4-byte Payload CRC
```

The Payload Length field records the application payload length and
excludes the CRC trailer.

The receiver validates the header and payload checksum before passing
the packet to the reliability or application layer.

A packet with an invalid checksum is discarded and is not acknowledged
as successfully received. The sender will recover through the normal
retransmission mechanism.

### 3.9 Relationship Between CRC-32 and SHA-256

CRC-32 and SHA-256 serve different purposes and must not be treated as
interchangeable.

  -----------------------------------------------------------------------
  Mechanism               Scope                   Purpose
  ----------------------- ----------------------- -----------------------
  16-bit Internet         Packet header           Detect accidental
  checksum                                        corruption in header
                                                  fields.

  CRC-32/ISO-HDLC         Individual UDP datagram Detect accidental
                          payload                 payload corruption
                                                  before processing the
                                                  packet.

  SHA-256                 Logical file chunk      Verify the integrity of
                                                  a reconstructed logical
                                                  chunk.

  SHA-256                 Complete reconstructed  Verify that the
                          file                    downloaded file matches
                                                  the expected file
                                                  content.
  -----------------------------------------------------------------------

A logical file chunk may consist of several UDP datagrams. Packet-level
CRC validation occurs before data is accepted into the chunk-reassembly
process. SHA-256 validation occurs after the logical chunk or complete
file has been reconstructed.

If a chunk fails SHA-256 verification, it must be treated as invalid and
obtained again from a valid source where possible.

These checksums detect accidental corruption; CRC-32 and the Internet
checksum do not provide cryptographic authentication.

------------------------------------------------------------------------

## 4. Multi-Flow Multiplexing Over a Single UDP Socket

### 4.1 Design Objective

The application must carry multiple types of traffic concurrently:

-   Peer discovery and membership control messages.
-   Group chat messages and their acknowledgements.
-   File-transfer requests, data packets, and acknowledgements.

The proposed design uses one non-blocking UDP socket bound to the peer's
configured application port.

Control, chat, and file-transfer traffic share that socket. They are
distinguished using the existing packet header fields and dispatched to
the appropriate application module.

This is a single-socket transport design. It does not introduce separate
TCP connections, separate transport ports for each stream, or a
permanent central server.

### 4.2 Header-Based Multiplexing

The existing header fields work together as follows:

  -----------------------------------------------------------------------
  Header field                        Role in multi-flow operation
  ----------------------------------- -----------------------------------
  Sender Peer ID                      Identifies the peer that originated
                                      the packet.

  Stream ID                           Identifies the logical stream or
                                      transfer associated with the
                                      packet.

  Message Type                        Identifies the operation, such as
                                      JOIN, HEARTBEAT, CHAT, ACK,
                                      CHUNK_REQ, or CHUNK_DATA.

  Flags                               Carries defined control indicators,
                                      such as protocol-specific
                                      acknowledgement or retransmission
                                      modifiers.

  Sequence Number                     Identifies a data packet within its
                                      sender's stream sequence space.

  ACK Number                          Identifies the packet being
                                      acknowledged when the packet is an
                                      ACK.

  Payload Length                      Identifies the length of the
                                      application payload.
  -----------------------------------------------------------------------

The receiver identifies a reliable stream using the sender identity and
stream identity, with the receiving peer and protocol context forming
the local state for that communication.

Each stream maintains its own sequence-number, acknowledgement,
buffering, and retransmission state.

Consequently, packets from one stream cannot incorrectly advance another
stream's sequence state.

### 4.3 Logical Stream Separation

The following logical traffic categories will share the UDP socket.

  -----------------------------------------------------------------------
  Traffic category        Example message types   Reliability and
                                                  handling
  ----------------------- ----------------------- -----------------------
  Control                 DISCOVER, PEER_LIST,    Processed by discovery
                          JOIN, JOIN_ACK, LEAVE,  and membership modules.
                          HEARTBEAT               Messages requiring
                                                  dependable delivery use
                                                  the reliability
                                                  mechanism defined for
                                                  their operation.

  Chat                    CHAT, ACK               Uses sender-specific
                                                  sequence tracking,
                                                  duplicate suppression,
                                                  acknowledgement
                                                  handling, and ordered
                                                  application delivery.

  File transfer           CHUNK_REQ, CHUNK_DATA,  Uses Selective Repeat,
                          CHUNK_ACK, ACK, NACK    independent transfer
                                                  state, chunk
                                                  reconstruction, and
                                                  integrity verification.

  Error handling          ERROR, NACK             Reports invalid or
                                                  unsuccessful operations
                                                  according to the
                                                  protocol specification.
  -----------------------------------------------------------------------

The exact message-type values and the assignment of stream identifiers
will be documented in `P2P-16: PROTOCOL_SPEC.md`.

### 4.4 Stream Identification and Sequence Spaces

Each reliable stream has independent state.

The sender's 32-bit Sequence Number is interpreted within the relevant
stream, rather than as a global sequence number shared by all
application traffic.

For example:

``` text
Peer A
 |
 +-- Control Stream
 |      Independent sequence and acknowledgement state
 |
 +-- Chat Stream
 |      Independent sequence and acknowledgement state
 |
 +-- File Transfer Stream 1
 |      Independent sequence and acknowledgement state
 |
 +-- File Transfer Stream 2
        Independent sequence and acknowledgement state
```

The same sequence number may exist in two different streams without
creating a conflict because the streams have different identities.

ACK processing must be performed against the correct sender and stream
state. An acknowledgement for one file transfer must never acknowledge a
packet belonging to another file transfer or a chat stream.

Sequence-number wraparound must be handled using modular sequence-number
comparisons. The receive and send windows must remain within the
Selective Repeat sequence-space safety limit.

With a 32-bit sequence number, the maximum Selective Repeat window must
be less than or equal to half of the sequence-number space:

``` text
W <= 2^(32 - 1)
```

The proposed initial windows of 8 and 32 packets are comfortably within
this limit.

### 4.5 Preventing Head-of-Line Blocking

A single UDP socket does not require all application traffic to be
processed as one blocking sequence.

The transport layer will use a non-blocking event-driven architecture
with independent logical queues and per-stream state.

The processing flow is:

1.  Receive a UDP datagram from the socket.
2.  Validate its minimum size, header length, protocol version, and
    header checksum.
3.  Validate payload length and payload CRC.
4.  Identify the sender, message type, and stream ID.
5.  Dispatch the packet to the relevant control, chat, or file-transfer
    handler.
6.  Update only the reliability state belonging to that stream.
7.  Queue any resulting ACK, response, or retransmission.
8.  Return to processing other available packets and timers.

A slow or incomplete file transfer must not block the reception or
processing of chat and control packets.

The transport layer must not wait synchronously for an acknowledgement
before receiving another packet or processing another stream.

### 4.6 Queue and Scheduling Policy

The proposed implementation maintains separate outgoing queues:

``` text
UDP Transport Layer
 |
 +-- Control Queue
 |
 +-- Chat Queue
 |
 +-- File Transfer Queue
 |
 +-- Non-blocking UDP Socket
```

The initial scheduling priority is:

1.  **Control traffic:** JOIN, LEAVE, HEARTBEAT, discovery responses,
    and other necessary control messages.
2.  **Chat traffic:** CHAT messages and their acknowledgements.
3.  **File-transfer traffic:** CHUNK_REQ, CHUNK_DATA, and related
    transfer packets.

This priority policy addresses the WP2 trade-off between low chat
latency and high file-transfer throughput.

The scheduler must not permanently starve file transfers. It should use
bounded processing batches or fair scheduling so that file packets
continue to make progress when control and chat traffic are active.

ACK processing and retransmission timers must remain independent for
each reliable stream.

### 4.7 Non-Blocking Socket and Concurrency Model

The transport module will own the UDP socket and perform packet
reception, parsing, validation, dispatch, and transmission scheduling.

The initial architecture should avoid unnecessary socket-per-stream
designs.

The implementation may use a single event loop with non-blocking socket
operations and timer processing. Slow disk operations, such as writing
reconstructed file data, should not block the network-receive path.

If worker threads are required for disk operations, they must
communicate with the transport and download modules through bounded,
thread-safe queues.

The final threading model must remain consistent with the project's
selected programming language and implementation environment. It should
not introduce an unnecessary concurrency framework.

### 4.8 Logical Chunk Transfer Versus UDP Datagram Transfer

The Project Charter explicitly distinguishes a logical file chunk from
an individual UDP datagram.

For example, a logical file chunk of approximately 64 KB may be
transmitted using multiple UDP datagrams.

The responsibilities are therefore separated:

  -----------------------------------------------------------------------
  Layer                               Responsibility
  ----------------------------------- -----------------------------------
  Logical file chunk                  Represents a portion of the
                                      original file stored and
                                      distributed across peers.

  UDP datagram                        Carries a portion of a logical
                                      chunk or a protocol message.

  Selective Repeat                    Detects missing datagrams and
                                      retransmits only those datagrams.

  Chunk reconstruction                Reassembles validated datagram
                                      payloads into the logical chunk.

  SHA-256 verification                Verifies the reconstructed logical
                                      chunk and complete file.
  -----------------------------------------------------------------------

The final logical chunk size and maximum datagram payload size will be
determined through implementation testing.

Datagram payload sizes must be selected to avoid IP fragmentation under
the expected network conditions.

This ADR does not fix a final chunk size or datagram payload size.

------------------------------------------------------------------------

## 5. Failure Handling and Recovery

### 5.1 Packet Loss

If a data packet is lost, its acknowledgement will not be received.

When its retransmission timer expires, the sender retransmits that
packet without retransmitting other packets that have already been
acknowledged.

### 5.2 Packet Duplication

The receiver checks the sequence number against its current receive
state.

A duplicate packet must not be delivered to the application a second
time.

If the duplicate is otherwise valid, the receiver may send the
corresponding ACK again so that the sender can stop retransmitting.

### 5.3 Packet Reordering

Packets that arrive out of order but remain within the receive window
are buffered.

The receiver delivers data to the relevant application stream in the
required order once missing earlier packets arrive.

This preserves sender-specific chat ordering and supports file-data
reassembly.

### 5.4 Delayed or Lost Acknowledgements

If an acknowledgement is delayed or lost, the sender may retransmit the
original data packet.

The receiver's duplicate-detection mechanism prevents repeated
application processing and sends the acknowledgement again.

### 5.5 Source Peer Failure During File Download

If a file source becomes unavailable during a download:

1.  The downloader identifies the affected transfer as stalled or
    unavailable after the configured retry policy is exhausted.
2.  The download module consults the available chunk-location metadata.
3.  It selects another peer that holds a valid replica of the required
    logical chunk.
4.  It requests the missing data from that peer.
5.  The received datagrams are validated and reassembled.
6.  The reconstructed chunk is verified using SHA-256.
7.  The download continues until the complete file is reconstructed and
    verified.

This mechanism supports the Charter's requirement that a download can
recover from a source failure when another valid replica exists.

The reliability layer does not itself decide where replicas are stored
or how peers are selected. Those responsibilities belong to the storage,
metadata, and download modules.

------------------------------------------------------------------------

## 6. Consequences and Trade-offs

### 6.1 Positive Consequences

-   Selective Repeat avoids retransmitting packets that have already
    been acknowledged.
-   Multiple packets can remain in transit, improving file-transfer
    efficiency compared with Stop-and-Wait.
-   Independent stream state allows control, chat, and file transfers to
    progress concurrently.
-   Sender-specific chat ordering is maintained without requiring global
    ordering across all senders.
-   Duplicate suppression prevents repeated application-level message
    processing.
-   Adaptive RTO estimation responds to observed peer RTT rather than
    relying entirely on a fixed timeout.
-   CRC-32 provides packet-payload corruption detection, while SHA-256
    verifies logical chunk and file integrity.
-   One non-blocking UDP socket simplifies transport ownership and
    packet dispatch.
-   The design preserves the existing 38-byte packet-header baseline.
-   Source failure recovery can use the existing replica information and
    download scheduling responsibilities.

### 6.2 Negative Consequences and Resource Costs

**Memory overhead**

Selective Repeat requires sender-side outstanding-packet records and
receiver-side out-of-order buffers.

The memory cost increases with:

-   Number of active peers.
-   Number of simultaneous streams.
-   Configured transmission and receive-window sizes.
-   Maximum UDP payload size.
-   Number of packets awaiting acknowledgement.

The implementation must use bounded buffers and release packet state
after acknowledgement, timeout exhaustion, or stream termination.

**Processing overhead**

The transport must calculate and validate checksums, maintain sequence
state, process acknowledgements, and manage retransmission timers.

**Retransmission overhead**

Packet loss causes additional transmissions. Although Selective Repeat
avoids unnecessary retransmission of acknowledged packets, it cannot
eliminate the bandwidth cost of retransmitting missing data.

**Concurrency complexity**

Independent queues and stream states require careful synchronization if
worker threads are used. Shared transport state must not be modified
unsafely by multiple execution contexts.

**Failure-handling complexity**

The download module must distinguish an individual packet loss from an
unavailable peer. It must also coordinate alternative replica selection
with chunk-location metadata.

**Checksum overhead**

The 4-byte payload CRC trailer adds a small amount of overhead to each
datagram. The existing 38-byte header remains unchanged.

### 6.3 WP2 Trade-off Analysis

The design directly addresses the conflicting project requirements
identified under WP2.

  -----------------------------------------------------------------------
  Trade-off               Selected approach       Expected effect
  ----------------------- ----------------------- -----------------------
  Speed vs. reliability   Selective Repeat with a Supports multiple
                          sliding window          outstanding packets
                                                  while retransmitting
                                                  only missing packets.

  Low chat latency        Independent queues with Reduces the likelihood
  vs. file throughput     control/chat priority   that bulk file traffic
                          and fair file           delays interactive
                          scheduling              messages.

  Reliability vs. memory  Bounded per-stream send Controls buffering
  usage                   and receive windows     overhead while
                                                  retaining
                                                  retransmission
                                                  capability.

  Fast recovery           Adaptive RTT-based RTO  Adapts timeout behavior
  vs. unnecessary         with Karn's algorithm   to observed peer
  retransmissions                                 response times.

  Packet integrity        Header checksum,        Detects corruption at
  vs. transfer overhead   payload CRC-32, and     different protocol
                          SHA-256 at chunk/file   layers with limited
                          level                   additional packet
                                                  overhead.
  -----------------------------------------------------------------------

The final configuration must be validated using measured results rather
than assumed performance values.

------------------------------------------------------------------------

## 7. Out-of-Scope Boundaries

The following features are intentionally excluded from this ADR and the
initial five-week implementation unless the team formally approves a
scope change.

### 7.1 TCP-Style Congestion Control

The project requires TCP-like reliability but explicitly allows
congestion control to be ignored.

Therefore, this design does not implement:

-   TCP slow start.
-   Congestion avoidance.
-   Congestion windows.
-   Fast recovery.
-   TCP congestion-control algorithms.

The transmission window in this ADR is a reliability window, not a TCP
congestion window.

### 7.2 Advanced Flow Control

Dynamic receiver-advertised window negotiation and sophisticated
flow-control algorithms are not required by this ADR.

The initial design uses configured bounded windows. Any later addition
must be justified by implementation evidence and documented through an
approved decision.

### 7.3 Global Total Ordering

The system guarantees sender-specific message ordering. It does not
guarantee one globally agreed order for messages originating from
different senders.

A global ordering or consensus mechanism is outside this decision.

### 7.4 Advanced Transport Features

The following are not required for the initial implementation:

-   Full TCP connection-state emulation.
-   TCP-compatible congestion-control behavior.
-   Advanced packet pacing algorithms.
-   Dynamic per-stream congestion control.
-   Complex adaptive window negotiation.
-   A separate transport connection for every logical stream.

### 7.5 Security and Authentication

CRC-32 and the Internet checksum are error-detection mechanisms, not
security mechanisms.

End-to-end encryption, peer authentication, cryptographic packet
authentication, and protection against malicious packet injection are
outside this ADR and are identified as possible future enhancements in
the Charter.

### 7.6 Advanced Distributed-System Enhancements

This ADR does not define:

-   Automatic re-replication after a peer permanently leaves.
-   Advanced distributed metadata consistency.
-   A new peer-discovery mechanism.
-   Advanced NAT traversal.
-   A new replica-placement algorithm.
-   A new chunk-scheduling algorithm.

These responsibilities remain with the relevant project modules and
existing scope decisions.

------------------------------------------------------------------------

## 8. Relationship to Project Work Packages and Sprint Objectives

This decision supports the academic and implementation objectives
identified in the Project Statement.

  -----------------------------------------------------------------------
  Project attribute                   Relationship to ADR-01
  ----------------------------------- -----------------------------------
  **WP1 --- Depth of knowledge        Evaluates ARQ protocols, RTT
  required**                          estimation, checksums, UDP
                                      behavior, sliding windows, and
                                      multi-flow transport framing.

  **WP2 --- Range of conflicting      Evaluates speed versus reliability,
  requirements**                      chat latency versus file
                                      throughput, and memory overhead
                                      versus retransmission efficiency.

  **WP3 --- Depth of analysis**       Records the engineering rationale
                                      for choosing Selective Repeat,
                                      per-stream sequence state, timeout
                                      handling, and packet framing.

  **WP4 --- Familiarity of issues**   Addresses packet loss, duplication,
                                      reordering, delayed
                                      acknowledgements, peer failure, and
                                      incomplete transfers.

  **WP7 --- Interdependence**         Defines how transport reliability
                                      interacts with discovery,
                                      membership, chat, storage,
                                      downloading, and concurrency.

  **WK3 --- Engineering               Applies UDP, ARQ, sequence numbers,
  Fundamentals**                      acknowledgements, checksums, and
                                      RTT estimation.

  **WK4 --- Specialist Knowledge**    Applies reliable transport design
                                      and multi-flow framing to a
                                      decentralized P2P application.
  -----------------------------------------------------------------------

### Sprint Alignment

  -----------------------------------------------------------------------
  Sprint                  Charter objective       ADR-01 contribution
  ----------------------- ----------------------- -----------------------
  Week 1                  Requirements, project   Establishes the
                          setup, and initial      transport decision
                          design                  baseline for subsequent
                                                  protocol and
                                                  implementation work.

  Week 2                  Peer discovery,         Defines the reliability
                          membership, and initial and stream-separation
                          group communication     principles needed by
                                                  chat and control
                                                  traffic.

  Week 3                  UDP reliability and     Directly supports ARQ
                          distributed file        selection, ACK
                          storage                 handling,
                                                  retransmission timers,
                                                  packet validation, and
                                                  initial reliable chunk
                                                  transfer.

  Week 4                  Multi-source            Supports reliable
                          downloading and fault   datagram transfer,
                          recovery                independent
                                                  file-transfer streams,
                                                  and recovery of missing
                                                  data from available
                                                  replicas.

  Week 5                  Integration, testing,   Provides the design
                          optimization, and       basis for packet-loss,
                          release                 duplication,
                                                  reordering, latency,
                                                  throughput, and
                                                  recovery testing.
  -----------------------------------------------------------------------

------------------------------------------------------------------------

## 9. Implementation and Testing Implications

The implementation must demonstrate the following behaviors.

  -----------------------------------------------------------------------
  Test ID                 Test scenario           Expected result
  ----------------------- ----------------------- -----------------------
  RDT-01                  Normal packet           Valid data packets are
                          transmission            received and
                                                  acknowledged.

  RDT-02                  Simulated packet loss   Missing packets are
                                                  retransmitted after
                                                  timeout.

  RDT-03                  Duplicate packet        Duplicate data is not
                          delivery                processed by the
                                                  application more than
                                                  once.

  RDT-04                  Out-of-order packet     Valid packets are
                          arrival                 buffered and delivered
                                                  in the required stream
                                                  order.

  RDT-05                  Delayed acknowledgement The sender handles
                                                  timeout and duplicate
                                                  retransmission safely.

  RDT-06                  Multiple active streams Control, chat, and
                                                  file-transfer traffic
                                                  are dispatched to their
                                                  correct stream
                                                  handlers.

  RDT-07                  Concurrent chat and     File-transfer activity
                          file transfer           does not block chat
                                                  processing.

  RDT-08                  Independent             An ACK for one stream
                          acknowledgement state   does not acknowledge
                                                  packets in another
                                                  stream.

  RDT-09                  Invalid header checksum The invalid packet is
                                                  discarded before normal
                                                  processing.

  RDT-10                  Invalid payload CRC     The packet is rejected
                                                  and is not acknowledged
                                                  as valid data.

  RDT-11                  Logical chunk           Multiple datagrams are
                          reconstruction          reassembled into the
                                                  correct logical chunk.

  RDT-12                  SHA-256 verification    Reconstructed chunk and
                                                  complete file hashes
                                                  are checked against
                                                  expected values.

  RDT-13                  Source failure during   Missing data is
                          download                requested from another
                                                  available replica where
                                                  one exists.

  RDT-14                  Four-peer integration   At least four peers can
                                                  communicate and
                                                  participate in the
                                                  integrated
                                                  demonstration.
  -----------------------------------------------------------------------

### Performance Measurements

The following metrics should be recorded during testing:

-   Median chat latency.
-   P95 chat latency.
-   File-transfer throughput.
-   Retransmission count.
-   Peer failure detection time.
-   Download recovery time.
-   Memory usage under concurrent transfers.

Where practical, packet-loss tests should include the Charter's proposed
conditions:

-   0% packet loss.
-   5% packet loss.
-   10% packet loss.
-   20% packet loss.

Results must include test conditions, number of peers, file sizes, and
measured values. Performance targets must not be invented before
measurement.

------------------------------------------------------------------------

## 10. Dependencies and Follow-up Work

This ADR establishes the architectural decision. The complete wire-level
protocol must be specified before independent team members implement
interoperating transport components.

The following details must be finalized in `P2P-16: PROTOCOL_SPEC.md`:

1.  Exact numeric message-type values for all supported messages.
2.  Exact flag-bit assignments and their meanings.
3.  Exact semantics of Sequence Number and ACK Number for every message
    type.
4.  Stream ID allocation, scope, and lifecycle.
5.  Sequence-number initialization and wraparound behavior.
6.  ACK packet format and the use of the existing ACK Number field for
    individual Selective Repeat acknowledgements.
7.  Header checksum calculation and validation procedure.
8.  Payload CRC-32 trailer encoding and checksum coverage.
9.  Exact Payload Length interpretation, including exclusion of the CRC
    trailer.
10. Maximum UDP datagram payload size and fragmentation-avoidance
    policy.
11. Configured transmission and receive-window sizes.
12. RTO initialization, update, clamping, and retry-limit behavior.
13. Expected message sequences for CHAT, CHUNK_REQ, CHUNK_DATA,
    CHUNK_ACK, JOIN, and other relevant operations.
14. Behavior when a peer becomes unavailable or a stream is terminated.

The protocol specification must preserve the 38-byte header and the
decisions recorded in this ADR. Any proposed change to the header or
selected reliability mechanism must be reviewed and documented rather
than introduced independently by individual module implementations.

------------------------------------------------------------------------

## 11. Final Decision Summary

The proposed reliability and multi-flow architecture is:

1.  Use **Selective Repeat ARQ with a sliding transmission window** as
    the primary reliable data-transfer mechanism.
2.  Maintain independent sequence, acknowledgement, buffering, and
    retransmission state for each logical stream.
3.  Use the existing **32-bit Sequence Number and ACK Number fields**
    for per-stream packet tracking and individual acknowledgements.
4.  Use **adaptive RTT-based RTO estimation** with Jacobson/Karels
    smoothing, Karn's algorithm, bounded timeout values, and exponential
    backoff.
5.  Use the existing **16-bit header checksum field** for an Internet
    checksum over the 38-byte header.
6.  Use a **CRC-32/ISO-HDLC payload trailer** for individual UDP
    datagram payload validation.
7.  Retain **SHA-256** for logical chunk and complete-file integrity
    verification.
8.  Multiplex control, chat, and file-transfer traffic over one
    non-blocking UDP socket using the existing header fields.
9.  Maintain independent queues and per-stream state to prevent
    file-transfer processing from blocking chat and control traffic.
10. Prioritize control and chat traffic while ensuring fair progress for
    file transfers.
11. Recover missing file data from another available replica when a
    source peer becomes unavailable.
12. Do not implement TCP-style congestion control as part of this
    reliability design.

**Approval condition:** The team should review and approve this ADR
before implementation depends on it. Once approved,
`P2P-16: PROTOCOL_SPEC.md` must define the exact interoperable wire
format and message sequences consistent with these decisions.

**End of ADR-01.**
