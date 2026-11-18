# ADR-02: Discovery & Heartbeat Interval

**Project:** Decentralized P2P Group Chat and File-Sharing System  
**Sprint:** Sprint 0 — Research and Architecture  
**Status:** Proposed — pending team approval  
**Related requirements:** CN WP3 / WP4  
**Decision owner:** Peer Discovery and Membership Module

---

## 1. Context and Problem

The system must allow at least four peers to discover one another and maintain an accurate list of active peers without depending on a central server.

Peers communicate over UDP. The discovery mechanism must work on a local area network (LAN), while also providing an alternative when broadcast or multicast traffic cannot cross network boundaries.

The system must also detect when a peer disconnects or crashes. If a peer becomes unreachable, the remaining peers should eventually remove it from their active peer lists.

Two design decisions are required:

1. How peers discover other peers.
2. How peers detect that another peer is no longer active.

## 2. Discovery Protocol Options

### 2.1 UDP LAN Broadcast

A peer sends a `DISCOVER` message to a predefined UDP broadcast address and port.

For example:

- Broadcast address: `255.255.255.255`
- Discovery UDP port: `5000`

All peers listening on that LAN discovery port can receive the message and respond.

**Advantages:**
- Simple to implement.
- No central coordinator is required.
- Suitable for discovering peers on the same LAN.
- A newly started peer can discover multiple existing peers.

**Disadvantages:**
- Routers generally do not forward broadcast packets between subnets.
- Some firewalls block broadcast traffic.
- WSL2 NAT networking may prevent broadcast discovery between a WSL2 instance and other LAN devices.

### 2.2 UDP Multicast

A peer sends a discovery message to a configured multicast group, for example:

`239.255.0.1:5000`

Peers that join the multicast group can receive discovery messages.

**Advantages:**
- One message can reach multiple group members.
- Avoids sending an individual discovery request to every known peer.
- Useful for LAN-based peer discovery.

**Disadvantages:**
- Multicast support depends on network configuration.
- Routers may not forward multicast traffic between subnets.
- Firewall and WSL2 networking restrictions may interfere with multicast.

### 2.3 Bootstrap Peer Fallback

When broadcast or multicast discovery is unavailable, a peer can use one or more predefined bootstrap peer addresses.

Example configuration:

```text
BOOTSTRAP_PEERS = [
    "192.168.1.10:5000",
    "192.168.1.11:5000"
]
```

A new peer sends a `DISCOVER` request directly to each configured bootstrap endpoint.

The bootstrap peer responds with a `PEER_LIST` containing the currently known active peers.

**Important:** A bootstrap peer is only an initial contact point. It is not a central coordinator. Once discovered, peers communicate directly with one another and can exchange peer information. The system should support multiple bootstrap addresses so that discovery does not depend on one machine.

## 3. Chosen Discovery Approach

The system will use a hybrid discovery mechanism:

1. **Primary method:** UDP LAN broadcast.
2. **Optional LAN method:** UDP multicast, if supported by the network.
3. **Fallback method:** Direct UDP requests to configured bootstrap peers.

This approach supports local peer discovery while providing an alternative for environments where broadcast or multicast cannot reach other peers.

The design must not assume that LAN broadcast works across subnets or WSL2 NAT boundaries.

## 4. Exact Discovery Handshake

The discovery process follows:

`DISCOVER → PEER_LIST → JOIN`

### Step 1: DISCOVER

When a peer starts, it:

1. Generates or loads a unique `PeerID`.
2. Opens its UDP listening socket.
3. Sends a `DISCOVER` message using LAN broadcast or multicast.
4. If no usable peer list is received, it sends `DISCOVER` directly to configured bootstrap peers.

Example message:

```text
DISCOVER {
    protocol_version,
    peer_id,
    listening_ip,
    listening_port,
    request_id
}
```

### Step 2: PEER_LIST

An existing peer that receives a valid discovery request responds directly to the requesting peer.

Example:

```text
PEER_LIST {
    protocol_version,
    request_id,
    responding_peer_id,
    peers: [
        { peer_id, ip, port },
        { peer_id, ip, port }
    ]
}
```

The response contains the known active peers, including the responding peer where appropriate.

The requesting peer validates the protocol version and ignores duplicate or invalid peer entries.

If the peer list is too large for a safe UDP datagram, the implementation should split it into multiple responses or use a bounded peer-list response.

### Step 3: JOIN

The new peer sends a `JOIN` message directly to the peers returned in the peer list.

Example:

```text
JOIN {
    protocol_version,
    peer_id,
    listening_ip,
    listening_port
}
```

Each receiving peer adds the new peer to its membership table if it is valid and not already registered.

