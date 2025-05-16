#include "broker.h"
#include "session.h"
#include "terminal_ui.h"
#include <algorithm>
#include <iostream>

namespace tinymq {

Broker::Broker(uint16_t port, size_t thread_pool_size, const std::string& db_connection_str)
    : io_context_(),
      acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      thread_pool_size_(thread_pool_size),
      running_(false) {
    
    if (!db_connection_str.empty()) {
        db_manager_ = std::make_unique<DbManager>(db_connection_str);
        ui::print_message("Broker", "Database support enabled", ui::MessageType::INFO);
    } else {
        ui::print_message("Broker", "Running without database support", ui::MessageType::WARNING);
    }
}

Broker::~Broker() {
    stop();
}

void Broker::start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    
    // Initialize database if configured
    if (db_manager_) {
        if (!db_manager_->initialize()) {
            ui::print_message("Broker", "Failed to initialize database, continuing without DB support", 
                             ui::MessageType::ERROR);
            db_manager_.reset();
        }
    }
    
    accept_connections();
    
    threads_.reserve(thread_pool_size_);
    for (size_t i = 0; i < thread_pool_size_; ++i) {
        threads_.emplace_back([this]() {
            try {
                io_context_.run();
            } catch (const std::exception& e) {
                ui::print_message("Thread", "Exception: " + std::string(e.what()), ui::MessageType::ERROR);
            }
        });
    }
    
    ui::print_message("Broker", "Started on port " + std::to_string(acceptor_.local_endpoint().port()) + 
                     " with " + std::to_string(thread_pool_size_) + " threads", 
                     ui::MessageType::SUCCESS);
}

void Broker::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    acceptor_.close();
    
    io_context_.stop();
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(topics_mutex_);
        topic_subscribers_.clear();
    }
    
    threads_.clear();
    
    ui::print_message("Broker", "Stopped", ui::MessageType::INFO);
}

void Broker::accept_connections() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec) {
                auto session = std::make_shared<Session>(std::move(socket), *this);
                ui::print_message("Broker", "New connection from " + 
                                session->remote_endpoint(), ui::MessageType::INCOMING);
                session->start();
            } else {
                ui::print_message("Broker", "Accept error: " + ec.message(), ui::MessageType::ERROR);
            }
            
            if (running_) {
                accept_connections();
            }
        });
}

void Broker::register_session(std::shared_ptr<Session> session) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto& client_id = session->client_id();
    
    // Register in database if enabled
    if (db_manager_ && session->is_authenticated()) {
        try {
            auto endpoint = session->remote_endpoint();
            size_t pos = endpoint.find_last_of(':');
            std::string ip = endpoint.substr(0, pos);
            int port = std::stoi(endpoint.substr(pos + 1));
            
            db_manager_->register_client(client_id, ip, port);
        } catch (const std::exception& e) {
            ui::print_message("Broker", "Error registering client in DB: " + std::string(e.what()), 
                             ui::MessageType::ERROR);
        }
    }
    
    auto it = sessions_.find(client_id);
    if (it != sessions_.end()) {
        std::shared_ptr<Session> old_session = it->second;
        
        ui::print_message("Broker", "Client ID already in use, disconnecting old session: " + client_id, 
                        ui::MessageType::WARNING);
        
        {
            std::lock_guard<std::mutex> topics_lock(topics_mutex_);
            for (auto& topic_entry : topic_subscribers_) {
                auto& subscribers = topic_entry.second;
                subscribers.erase(
                    std::remove_if(
                        subscribers.begin(),
                        subscribers.end(),
                        [&old_session](const std::shared_ptr<Session>& s) {
                            return s == old_session;
                        }),
                    subscribers.end());
            }
        }
        
        it->second.reset();
    }
    
    sessions_[client_id] = session;
    ui::print_message("Broker", "Session registered: " + client_id, ui::MessageType::SUCCESS);
}

void Broker::remove_session(std::shared_ptr<Session> session) {
    const auto& client_id = session->client_id();
    
    if (client_id.empty()) {
        return;
    }
    
    // Log client disconnect if database is enabled
    if (db_manager_) {
        db_manager_->log_client_disconnect(client_id);
    }
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.erase(client_id);
    }
    
    {
        std::lock_guard<std::mutex> lock(topics_mutex_);
        for (auto& topic_entry : topic_subscribers_) {
            auto& subscribers = topic_entry.second;
            subscribers.erase(
                std::remove_if(
                    subscribers.begin(),
                    subscribers.end(),
                    [&session](const std::shared_ptr<Session>& s) {
                        return s == session;
                    }),
                subscribers.end());
        }
    }
    
    ui::print_message("Broker", "Session removed: " + client_id, ui::MessageType::INFO);
}

