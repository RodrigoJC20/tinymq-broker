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
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
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

-- Solicitudes de administración
CREATE TABLE admin_requests (
  id SERIAL PRIMARY KEY,
  topic_id INTEGER REFERENCES topics(id),
  requester_client_id TEXT REFERENCES clients(client_id),
  status TEXT CHECK (status IN ('pending', 'approved', 'rejected')),
  request_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  response_timestamp TIMESTAMP,
  UNIQUE(topic_id, requester_client_id)
);

-- Administradores activos (solo puede haber uno adicional por tópico)
CREATE TABLE topic_admins (
  topic_id INTEGER REFERENCES topics(id),
  admin_client_id TEXT REFERENCES clients(client_id),
  granted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (topic_id, admin_client_id)
);

-- Configuración de sensores por administrador
CREATE TABLE admin_sensor_config (
  topic_id INTEGER REFERENCES topics(id),
  sensor_name TEXT,
  active BOOLEAN DEFAULT TRUE,
  set_by TEXT REFERENCES clients(client_id),
  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (topic_id, sensor_name)
);

-- Create indexes for better query performance
CREATE INDEX idx_topics_owner ON topics(owner_client_id);
CREATE INDEX idx_subscriptions_client ON subscriptions(client_id);
CREATE INDEX idx_subscriptions_topic ON subscriptions(topic_id);
CREATE INDEX idx_message_logs_publisher ON message_logs(publisher_client_id);
CREATE INDEX idx_message_logs_topic ON message_logs(topic_id);
CREATE INDEX idx_connection_events_client ON connection_events(client_id); 