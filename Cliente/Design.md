# TinyMQ Client - Design Document

## Overview

The TinyMQ Client is a lightweight IoT client implementation for the TinyMQ protocol. It collects sensor data from a local ESP32-powered smart house kit and handles local storage, optional remote publishing via a broker, and user access through CLI and GUI interfaces. By default, all data remains local and private unless explicitly shared by the user.

Clients can also **subscribe to data published by other clients** on the same local network, enabling a fully decentralized pub/sub model.

---

## TinyMQ Protocol Summary

Each TinyMQ packet has the following structure:

- **Packet Type (1 byte)**: One of the predefined message types.
- **Flags (1 byte)**: Reserved for future use.
- **Payload Length (2 bytes)**: 16-bit unsigned integer.
- **Payload (variable)**: Content depending on packet type.

### Supported Packet Types

| Name       | Code  | Purpose                          |
|------------|-------|----------------------------------|
| CONN       | 0x01  | Connection request               |
| CONNACK    | 0x02  | Connection acknowledgment        |
| PUB        | 0x03  | Publish message                  |
| PUBACK     | 0x04  | Publish acknowledgment           |
| SUB        | 0x05  | Subscribe to topic               |
| SUBACK     | 0x06  | Subscribe acknowledgment         |
| UNSUB      | 0x07  | Unsubscribe from topic           |
| UNSUBACK   | 0x08  | Unsubscribe acknowledgment       |

---

## Client Architecture

### 1. **Data Acquisition Service (DAS)**
- TCP server running on PC.
- Receives sensor data (every 1s) from ESP32.
- Parses and validates the data received.
- Saves sensor readings into a local SQLite database.

### 2. **Client Identity and Metadata**

#### Client ID
- A required **unique Client ID** (e.g., school-assigned student ID).
- Set **once** via CLI if not configured already.
- Used to tag all published packets and help others identify the source.
- Stored locally (e.g., config file).

#### Optional Metadata
- User-defined fields for friendliness:
  - **Name** (e.g., "Rick")
  - **Email** (optional, display only)
- Settable via CLI/GUI
- No authentication, purely for visibility and UI

### 3. **Local Storage (SQLite)**
- Lightweight, embedded DB for persistence.
- Stores time-series data:
  - Timestamp
  - Sensor ID / name
  - Value
  - Units (optional)
  - Source (Client ID)

### 4. **Access Interface**

#### a. Command Line Interface (CLI)
- View active sensors
- Query sensor history
- Toggle publishing to broker
- Subscribe/unsubscribe to other clients
- Set Client ID (if unset)
- Set/display user metadata

#### b. Graphical User Interface (GUI)
- Dashboard with sensor history (charts)
- Status indicators (sensor activity)
- Option to publish/subscribe to topics
- Set and display client ID and metadata

### 5. **Broker Communication (Optional)**
- User-controlled toggle to start publishing to broker.
- Published topics tagged with `Client ID` and metadata.
- Client can subscribe to topics shared by others.
- Only `PUB`, `SUB`, and `UNSUB` packets used.

---

## Privacy & Data Ownership

- All data is private by default.
- User decides when to start or stop publishing.
- Clients can subscribe only to topics explicitly shared on the local network.

---

## To-Do List (Ordered by Priority)

1. **Define sensor data format** expected from ESP32.
2. **Implement Data Acquisition Service (DAS)** TCP listener.
3. **Design and initialize SQLite schema** for:
   - Local readings
   - Subscribed readings
   - Source metadata
4. **Add persistent Client ID configuration** with CLI prompt if not set.
5. **Implement TinyMQ packet parsing logic** in the DAS.
6. **Store incoming sensor readings in SQLite.**
7. **Create CLI interface** to:
   - Set Client ID and metadata
   - List sensors and history
   - Toggle publishing
   - Subscribe/unsubscribe to topics
8. **Implement GUI frontend** with:
   - Realtime dashboard
   - Historical graphs
   - Topic discovery & subscriptions
   - Metadata and ID configuration
9. **Implement broker communication logic**:
   - Advertise/publish to broker
   - Discover and subscribe to topics
10. **Test with multiple clients on LAN**
11. **(Optional)** Add security layer for broker communication

---

## Notes

- No login or password mechanism
- No central registry; IDs are self-managed
- Broker implementation is out-of-scope