void Broker::subscribe(std::shared_ptr<Session> session, const std::string& topic) {
    const auto& client_id = session->client_id();
    
    // Register topic in database if needed
    int topic_id = -1;
    if (db_manager_) {
        // Check topic cache first
        {
            std::lock_guard<std::mutex> lock(topic_cache_mutex_);
            auto it = topic_id_cache_.find(topic);
            if (it != topic_id_cache_.end()) {
                topic_id = it->second;
            }
        }
        
        if (topic_id == -1) {
            // Get client_id from topic format: [client_id]/topic_name
            std::string owner_client_id = extract_client_id_from_topic(topic);
            
            // Register topic if we can determine the owner
            if (!owner_client_id.empty()) {
                db_manager_->register_topic(topic, owner_client_id);
            }
            
            // Get the topic ID
            topic_id = db_manager_->get_topic_id(topic);
            
            // Cache it if found
            if (topic_id != -1) {
                std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                topic_id_cache_[topic] = topic_id;
            }
        }
        
        // Add subscription to database
        if (topic_id != -1) {
            db_manager_->add_subscription(client_id, topic_id);
        }
    }
    
    std::lock_guard<std::mutex> lock(topics_mutex_);
    
    auto& subscribers = topic_subscribers_[topic];
    if (std::find(subscribers.begin(), subscribers.end(), session) == subscribers.end()) {
        subscribers.push_back(session);
        ui::print_message("Topic", "Client " + client_id + 
                        " subscribed to topic: " + topic, ui::MessageType::INFO);
    }
}

void Broker::unsubscribe(std::shared_ptr<Session> session, const std::string& topic) {
    const auto& client_id = session->client_id();
    
    // Update database
    if (db_manager_) {
        int topic_id = -1;
        
        // Check topic cache
        {
            std::lock_guard<std::mutex> lock(topic_cache_mutex_);
            auto it = topic_id_cache_.find(topic);
            if (it != topic_id_cache_.end()) {
                topic_id = it->second;
            }
        }
        
        // If not in cache, try to get from DB
        if (topic_id == -1) {
            topic_id = db_manager_->get_topic_id(topic);
            
            // Cache it if found
            if (topic_id != -1) {
                std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                topic_id_cache_[topic] = topic_id;
            }
        }
        
        // Remove subscription in DB
        if (topic_id != -1) {
            db_manager_->remove_subscription(client_id, topic_id);
        }
    }
    
    std::lock_guard<std::mutex> lock(topics_mutex_);
    
    // Find the topic
    auto it = topic_subscribers_.find(topic);
    if (it != topic_subscribers_.end()) {
        auto& subscribers = it->second;
        subscribers.erase(
            std::remove(subscribers.begin(), subscribers.end(), session),
            subscribers.end());
        
        ui::print_message("Topic", "Client " + client_id + 
                        " unsubscribed from topic: " + topic, ui::MessageType::INFO);
        
        if (subscribers.empty()) {
            topic_subscribers_.erase(it);
        }
    }
}

void Broker::publish(const std::string& topic, const std::vector<uint8_t>& message) {
    std::vector<std::shared_ptr<Session>> subscribers;
    
    {
        std::lock_guard<std::mutex> lock(topics_mutex_);
        auto it = topic_subscribers_.find(topic);
        if (it != topic_subscribers_.end()) {
            subscribers = it->second;
        }
    }
    
    // Extract client ID from the publisher (from topic format: [client_id]/topic_name)
    std::string publisher_client_id = extract_client_id_from_topic(topic);
    
    // Log to database if enabled
    if (db_manager_ && !publisher_client_id.empty()) {
        int topic_id = -1;
        
        // Check topic cache
        {
            std::lock_guard<std::mutex> lock(topic_cache_mutex_);
            auto it = topic_id_cache_.find(topic);
            if (it != topic_id_cache_.end()) {
                topic_id = it->second;
            }
        }
        
        // If not in cache, try to get from DB
        if (topic_id == -1) {
            topic_id = db_manager_->get_topic_id(topic);
            
            // Register topic if not found
            if (topic_id == -1) {
                db_manager_->register_topic(topic, publisher_client_id);
                topic_id = db_manager_->get_topic_id(topic);
            }
            
            // Cache it if found
            if (topic_id != -1) {
                std::lock_guard<std::mutex> lock(topic_cache_mutex_);
                topic_id_cache_[topic] = topic_id;
            }
        }
        
        if (topic_id != -1) {
            // Generate message preview
            std::string msg_preview;
            for (size_t i = 0; i < std::min(message.size(), size_t(20)); ++i) {
                char c = static_cast<char>(message[i]);
                if (isprint(c)) {
                    msg_preview += c;
                } else {
                    msg_preview += '?';
                }
            }
            if (message.size() > 20) {
                msg_preview += "...";
            }
            
            db_manager_->log_message(publisher_client_id, topic_id, message.size(), msg_preview);
        }
    }
    
    if (subscribers.empty()) {
        ui::print_message("Topic", "No subscribers for topic: " + topic, ui::MessageType::INFO);
        return;
    }
    
    ui::print_message("Topic", "Publishing to " + std::to_string(subscribers.size()) + 
                   " subscribers on topic: " + topic, ui::MessageType::OUTGOING);
    
    std::vector<uint8_t> payload;
    
    payload.push_back(static_cast<uint8_t>(topic.size()));
    
    payload.insert(payload.end(), topic.begin(), topic.end());
    
    payload.insert(payload.end(), message.begin(), message.end());
    
    Packet packet(PacketType::PUB, 0, payload);
    
    for (auto& subscriber : subscribers) {
        subscriber->send_packet(packet);
    }
}

std::string Broker::extract_client_id_from_topic(const std::string& topic) {
    size_t pos = topic.find('/');
    if (pos != std::string::npos) {
        return topic.substr(0, pos);
    }
    return ""; // Not a valid topic format
}

} // namespace tinymq 