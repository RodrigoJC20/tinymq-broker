# TinyMQ

A lightweight and simple implementation of an MQTT-like protocol in C++.

## Overview

TinyMQ is a minimal implementation of a publish-subscribe messaging protocol inspired by MQTT. It focuses on simplicity while providing the core functionality needed for a messaging broker:

- Basic connection handling
- Topic subscription and unsubscription
- Message publishing
- PostgreSQL database integration for activity logging

## Features

- Lightweight packet structure
- Simple broker implementation using Boost ASIO
- Thread pool for handling multiple connections concurrently
- Support for basic MQTT-like operations
- Client implementation for testing and application integration
- Database storage of clients, topics, subscriptions, and messages

## Components

- **Broker**: The server component that handles connections and routes messages
- **Database**: PostgreSQL integration for persistent storage of broker activity

## Packet Types

TinyMQ supports the following packet types:

- `CONN` (0x01): Initial connection request
- `CONNACK` (0x02): Connection acknowledgment
- `PUB` (0x03): Publish message
- `PUBACK` (0x04): Publish acknowledgment
- `SUB` (0x05): Subscribe to topic
- `SUBACK` (0x06): Subscribe acknowledgment
- `UNSUB` (0x07): Unsubscribe from topic
- `UNSUBACK` (0x08): Unsubscribe acknowledgment

## Packet Structure

Every TinyMQ packet consists of:

- Packet Type (1 byte): Identifies the type of packet
- Flags (1 byte): Reserved for future extensions
- Payload Length (2 bytes): Length of the payload data
- Payload: Variable length data depending on packet type

## Building

### Prerequisites

- C++17 compatible compiler
- Boost libraries (system and thread components)
- PostgreSQL development libraries (libpq-dev)
- libpqxx C++ PostgreSQL client library (libpqxx-dev)
- CMake (3.10 or higher)

### Installation of Dependencies

On Debian/Ubuntu based systems:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libboost-system-dev libboost-thread-dev
sudo apt-get install libpq-dev libpqxx-dev postgresql postgresql-contrib
```

### Database Setup

1. Install PostgreSQL if not already installed:
   ```bash
   sudo apt-get install postgresql postgresql-contrib
   ```

2. Create a database and user for TinyMQ:
   ```bash
   sudo -u postgres psql -c "CREATE DATABASE tinymq;"
   sudo -u postgres psql -c "CREATE USER tinymq_broker WITH PASSWORD 'tinymq_password';"
   sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE tinymq TO tinymq_broker;"
   ```

3. Allow password authentication for the user:
   ```bash
   sudo sh -c "echo 'host tinymq tinymq_broker 127.0.0.1/32 md5' >> /etc/postgresql/15/main/pg_hba.conf"
   sudo systemctl restart postgresql
   ```

   Note: The PostgreSQL version number in the path may vary. Use `ls /etc/postgresql/` to find your version.

4. Grant schema privileges:
   ```bash
   sudo -u postgres psql -d tinymq -c "GRANT ALL PRIVILEGES ON SCHEMA public TO tinymq_broker;"
   ```

### Compilation

#### Building

```bash
cd tinymq
mkdir build
cd build
cmake ..
make
```

## Usage

### Running the Broker

#### Using the startup script:

```bash
./start_broker.sh [options]
```

Options:
- `--port PORT`: Set the port number (default: 1505)
- `--threads N`: Set thread pool size (default: 4)
- `--db-host HOST`: PostgreSQL host (default: localhost)
- `--db-name NAME`: PostgreSQL database name (default: tinymq)
- `--db-user USER`: PostgreSQL user (default: tinymq_broker)
- `--db-password PASS`: PostgreSQL password (default: tinymq_password)
- `--no-database`: Run without database support

#### Direct command:

```bash
./tinymq_broker [--port PORT] [--threads NUM_THREADS] [--db CONNECTION_STRING]
```

Options:
- `--port PORT`: Set the port number (default: 1505)
- `--threads N`: Set thread pool size (default: 4)
- `--db CONNSTR`: PostgreSQL connection string (e.g. "dbname=tinymq user=tinymq_broker password=tinymq_password host=localhost")

## Database Integration

The broker stores the following information in the PostgreSQL database:
- Client connection information
- Topic registrations and ownership
- Subscription relationships
- Published message logs
- Connection/disconnection events

A detailed schema description can be found in [docs/database_schema.md](docs/database_schema.md).

### Setting Up Database Replication

For high availability and disaster recovery, PostgreSQL replication can be setup:

1. **Setup Primary Server**:
   - Configure `postgresql.conf` on the primary server:
     ```
     listen_addresses = '*'
     wal_level = replica
     max_wal_senders = 10
     max_replication_slots = 10
     ```
   - Configure access in `pg_hba.conf`:
     ```
     host replication repluser 192.168.1.0/24 md5
     ```

2. **Create a Replication User**:
   ```sql
   CREATE ROLE repluser WITH REPLICATION PASSWORD 'replpassword' LOGIN;
   ```

3. **Setup Standby Server**:
   - Stop PostgreSQL on the standby
   - Backup primary data to standby:
     ```bash
     pg_basebackup -h primary_host -D /var/lib/postgresql/15/main -U repluser -P -v -X stream
     ```
   - Create `standby.signal` file in data directory
   - Configure `postgresql.conf` on standby:
     ```
     primary_conninfo = 'host=primary_host port=5432 user=repluser password=replpassword'
     ```

4. **Start Standby Server**:
   ```bash
   sudo systemctl start postgresql
   ```

5. **Verify Replication**:
   - On primary:
     ```sql
     SELECT client_addr, state FROM pg_stat_replication;
     ```

## Future Work

- Add QoS levels
- Add persistent sessions
- Add authentication and security features
- Improve error handling
- Add wildcard topic subscriptions
- Support for retained messages
- Add database monitoring tools

## License

This project is open source and available under the MIT license. 

## Project Structure

```
tinymq/
├── CMakeLists.txt          # Broker CMake file
├── README.md               # Main project README
├── build.sh                # Build script for both components
├── start_broker.sh         # Script to start broker with database
├── db/                     # Database files
│   └── schema.sql          # PostgreSQL schema
├── docs/                   # Documentation
│   └── database_schema.md  # Database schema documentation
├── client/                 # Client directory
│   ├── CMakeLists.txt      # Client CMake file
│   ├── README.md           # Client README
│   └── src/                # Client source files
│       ├── client.cpp      # Client implementation
│       ├── client.h        # Client header
│       └── main.cpp        # Client executable
└── src/                    # Broker source files
    ├── broker.cpp          # Broker implementation
    ├── broker.h            # Broker header
    ├── db_manager.cpp      # Database manager implementation
    ├── db_manager.h        # Database manager header
    ├── main.cpp            # Broker executable
    ├── packet.cpp          # Packet implementation
    ├── packet.h            # Packet header
    ├── session.cpp         # Session implementation
    └── session.h           # Session header
``` 