The receiving peer may respond with a `JOIN_ACK` to confirm registration.

After joining, peers exchange membership information as needed so that their peer lists converge.

### Handshake Sequence Diagram

```text
New Peer             Existing Peer A          Existing Peer B
    |                       |                        |
    |------ DISCOVER ------>|                        |
    |                       |                        |
    |<----- PEER_LIST ------|                        |
    |                       |                        |
    |---------------- JOIN ------------------------->|
    |                       |                        |
    |<--------------- JOIN_ACK ----------------------|
    |                       |                        |
    |------ JOIN ---------->|                        |
    |<----- JOIN_ACK -------|                        |
    |                       |                        |
    |        Peer is now part of the group           |
```

The initial `DISCOVER` may be broadcast or multicast. The `PEER_LIST` response and subsequent `JOIN` messages are sent using direct UDP unicast.

## 5. Bootstrap Fallback for Cross-Subnet and WSL2 Environments

UDP broadcast and multicast are generally limited to the local network segment. Therefore, they cannot be relied upon when peers run on different subnets or in WSL2 NAT environments.

The following fallback procedure will be implemented:

1. Configure at least one reachable bootstrap peer IP address and UDP port.
2. If LAN discovery does not produce a usable peer list, send `DISCOVER` directly to the bootstrap endpoints.
3. Receive `PEER_LIST` from a reachable bootstrap peer.
4. Send `JOIN` directly to the returned peers.
5. Continue normal peer-to-peer communication without depending on the bootstrap peer.

### WSL2 considerations

In WSL2 NAT mode, the Linux instance may have a different virtual IP address from the Windows host. LAN broadcast packets from WSL2 may not reach other physical LAN devices.

Possible solutions:

- Use a reachable Windows host IP and configure UDP port forwarding to the WSL2 application.
- Use WSL2 mirrored networking where supported and correctly configured.
- Configure a bootstrap peer using a reachable IP address and UDP port.
- Ensure the required UDP port is allowed through Windows and Linux firewalls.

**Important:** The bootstrap address must be reachable from the peer that is trying to join. A private WSL2 virtual IP should not be assumed to be reachable by other LAN devices.

## 6. Failure Detection Options

Two failure detection approaches were considered.

### 6.1 Periodic Heartbeats with Timeout

Every active peer periodically sends a small UDP `HEARTBEAT` message to its known peers.

Example:

```text
HEARTBEAT {
    peer_id,
    sequence_number,
    timestamp
}
```

Each receiver records the time of the most recent heartbeat from each peer.

If no heartbeat is received within the configured timeout, that peer is marked as suspected or inactive.

**Advantages:**
- Easy to understand and implement.
- Low processing overhead.
- Suitable for a small system with at least four peers.
- Straightforward to test.

**Disadvantages:**
- Temporary network congestion can cause false failure detection.
- A fixed timeout may not suit networks with highly variable latency.

### 6.2 Phi Accrual Failure Detector

A phi-accrual detector evaluates the time since the last heartbeat using the observed heartbeat arrival-time distribution.

It calculates a suspicion value called `phi`. A higher value indicates that the peer is increasingly likely to have failed.

A peer can be suspected when:

`phi >= configured_threshold`

**Advantages:**
- Adapts to changes in heartbeat arrival patterns.
- Can be more flexible in networks with variable delays.
- Provides a continuous suspicion level instead of a single fixed timeout.

**Disadvantages:**
- More complicated to implement.
- Requires tracking heartbeat arrival statistics.
- Needs careful threshold selection and testing.

## 7. Chosen Failure Detection Approach

For the initial implementation, the project will use **periodic heartbeats with a fixed timeout threshold**.

This is selected because the project has a small expected network size and requires a reliable, understandable implementation within the semester.

A phi-accrual detector may be considered as a future improvement if testing shows that fixed timeouts produce too many false failure detections.

### Recommended Configuration

| Parameter | Selected value | Reason |
|---|---:|---|
| Heartbeat interval | 2 seconds | Regular detection without excessive traffic |
| Dead-peer timeout | 8 seconds | Allows several missed heartbeat intervals |
| Missed heartbeat intervals | Approximately 4 | Provides tolerance for temporary packet loss |
| Discovery retry interval | 3 seconds | Allows another discovery attempt if no response arrives |
| Initial peer count target | At least 4 | Matches project requirements |

A peer is marked as suspected or inactive if no valid heartbeat is received from it for 8 seconds.

The timeout should be checked using a monotonic clock rather than the system wall clock.

### Heartbeat Sequence

