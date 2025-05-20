# TinyMQ Broker Database Schema

This document details the PostgreSQL database schema used by the TinyMQ broker for tracking clients, topics, subscriptions, and message activity.

## Overview

The TinyMQ broker uses PostgreSQL to persistently store:
- Client connection information
- Topic registrations
- Subscription relationships
- Message publishing history
- Connection/disconnection events

## Tables

### `clients`

Stores information about MQTT clients that connect to the broker.

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | SERIAL | PRIMARY KEY | Auto-incrementing unique identifier |
| `client_id` | VARCHAR(255) | UNIQUE NOT NULL | Client-provided ID (must be unique) |
| `last_connected` | TIMESTAMP WITH TIME ZONE | DEFAULT CURRENT_TIMESTAMP | When the client last connected |
| `last_ip` | VARCHAR(45) | | Last IP address the client connected from |
| `last_port` | INTEGER | | Last port the client connected from |
| `connection_count` | INTEGER | DEFAULT 1 | Number of times the client has connected |

### `topics`

Stores information about message topics created by clients.

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | SERIAL | PRIMARY KEY | Auto-incrementing unique identifier |
| `name` | VARCHAR(255) | UNIQUE NOT NULL | Topic name (format: [client_id]/topic_name) |
| `owner_client_id` | VARCHAR(255) | NOT NULL REFERENCES clients(client_id) | Client ID of the topic owner |
| `created_at` | TIMESTAMP WITH TIME ZONE | DEFAULT CURRENT_TIMESTAMP | When the topic was first created |
| `publish` | BOOLEAN  | DEFAULT FALSE | Whether the topic is currently active |

### `subscriptions`

Tracks which clients are subscribed to which topics.

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | SERIAL | PRIMARY KEY | Auto-incrementing unique identifier |
| `client_id` | VARCHAR(255) | NOT NULL REFERENCES clients(client_id) | Client ID of the subscriber |
| `topic_id` | INTEGER | NOT NULL REFERENCES topics(id) | ID of the topic being subscribed to |
| `subscribed_at` | TIMESTAMP WITH TIME ZONE | DEFAULT CURRENT_TIMESTAMP | When the subscription was created |
| `active` | BOOLEAN | DEFAULT TRUE | Whether the subscription is currently active |
| | | UNIQUE (client_id, topic_id) | Prevents duplicate subscriptions |

### `message_logs`

Logs each message published to a topic.

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | SERIAL | PRIMARY KEY | Auto-incrementing unique identifier |
| `publisher_client_id` | VARCHAR(255) | NOT NULL REFERENCES clients(client_id) | Client ID of the publisher |
| `topic_id` | INTEGER | NOT NULL REFERENCES topics(id) | ID of the topic the message was published to |
| `payload_size` | INTEGER | NOT NULL | Size of the message payload in bytes |
| `payload_preview` | VARCHAR(100) | | Preview of the message content (first 20 chars) |
| `published_at` | TIMESTAMP WITH TIME ZONE | DEFAULT CURRENT_TIMESTAMP | When the message was published |

### `connection_events`

Tracks client connect and disconnect events.

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | SERIAL | PRIMARY KEY | Auto-incrementing unique identifier |
| `client_id` | VARCHAR(255) | NOT NULL REFERENCES clients(client_id) | Client ID for the event |
| `event_type` | VARCHAR(20) | NOT NULL | Type of event ('CONNECT' or 'DISCONNECT') |
| `ip_address` | VARCHAR(45) | | IP address for the event |
| `port` | INTEGER | | Port number for the event |
| `timestamp` | TIMESTAMP WITH TIME ZONE | DEFAULT CURRENT_TIMESTAMP | When the event occurred |

## Indexes

The schema includes the following indexes for improved query performance:

- `idx_topics_owner` on `topics(owner_client_id)`
- `idx_subscriptions_client` on `subscriptions(client_id)`
- `idx_subscriptions_topic` on `subscriptions(topic_id)`
- `idx_message_logs_publisher` on `message_logs(publisher_client_id)`
- `idx_message_logs_topic` on `message_logs(topic_id)`
- `idx_connection_events_client` on `connection_events(client_id)` 