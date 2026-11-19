# Member 4: Distributed File Storage — Architecture & Research Overview

**Sprint:** Sprint 1 (Research & System Design)  
**Module:** Distributed File Storage & P2P Transfer Engine  
**Reference Architecture:** BitTorrent Protocol & Content-Addressed P2P Storage  

---

## 1. Executive Summary
This document outlines the architectural research, protocol mechanics, and technical prerequisites required to implement the **Distributed File Storage** module for our Peer-to-Peer (P2P) Computer Networks project. By adapting core principles from the **BitTorrent protocol** (content-addressed chunking, swarm-based parallel downloading, and cryptographic verification) and extending them for fault-tolerant storage, the system eliminates single points of failure and maximizes network throughput across peers.

---

## 2. Core System Components (BitTorrent Reference Model)

### 2.1 File Chunking
* **Concept:** Large files are never transferred as monolithic streams. Instead, the file is treated as a contiguous byte array and partitioned into fixed-size **Pieces** (Chunks).
* **Two-Tier Granularity:**
  1. **Storage Pieces ($256\text{ KB}$ – $1\text{ MB}$):** The unit of disk storage and cryptographic verification. Only the final piece may be smaller than the fixed piece size.
  2. **Network Blocks ($1\text{ KB}$ – $16\text{ KB}$):** Pieces are further subdivided into smaller blocks for wire transmission so they fit cleanly within network datagrams/packets without causing head-of-line blocking or excessive retransmission costs.
* **Design Trade-off:** Smaller piece sizes increase download parallelism and minimize bandwidth wasted on corrupted chunks, but increase metadata overhead. Larger piece sizes keep metadata compact but slow down hash verification turnaround.

### 2.2 Chunk Placement
* **Pull-Based / Demand-Driven (Standard BitTorrent):**
  * BitTorrent does not pre-assign chunks to specific storage nodes. Peers dynamically advertise which chunks they hold using a **Bitfield** (a compact binary array where `1` indicates an owned, verified piece and `0` indicates a missing piece).
  * As soon as a peer completes and verifies a piece, it broadcasts a `HAVE` message to connected peers, immediately turning itself into a storage provider for that chunk.
* **Deterministic / Proactive Placement (Persistent Distributed Storage Extension):**
  * Because standard BitTorrent loses a file if all original seeders go offline, persistent P2P storage systems map `Chunk_ID`s onto a **Consistent Hashing Ring (e.g., Kademlia DHT)** to deterministically assign primary and backup storage peers.

### 2.3 Replication
* **Emergent Swarm Replication:** Every downloader ("leecher") simultaneously uploads completed chunks to other peers, automatically scaling the replication factor with file popularity.
* **Incentive & Bandwidth Management (Tit-for-Tat):**
  * To prevent free-riding, BitTorrent employs a **Choking/Unchoking** algorithm: peers prioritize uploading to the top $N$ peers (typically 4) providing the fastest download rates, alongside a periodic **Optimistic Unchoke** (every 30 seconds) to discover better peers.
* **Target Replication Factor ($R$):** For fault-tolerant storage in our system, each chunk must maintain a minimum replication factor (e.g., $R = 3$ active copies across distinct peers). A background health monitor triggers re-replication whenever a peer disconnects and drops a chunk's replica count below $R$.

### 2.4 File Metadata (`.meta` / `.torrent` Manifest)
* **Structure:** Metadata is decoupled from raw payload data and stored in a compact manifest file (serialized via Bencode, JSON, or Protobuf).
* **Essential Fields:**
  * `file_name` & `total_size`: Original file name(s) and total byte length.
  * `piece_length`: Fixed byte size of each chunk (e.g., $262,144\text{ bytes}$ for $256\text{ KB}$).
  * `pieces`: Concatenated array of cryptographic hashes (e.g., 32-byte `SHA-256` digests) for every piece in sequential order.
  * `tracker / bootstrap_peers`: Endpoint(s) used to discover initial peers in the swarm.
* **Content Addressing (`InfoHash`):** The entire metadata dictionary is hashed to produce a unique **InfoHash**, serving as the global primary key for locating files and verifying swarm membership.

### 2.5 Parallel Downloading & Piece Selection
Peers maintain concurrent connections to multiple neighbors and pipeline block requests across them simultaneously. Piece selection follows a three-phase state machine:
1. **Random First Piece:** When a peer holds $0$ pieces, it downloads a random piece first so it quickly acquires valid data to trade with other peers.
2. **Rarest-First Strategy:** Once the peer holds initial pieces, it aggregates all neighbors' Bitfields and prioritizes downloading chunks with the lowest availability across the swarm. This prevents the **"Last Piece Problem"** (where the sole holder of a rare chunk disconnects, stalling the entire swarm at 99%).
3. **Endgame Mode:** When only the final few blocks remain, the client broadcasts requests for those blocks to *all* connected peers and issues `CANCEL` messages as soon as the first copy arrives, eliminating tail latency from slow peers.

