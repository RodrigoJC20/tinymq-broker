bool DbManager::request_admin_status(const std::string &topic_name, const std::string &requester_id)
{
    try
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);

        // Verificar que el topic existe
        auto topic_result = exec_params(txn,
            "SELECT id, owner_client_id FROM topics WHERE name = $1",
            topic_name);

        if (topic_result.empty())
        {
            ui::print_message("Database", "Topic not found: " + topic_name, ui::MessageType::ERROR);
            return false;
        }

        int topic_id = topic_result[0]["id"].as<int>();
        std::string owner_id = topic_result[0]["owner_client_id"].as<std::string>();

        // Verificar que no sea una solicitud duplicada pendiente
        auto existing = exec_params(txn,
            "SELECT id FROM admin_requests WHERE topic_id = $1 AND requester_id = $2 AND status = 'PENDING'",
            topic_id, requester_id);

        if (!existing.empty())
        {
            ui::print_message("Database", "Admin request already exists", ui::MessageType::WARNING);
            return false;
        }

        // Insertar la nueva solicitud
        exec_params(txn,
            "INSERT INTO admin_requests (topic_id, requester_id, status) VALUES ($1, $2, 'PENDING')",
            topic_id, requester_id);

        txn.commit();
        ui::print_message("Database", "Admin request saved successfully", ui::MessageType::SUCCESS);
        return true;
    }
    catch (const std::exception &e)
    {
        ui::print_message("Database", std::string("Error requesting admin status: ") + e.what(),
                          ui::MessageType::ERROR);
        return false;
    }
}-- TinyMQ Broker PostgreSQL Schema

-- Create schema if not exists
CREATE SCHEMA IF NOT EXISTS public;

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
    publish BOOLEAN DEFAULT FALSE
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
    payload_preview VARCHAR(200),
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
CREATE INDEX idx_admin_requests_requester ON admin_requests(requester_client_id);
CREATE INDEX idx_admin_requests_topic ON admin_requests(topic_id);
CREATE INDEX idx_topic_admins_admin ON topic_admins(admin_client_id);