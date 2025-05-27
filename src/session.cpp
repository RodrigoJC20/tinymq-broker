#include "session.h"
#include "broker.h"
#include "terminal_ui.h"
#include <iostream>
#include <nlohmann/json.hpp> // AGREGAR esta línea

namespace tinymq
{

    Session::Session(boost::asio::ip::tcp::socket socket, Broker &broker)
        : socket_(std::move(socket)),
          broker_(broker),
          read_buffer_(1024)
    {
    }

    void Session::start()
    {
        read_header();
    }

    std::string Session::remote_endpoint() const
    {
        try
        {
            return socket_.remote_endpoint().address().to_string() + ":" +
                   std::to_string(socket_.remote_endpoint().port());
        }
        catch (const std::exception &)
        {
            return "unknown";
        }
    }

    void Session::read_header()
    {
        auto self = shared_from_this();
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(read_buffer_.data(), header_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec && length == header_length)
                {
                    PacketHeader header;
                    header.type = static_cast<PacketType>(read_buffer_[0]);
                    header.flags = read_buffer_[1];
                    header.payload_length = (static_cast<uint16_t>(read_buffer_[2]) << 8) | read_buffer_[3];

                    if (header.payload_length > 0)
                    {
                        read_payload(header);
                    }
                    else
                    {
                        Packet packet(header.type, header.flags, {});
                        process_packet(packet);
                    }
                }
                else
                {
                    // ui::print_message("Session", "Read header error: " + ec.message(), ui::MessageType::ERROR);
                    broker_.remove_session(shared_from_this());
                }
            });
    }

    void Session::read_payload(PacketHeader header)
    {
        auto self = shared_from_this();

        if (read_buffer_.size() < header.payload_length)
        {
            read_buffer_.resize(header.payload_length);
        }

        boost::asio::async_read(
            socket_,
            boost::asio::buffer(read_buffer_.data(), header.payload_length),
            [this, self, header](boost::system::error_code ec, std::size_t length)
            {
                if (!ec && length == header.payload_length)
                {
                    std::vector<uint8_t> payload(read_buffer_.begin(), read_buffer_.begin() + length);
                    Packet packet(header.type, header.flags, payload);

                    process_packet(packet);
                }
                else
                {
                    ui::print_message("Session", "Read payload error: " + ec.message(), ui::MessageType::ERROR);
                    broker_.remove_session(shared_from_this());
                }
            });
    }

    void Session::process_packet(const Packet &packet)
    {
        switch (packet.type())
        {
        case PacketType::CONN:
            handle_connect(packet);
            break;
        case PacketType::PUB:
            handle_publish(packet);
            break;
        case PacketType::SUB:
            handle_subscribe(packet);
            break;
        case PacketType::UNSUB:
            handle_unsubscribe(packet);
            break;
        case PacketType::TOPIC_REQ:
            handle_topic_request(packet);
            break;
        case PacketType::ADMIN_REQ:
            handle_admin_request(packet);
            break;
        case PacketType::ADMIN_RESPONSE:
            handle_admin_response(packet);
            break;
        case PacketType::ADMIN_LIST_REQ:
            handle_admin_list_request(packet);
            break;
        case PacketType::MY_TOPICS_REQ:
            handle_my_topics_request(packet);
            break;
        case PacketType::MY_ADMIN_REQ:
            handle_my_admin_requests(packet);
            break;
        case PacketType::MY_ADMIN_TOPICS_REQ:
            handle_my_admin_topics_request(packet);
            break;
        case PacketType::ADMIN_RESIGN:
            handle_admin_resignation(packet);
            break;
        case PacketType::TOPIC_SENSORS_REQ:
            handle_topic_sensors_request(packet);
            break;
        default:
            ui::print_message("Session", "Received unsupported packet type: " + std::to_string(static_cast<int>(packet.type())),
                              ui::MessageType::WARNING);
            break;
        }

        read_header();
    }

    void Session::handle_my_admin_topics_request(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client requesting admin topics", ui::MessageType::WARNING);
            return;
        }

        broker_.send_my_admin_topics(shared_from_this());
    }

    void Session::handle_admin_resignation(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to resign admin", ui::MessageType::WARNING);
            return;
        }

        const auto &payload = packet.payload();
        if (!payload.empty())
        {
            std::string topic_name(payload.begin(), payload.end());
            broker_.handle_admin_resignation(shared_from_this(), topic_name);
        }
    }

    void Session::handle_topic_sensors_request(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client requesting topic sensors", ui::MessageType::WARNING);
            return;
        }

        const auto &payload = packet.payload();
        if (!payload.empty())
        {
            std::string topic_name(payload.begin(), payload.end());
            broker_.send_topic_sensors_config(shared_from_this(), topic_name);
        }
    }

    void Session::handle_my_topics_request(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client requesting my topics",
                              ui::MessageType::WARNING);
            return;
        }

        // ui::print_message("Session", "Client " + client_id_ + " requesting own topics",
        //                   ui::MessageType::INFO);

        broker_.send_my_topics(shared_from_this());
    }

    void Session::handle_my_admin_requests(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to get their admin requests", ui::MessageType::WARNING);
            return;
        }

        // ui::print_message("Session", "Client " + client_id_ + " requesting their admin requests", ui::MessageType::INFO);

        // Llamar al broker para procesar la solicitud
        broker_.send_my_admin_requests(shared_from_this());
    }

    void Session::handle_admin_response(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to respond to admin request",
                              ui::MessageType::WARNING);
            return;
        }

        const auto &payload = packet.payload();
        if (payload.size() < 3) // Mínimo: 1 byte approved + 1 byte topic_len + 1 byte requester_len
        {
            ui::print_message("Session", "Invalid admin response packet", ui::MessageType::ERROR);
            return;
        }

        // Formato: [approved:1][topic_len:1][topic][requester_len:1][requester_id]
        bool approved = payload[0] != 0;
        uint8_t topic_len = payload[1];

        if (payload.size() < 2 + topic_len + 1)
        {
            ui::print_message("Session", "Invalid admin response packet format", ui::MessageType::ERROR);
            return;
        }

        std::string topic_name(payload.begin() + 2, payload.begin() + 2 + topic_len);
        uint8_t requester_len = payload[2 + topic_len];

        if (payload.size() < 2 + topic_len + 1 + requester_len)
        {
            ui::print_message("Session", "Invalid admin response packet format", ui::MessageType::ERROR);
            return;
        }

        std::string requester_id(payload.begin() + 3 + topic_len,
                                 payload.begin() + 3 + topic_len + requester_len);

        ui::print_message("Session", "Client " + client_id_ + " responding to admin request: " + topic_name + " for " + requester_id + " - " + (approved ? "APPROVED" : "DENIED"), ui::MessageType::INFO);

        broker_.handle_admin_response(shared_from_this(), topic_name, requester_id, approved);
    }

    void Session::handle_admin_list_request(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client requesting admin list",
                              ui::MessageType::WARNING);
            return;
        }

        // ui::print_message("Session", "Client " + client_id_ + " requesting admin requests list",
        //                   ui::MessageType::INFO);

        broker_.send_admin_requests_list(shared_from_this());
    }

    void Session::handle_admin_request(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to request admin", ui::MessageType::WARNING);
            return;
        }

        const auto &payload = packet.payload();
        if (!payload.empty())
        {
            std::string topic_name(payload.begin(), payload.end());

            ui::print_message("Session", "Client " + client_id_ + " requesting admin for topic: " + topic_name, ui::MessageType::INFO);

            // Llamar al broker para procesar la solicitud
            broker_.handle_admin_request(shared_from_this(), topic_name);
        }
    }

    void Session::handle_topic_request(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to request topics", ui::MessageType::WARNING);
            return;
        }

        // ui::print_message("Session", "Client " + client_id_ + " requesting published topics", ui::MessageType::INFO);

        broker_.send_published_topics(shared_from_this());
    }

    void Session::send_topic_list(const std::vector<std::pair<std::string, std::string>> &topics)
    {

        // Serializar la lista de tópicos en formato JSON
        std::string json = "[";
        bool first = true;

        for (const auto &topic_pair : topics)
        {
            if (!first)
                json += ",";
            first = false;

            json += "{\"name\":\"" + topic_pair.first + "\",\"owner\":\"" + topic_pair.second + "\"}";
        }

        json += "]";

        std::vector<uint8_t> payload(json.begin(), json.end());
        Packet response_packet(PacketType::TOPIC_RESP, 0, payload);
        send_packet(response_packet);
    }

    void Session::handle_connect(const Packet &packet)
    {
        const auto &payload = packet.payload();
        if (!payload.empty())
        {
            client_id_ = std::string(payload.begin(), payload.end());
            is_authenticated_ = true;

            ui::print_message("Session", "Client connected: " + client_id_ + " from " + remote_endpoint(), ui::MessageType::SUCCESS);

            send_ack(PacketType::CONNACK);

            broker_.register_session(shared_from_this());
        }
        else
        {
            ui::print_message("Session", "Invalid CONNECT packet (empty client ID)", ui::MessageType::ERROR);
            socket_.close();
        }
    }

    void Session::handle_publish(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to publish", ui::MessageType::WARNING);
            return;
        }

        const auto &payload = packet.payload();

        if (payload.size() > 1)
        {
            uint8_t topic_length = payload[0];

            if (payload.size() > topic_length + 1)
            {
                std::string topic(payload.begin() + 1, payload.begin() + 1 + topic_length);
                std::vector<uint8_t> message_payload(payload.begin() + 1 + topic_length, payload.end());

                std::string msg_preview;
                for (size_t i = 0; i < std::min(message_payload.size(), size_t(20)); ++i)
                {
                    char c = static_cast<char>(message_payload[i]);
                    if (isprint(c))
                    {
                        msg_preview += c;
                    }
                    else
                    {
                        msg_preview += '?';
                    }
                }
                if (message_payload.size() > 20)
                {
                    msg_preview += "...";
                }

                ui::print_message("Session", "Client " + client_id_ + " published to topic '" + topic + "': " + msg_preview, ui::MessageType::OUTGOING);

                broker_.publish(topic, message_payload);

                send_ack(PacketType::PUBACK);
            }
        }
    }

    void Session::handle_subscribe(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to subscribe", ui::MessageType::WARNING);
            return;
        }

        const auto &payload = packet.payload();

        if (!payload.empty())
        {
            std::string topic(payload.begin(), payload.end());

            // ui::print_message("Session", "Client " + client_id_ + " subscribing to topic: " + topic,
            //                   ui::MessageType::INFO);

            broker_.subscribe(shared_from_this(), topic);

            send_ack(PacketType::SUBACK);
        }
    }

    void Session::handle_unsubscribe(const Packet &packet)
    {
        if (!is_authenticated_)
        {
            ui::print_message("Session", "Unauthenticated client trying to unsubscribe", ui::MessageType::WARNING);
            return;
        }

        const auto &payload = packet.payload();

        if (!payload.empty())
        {
            std::string topic(payload.begin(), payload.end());

            // ui::print_message("Session", "Client " + client_id_ + " unsubscribing from topic: " + topic,
            //                   ui::MessageType::INFO);

            broker_.unsubscribe(shared_from_this(), topic);

            send_ack(PacketType::UNSUBACK);
        }
    }

    void Session::send_ack(PacketType ack_type, uint16_t packet_id)
    {
        std::vector<uint8_t> payload;
        if (packet_id > 0)
        {
            payload.push_back(static_cast<uint8_t>(packet_id >> 8));
            payload.push_back(static_cast<uint8_t>(packet_id & 0xFF));
        }

        Packet ack_packet(ack_type, 0, payload);
        send_packet(ack_packet);
    }

    void Session::send_packet(const Packet &packet)
    {
        auto serialized = packet.serialize();
        auto self = shared_from_this();

        boost::asio::async_write(
            socket_,
            boost::asio::buffer(serialized),
            [this, self](boost::system::error_code ec, std::size_t /*length*/)
            {
                if (ec)
                {
                    ui::print_message("Session", "Write error: " + ec.message(), ui::MessageType::ERROR);
                    broker_.remove_session(shared_from_this());
                }
            });
    }

} // namespace tinymq