### 2.6 Failover and Integrity Verification
* **Cryptographic Integrity Verification:**
  * A peer **never** advertises (`HAVE`) or uploads a chunk until all sub-blocks arrive, are reassembled, and pass `SHA-256` hash verification against the manifest.
  * If a hash mismatch occurs, the corrupted buffer is discarded, the sending peer's trust score is penalized (or IP banned on repeat offenses), and the piece is re-queued.
* **Seamless Failover:**
  * Because transfer state is tracked per chunk/block rather than bound to a single connection session, if a peer crashes or times out mid-transfer, only the unacknowledged in-flight block offsets are reassigned to another peer holding that piece.

---

## 3. Engineering Prerequisites

To implement this module cleanly in Sprint 2+, the following technical components must be established:

### 3.1 Networking & Transport Layer
* **Non-Blocking Socket Multiplexing:** Event-driven I/O (`epoll`, `poll`, or `select` on POSIX systems / async sockets) to manage concurrent peer connections without spawning an excessive number of OS threads.
* **Reliable Framing over UDP (if using UDP):** Since UDP is connectionless and unreliable, our transport wrapper must implement:
  * Packet sequence numbers & ACKs/NACKs.
  * Retransmission timeouts (RTO) and basic congestion windowing (inspired by BitTorrent's **uTP**).
  * Maximum Transmission Unit (MTU) awareness (keeping UDP payloads $\le 1400\text{ bytes}$ to prevent IP-level fragmentation).
* **Peer Liveness Detection:** Periodic `PING` / `HEARTBEAT` packets to detect disconnected nodes and trigger failover/re-replication.

### 3.2 Disk I/O & Data Structures
* **Random-Access File I/O (`pread` / `pwrite` or `fseek`):** Because pieces download out of order (Rarest-First), the client must pre-allocate disk space (or use sparse files) and write verified chunks directly to their exact byte offset:
  $$\text{Byte\_Offset} = \text{Piece\_Index} \times \text{Piece\_Size}$$
* **Bitfield Management:** A memory-efficient bitset implementation to track local chunk ownership and store remote peer bitfields for O(1) availability lookups.
* **Cryptographic Library:** Integration of `OpenSSL` (`libcrypto`) or a lightweight standalone `SHA-256` library for fast chunk hashing and `InfoHash` generation.
* **Concurrency Control:** Thread-safe state tables (protected by mutexes/read-write locks) separating network I/O workers from disk I/O and hashing threads.

### 3.3 Wire Protocol Specification (Peer Messages)
All peers must adhere to a standardized binary message framing format (`[4-byte Length Prefix][1-byte Message ID][Payload]`):

| Message ID | Name | Payload | Description |
| :--- | :--- | :--- | :--- |
| `0x01` | `HANDSHAKE` | `ProtocolVer`, `InfoHash`, `PeerID` | Validates swarm compatibility upon connection. |
| `0x02` | `BITFIELD` | `Bitfield Bytes` | Sent post-handshake to advertise owned chunks. |
| `0x03` | `HAVE` | `Piece_Index` | Broadcast when a newly downloaded piece passes hash check. |
| `0x04` | `REQUEST` | `Piece_Index`, `Offset`, `Length` | Requests a specific sub-block from a peer. |
| `0x05` | `PIECE` | `Piece_Index`, `Offset`, `Raw_Data` | Delivers the requested chunk bytes. |
| `0x06` | `CANCEL` | `Piece_Index`, `Offset`, `Length` | Cancels duplicate block requests during Endgame Mode. |
| `0x07` | `PING / PONG` | `Timestamp` | Verifies peer liveness for failover and replication tracking. |

---

## 4. Sprint 1 Design Decisions (Action Items for Team Alignment)

Before transitioning from research to implementation, the team should finalize these four system parameters:

1. **Chunk & Packet Sizing:** Standardize on **$256\text{ KB}$ Pieces** divided into **$1\text{ KB}$ (or $16\text{ KB}$ framed) Blocks** to balance metadata compactness with network MTU efficiency.
2. **Metadata Sharing Mechanism:** Decide whether `.meta` descriptors are exchanged directly via the P2P messaging layer or indexed via a central Tracker / DHT node.
3. **Storage Persistence Model:** Confirm whether files are seeded purely while uploaders remain online (BitTorrent mode) or proactively replicated to $R = 3$ online peers upon upload (Distributed Storage mode).
4. **Local State & Resume Support:** Store partial downloads in a `.part` staging file alongside a local `.state` bitfield checkpoint so interrupted transfers resume seamlessly after client restarts.