```text
Peer A                  Peer B
  |                        |
  |------ HEARTBEAT ------>|
  |                        |
  |<------ HEARTBEAT ------|
  |                        |
  |------ HEARTBEAT ------>|
  |                        |
  |<------ HEARTBEAT ------|
  |                        |
  |     Peer B crashes     |
  |                        X
  |                        |
  | No heartbeat received  |
  |                        |
  | After 8 seconds:       |
  | Mark Peer B inactive   |
```

## 8. Maintaining Consistent Active Peer Lists

Each peer maintains its own membership table.

Example:

| Peer ID | IP Address | UDP Port | Last Heartbeat | Status |
|---|---|---:|---|---|
| Peer-A | 192.168.1.10 | 5000 | Recent | Active |
| Peer-B | 192.168.1.11 | 5000 | Recent | Active |
| Peer-C | 192.168.1.12 | 5000 | Recent | Active |
| Peer-D | 192.168.1.13 | 5000 | Older than 8 sec | Suspected/Inactive |

The following rules will be applied:

1. Each peer maintains a peer table indexed by unique `PeerID`.
2. A valid `JOIN` adds a previously unknown peer.
3. A heartbeat updates that peer's last-seen timestamp.
4. A peer exceeding the 8-second timeout is marked inactive and removed from the active peer list.
5. Duplicate `JOIN` messages must not create duplicate peer entries.
6. Peers periodically exchange membership information to help their peer tables converge.
7. A returning peer can rejoin through discovery and the normal `JOIN` process.

A timeout indicates that a peer is unreachable, not necessarily that it has permanently crashed. Network partitions and temporary congestion can also cause missed heartbeats.

## 9. Bandwidth Considerations

Heartbeats are small UDP messages. For four peers, if every peer sends one heartbeat to each of the other three peers every two seconds:

- Each peer sends 3 heartbeat messages every 2 seconds.
- The whole group sends 12 heartbeat messages every 2 seconds.
- This equals approximately 6 heartbeat messages per second across the group.

This is a low background message rate for a four-peer system.

The implementation should avoid sending unnecessary heartbeat messages to unknown or inactive peers.

## 10. Consequences and Trade-offs

**Positive consequences:**

- No central coordinator is required.
- LAN peers can discover each other automatically.
- Bootstrap fallback supports networks where broadcast or multicast is unavailable.
- A fixed heartbeat timeout is simple to implement and test.
- A four-peer group generates low heartbeat traffic.

**Negative consequences:**

- Broadcast and multicast do not automatically work across network boundaries.
- Bootstrap endpoints must be configured and reachable.
- A fixed timeout can temporarily classify a slow but healthy peer as inactive.
- Membership lists may temporarily differ before peers exchange updates.

These limitations will be addressed through testing and membership synchronization.

## 11. Testing and Validation Plan
| Test ID | Test scenario | Expected result |
|---|---|---|
| ADR02-T01 | Start four peers on the same LAN | All four peers discover each other and join |
| ADR02-T02 | Start a new peer while three are active | Existing peers receive its JOIN and update membership |
| ADR02-T03 | Disable LAN broadcast discovery | Peer discovers the group through bootstrap fallback |
| ADR02-T04 | Run peers in different network environments | Reachable bootstrap endpoint enables discovery |
| ADR02-T05 | Stop one peer unexpectedly | Other peers mark it inactive after the timeout |
| ADR02-T06 | Restart an inactive peer | It can rediscover and rejoin the group |
| ADR02-T07 | Send duplicate JOIN messages | No duplicate peer entries are created |
| ADR02-T08 | Introduce temporary heartbeat packet loss | Peer is not immediately marked inactive |

## 12. Final Decision

The project will implement a hybrid peer discovery mechanism using UDP LAN broadcast as the primary method and configured bootstrap peers as a fallback. Multicast may also be supported where the network allows it.

The discovery handshake will follow:

**DISCOVER → PEER_LIST → JOIN**

Each peer will maintain its own membership table and exchange membership information with other peers.

For failure detection, the initial implementation will use a heartbeat interval of **2 seconds** and a dead-peer timeout of **8 seconds**.

This decision provides a practical balance between discovery reliability, failure detection speed, implementation complexity, and background network traffic for a decentralized group containing at least four peers.

--- 

**ADR-02 acceptance status**

- [x] Research notes document UDP broadcast, multicast, bootstrap fallback, periodic heartbeats, and phi-accrual detection.
- [x] Exact `DISCOVER → PEER_LIST → JOIN` handshake specified.
- [x] Bootstrap fallback and WSL2/cross-subnet limitations documented.
- [x] Heartbeat interval and dead-peer timeout selected with bandwidth reasoning.
- [ ] Team review and approval pending.
- [ ] Implementation and four-peer validation pending.