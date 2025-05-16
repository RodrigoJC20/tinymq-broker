#include "db_manager.h"
#include "terminal_ui.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <pqxx/pqxx>

namespace tinymq {

DbManager::DbManager(const std::string& connection_string)
    : connection_string_(connection_string) {
}

DbManager::~DbManager() {
}

bool DbManager::initialize() {
    try {
        // Test the connection
        pqxx::connection conn(connection_string_);
        if (conn.is_open()) {
            ui::print_message("Database", "Successfully connected to PostgreSQL database", 
                             ui::MessageType::SUCCESS);
            return setup_schema();
        } else {
            ui::print_message("Database", "Failed to open database connection", 
                             ui::MessageType::ERROR);
            return false;
        }
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Database connection error: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

bool DbManager::register_client(const std::string& client_id, const std::string& ip_address, int port) {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);

        if (client_exists(txn, client_id)) {
            // Update existing client
            txn.exec_params(
                "UPDATE clients SET last_connected = CURRENT_TIMESTAMP, "
                "last_ip = $1, last_port = $2, connection_count = connection_count + 1 "
                "WHERE client_id = $3",
                ip_address, port, client_id);
        } else {
            // Insert new client
            txn.exec_params(
                "INSERT INTO clients (client_id, last_ip, last_port) VALUES ($1, $2, $3)",
                client_id, ip_address, port);
        }

        // Log the connection event
        txn.exec_params(
            "INSERT INTO connection_events (client_id, event_type, ip_address, port) "
            "VALUES ($1, 'CONNECT', $2, $3)",
            client_id, ip_address, port);

        txn.commit();
        return true;
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error registering client: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

bool DbManager::log_client_disconnect(const std::string& client_id) {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);

        // Get the last known IP and port
        auto result = txn.exec_params(
            "SELECT last_ip, last_port FROM clients WHERE client_id = $1",
            client_id);
        
        if (result.empty()) {
            ui::print_message("Database", "Client not found for disconnect: " + client_id, 
                             ui::MessageType::WARNING);
            return false;
        }

        std::string ip = result[0]["last_ip"].as<std::string>();
        int port = result[0]["last_port"].as<int>();

        // Log the disconnect event
        txn.exec_params(
            "INSERT INTO connection_events (client_id, event_type, ip_address, port) "
            "VALUES ($1, 'DISCONNECT', $2, $3)",
            client_id, ip, port);

        txn.commit();
        return true;
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error logging client disconnect: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

bool DbManager::register_topic(const std::string& topic_name, const std::string& owner_client_id) {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);
        
        if (!client_exists(txn, owner_client_id)) {
            ui::print_message("Database", "Cannot register topic: client does not exist: " + owner_client_id,
                            ui::MessageType::WARNING);
            return false;
        }

        if (!topic_exists(txn, topic_name)) {
            txn.exec_params(
                "INSERT INTO topics (name, owner_client_id) VALUES ($1, $2)",
                topic_name, owner_client_id);
            
            txn.commit();
            ui::print_message("Database", "Registered new topic: " + topic_name, 
                            ui::MessageType::INFO);
        }
        
        return true;
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error registering topic: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

int DbManager::get_topic_id(const std::string& topic_name) {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);
        
        auto result = txn.exec_params(
            "SELECT id FROM topics WHERE name = $1",
            topic_name);
        
        if (result.empty()) {
            return -1; // Topic not found
        }
        
        return result[0][0].as<int>();
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error getting topic ID: ") + e.what(), 
                         ui::MessageType::ERROR);
        return -1;
    }
}

bool DbManager::add_subscription(const std::string& client_id, int topic_id) {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);
        
        if (!client_exists(txn, client_id)) {
            ui::print_message("Database", "Cannot add subscription: client does not exist: " + client_id,
                            ui::MessageType::WARNING);
            return false;
        }
        
        // Check if topic exists
        auto topic_result = txn.exec_params(
            "SELECT name FROM topics WHERE id = $1",
            topic_id);
            
        if (topic_result.empty()) {
            ui::print_message("Database", "Cannot add subscription: topic ID does not exist: " + 
                             std::to_string(topic_id), ui::MessageType::WARNING);
            return false;
        }
        
        // Check if subscription already exists
        auto sub_result = txn.exec_params(
            "SELECT id, active FROM subscriptions WHERE client_id = $1 AND topic_id = $2",
            client_id, topic_id);
            
        if (!sub_result.empty()) {
            // Subscription exists - check if it's active
            bool active = sub_result[0]["active"].as<bool>();
            if (!active) {
                // Reactivate it
                txn.exec_params(
                    "UPDATE subscriptions SET active = TRUE, subscribed_at = CURRENT_TIMESTAMP "
                    "WHERE client_id = $1 AND topic_id = $2",
                    client_id, topic_id);
            }
        } else {
            // Create new subscription
            txn.exec_params(
                "INSERT INTO subscriptions (client_id, topic_id) VALUES ($1, $2)",
                client_id, topic_id);
        }
        
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error adding subscription: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

bool DbManager::remove_subscription(const std::string& client_id, int topic_id) {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);
        
        auto result = txn.exec_params(
            "UPDATE subscriptions SET active = FALSE "
            "WHERE client_id = $1 AND topic_id = $2",
            client_id, topic_id);
            
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error removing subscription: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

bool DbManager::log_message(const std::string& publisher_client_id, 
                           int topic_id, 
                           size_t payload_size, 
                           const std::string& payload_preview) {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        pqxx::connection conn(connection_string_);
        pqxx::work txn(conn);
        
        txn.exec_params(
            "INSERT INTO message_logs (publisher_client_id, topic_id, payload_size, payload_preview) "
            "VALUES ($1, $2, $3, $4)",
            publisher_client_id, topic_id, static_cast<int>(payload_size), payload_preview);
            
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error logging message: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

bool DbManager::client_exists(pqxx::work& txn, const std::string& client_id) {
    auto result = txn.exec_params(
        "SELECT 1 FROM clients WHERE client_id = $1",
        client_id);
    return !result.empty();
}

bool DbManager::topic_exists(pqxx::work& txn, const std::string& topic_name) {
    auto result = txn.exec_params(
        "SELECT 1 FROM topics WHERE name = $1",
        topic_name);
    return !result.empty();
}

bool DbManager::setup_schema() {
    try {
        std::lock_guard<std::mutex> lock(db_mutex_);
        
        // Check if our schema is already set up
        {
            pqxx::connection conn(connection_string_);
            pqxx::work txn(conn);
            auto result = txn.exec(
                "SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'clients')");
                
            if (result[0][0].as<bool>()) {
                ui::print_message("Database", "Database schema already exists", ui::MessageType::INFO);
                return true;
            }
            txn.commit();
        }
        
        // Read schema file
        std::ifstream schema_file("db/schema.sql");
        if (!schema_file.is_open()) {
            ui::print_message("Database", "Failed to open schema file", ui::MessageType::ERROR);
            return false;
        }
        
        std::stringstream schema_stream;
        schema_stream << schema_file.rdbuf();
        std::string schema = schema_stream.str();
        
        // Execute schema in a separate connection
        {
            pqxx::connection conn(connection_string_);
            pqxx::nontransaction ntxn(conn);
            ntxn.exec(schema);
        }
        
        ui::print_message("Database", "Database schema successfully created", ui::MessageType::SUCCESS);
        return true;
    } catch (const std::exception& e) {
        ui::print_message("Database", std::string("Error setting up schema: ") + e.what(), 
                         ui::MessageType::ERROR);
        return false;
    }
}

} // namespace tinymq 