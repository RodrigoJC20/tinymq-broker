#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <vector>
#include "packet.h"

namespace tinymq
{

    class Broker;

    class Session : public std::enable_shared_from_this<Session>
    {
    public:
        Session(boost::asio::ip::tcp::socket socket, Broker &broker);

        void start();

        void send_packet(const Packet &packet);

        const std::string &client_id() const { return client_id_; }

        bool is_authenticated() const { return is_authenticated_; }

        std::string remote_endpoint() const;

        void handle_topic_request(const Packet &packet);
        void send_topic_list(const std::vector<std::pair<std::string, std::string>> &topics);

    private:
        void read_header();
        void read_payload(PacketHeader header);
        void process_packet(const Packet &packet);

        // Métodos principales de manejo de paquetes
        void handle_connect(const Packet &packet);
        void handle_publish(const Packet &packet);
        void handle_subscribe(const Packet &packet);
        void handle_unsubscribe(const Packet &packet);
        void handle_admin_request(const Packet &packet);

        // Métodos de administración - DECLARAR SOLO UNA VEZ
        void handle_admin_response(const Packet &packet);
        void handle_admin_list_request(const Packet &packet);
        void handle_my_topics_request(const Packet &packet);
        void handle_my_admin_requests(const Packet &packet);

        // Métodos para nueva funcionalidad de administración
        void handle_my_admin_topics_request(const Packet &packet);
        void handle_admin_resignation(const Packet &packet);
        void handle_topic_sensors_request(const Packet &packet);

        void send_ack(PacketType ack_type, uint16_t packet_id = 0);

    private:
        boost::asio::ip::tcp::socket socket_;
        Broker &broker_;
        std::string client_id_;
        bool is_authenticated_{false};
        std::vector<uint8_t> read_buffer_;
        static constexpr size_t header_length = 4;
    };

} // namespace tinymq