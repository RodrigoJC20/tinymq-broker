-- TinyMQ Broker PostgreSQL Schema

-- Clients table
CREATE TABLE clients (
    id SERIAL PRIMARY KEY,
    client_id VARCHAR(255) UNIQUE NOT NULL,
    last_connected TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    last_ip VARCHAR(45),  -- Can store IPv6 addresses
    last_port INTEGER,
    connection_count INTEGER DEFAULT 1
);

-- Topics table
CREATE TABLE topics (
    id SERIAL PRIMARY KEY,
    name VARCHAR(255) UNIQUE NOT NULL,
    owner_client_id VARCHAR(255) NOT NULL REFERENCES clients(client_id),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    publish BOOLEAN DEFAULT FALSE,
);

-- Subscriptions table - tracks who is subscribed to what
CREATE TABLE subscriptions (
    id SERIAL PRIMARY KEY,
    client_id VARCHAR(255) NOT NULL REFERENCES clients(client_id),
    topic_id INTEGER NOT NULL REFERENCES topics(id),
    subscribed_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    active BOOLEAN DEFAULT TRUE,
    UNIQUE (client_id, topic_id)
);

-- Message log table - tracks all published messages
CREATE TABLE message_logs (
    id SERIAL PRIMARY KEY,
    publisher_client_id VARCHAR(255) NOT NULL REFERENCES clients(client_id),
    topic_id INTEGER NOT NULL REFERENCES topics(id),
    payload_size INTEGER NOT NULL,
    payload_preview VARCHAR(100),
    published_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- Connection events - tracks connects/disconnects
CREATE TABLE connection_events (
    id SERIAL PRIMARY KEY,
    client_id VARCHAR(255) NOT NULL REFERENCES clients(client_id),
    event_type VARCHAR(20) NOT NULL, -- 'CONNECT', 'DISCONNECT'
    ip_address VARCHAR(45),
    port INTEGER,
    timestamp TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- Create indexes for better query performance
CREATE INDEX idx_topics_owner ON topics(owner_client_id);
CREATE INDEX idx_subscriptions_client ON subscriptions(client_id);
CREATE INDEX idx_subscriptions_topic ON subscriptions(topic_id);
CREATE INDEX idx_message_logs_publisher ON message_logs(publisher_client_id);
CREATE INDEX idx_message_logs_topic ON message_logs(topic_id);
CREATE INDEX idx_connection_events_client ON connection_events(client_id); 