# TinyMQ Client

A lightweight IoT client implementation for the TinyMQ protocol. This client collects sensor data from a local ESP32-powered smart house kit, handles local storage, optional remote publishing via a broker, and provides user access through a modern command-line interface.

## Features

- **Data Acquisition Service**: Receives sensor data from ESP32 devices over TCP
- **Local Storage**: Stores all sensor readings in a local SQLite database
- **Topic Management**: Create topics and assign sensors to them
- **Publish/Subscribe**: Optionally publish topics to a broker and subscribe to topics from other clients
- **Privacy by Default**: All data remains local unless explicitly shared
- **Modern CLI**: Colorful, command-based interface for ease of use

## Requirements

- Python 3.6 or later
- No external dependencies beyond the Python Standard Library
- Terminal with color support (most modern terminals)

## Installation

1. Clone this repository:
```
git clone <repository-url>
cd tinymq-client
```

## Usage

Run the client with:

```
python cli.py
```

### First-time Setup

On first launch, you'll be prompted to enter your unique Client ID (e.g., your student ID). This ID is required and will be used to tag all published data.

### ESP32 Data Format

The client expects sensor data from ESP32 devices in JSON format, sent over TCP. Two formats are supported:

1. Array of sensor readings:
```json
[
  {"name": "temperature", "value": 22.5, "timestamp": 1635000000, "units": "C"},
  {"name": "humidity", "value": 45, "timestamp": 1635000000, "units": "%"}
]
```

2. Single sensor reading:
```json
{"name": "temperature", "value": 22.5, "timestamp": 1635000000, "units": "C"}
```

If `timestamp` is omitted, the current time will be used. The `units` field is optional.

### Command-Line Interface

The client provides a modern command-based CLI with the following main commands:

#### General Commands
- `help` - Show available commands
- `exit` or Ctrl+D - Exit the program
- `clear` - Clear the screen

#### Sensor Commands
- `sensors` - List all sensors
- `sensor <name> [limit]` - Show history for a specific sensor

#### Topic Commands
- `topics` - List all topics
- `create_topic <name> [publish]` - Create a new topic
- `topic <name>` - Show sensors in a topic
- `add_sensor <topic_name> <sensor_name>` - Add a sensor to a topic
- `remove_sensor <topic_name> <sensor_name>` - Remove a sensor from a topic
- `publish_topic <name> [on|off]` - Toggle publishing for a topic

#### Broker Commands
- `status` - Show connection status
- `connect [host] [port]` - Connect to a broker
- `disconnect` - Disconnect from the broker

#### Subscription Commands
- `subscriptions` - List active subscriptions
- `subscribe <topic> <client_id>` - Subscribe to a topic
- `unsubscribe <topic> <client_id>` - Unsubscribe from a topic
- `subscription_data <topic> <client_id> [limit]` - View subscription data

#### Identity Commands
- `id` - Show current client ID and metadata
- `set_id <new_id>` - Set client ID
- `set_metadata` - Set client metadata (name and email)

### ESP32 Simulator

The repository includes an ESP32 simulator that can generate sample sensor data for testing:

```
python esp32_simulator.py [--host HOST] [--port PORT] [--interval SECONDS] [--count COUNT]
```

Default is to connect to localhost:12345 and send data every second indefinitely.

### Publishing Data

To publish sensor data to the broker:

1. Create a topic: `create_topic temperature`
2. Add sensors to the topic: `add_sensor temperature temperature`
3. Enable publishing: `publish_topic temperature on`
4. Connect to the broker: `connect`

All sensor readings for that topic will now be published to the broker.

### Subscribing to Data

To subscribe to data from another client:

1. Connect to the broker: `connect`
2. Subscribe to a topic: `subscribe temperature client123`
3. View subscription data: `subscription_data temperature client123`

## Protocol

The TinyMQ client implements the TinyMQ protocol with the following packet types:

- **CONN (0x01)**: Connection request
- **CONNACK (0x02)**: Connection acknowledgment
- **PUB (0x03)**: Publish message
- **PUBACK (0x04)**: Publish acknowledgment
- **SUB (0x05)**: Subscribe to topic
- **SUBACK (0x06)**: Subscribe acknowledgment
- **UNSUB (0x07)**: Unsubscribe from topic
- **UNSUBACK (0x08)**: Unsubscribe acknowledgment

## Project Structure

- `tinymq/`: Core package
  - `packet.py`: TinyMQ packet implementation
  - `client.py`: TinyMQ client implementation
  - `db.py`: Database implementation
  - `das.py`: Data Acquisition Service
- `cli.py`: Command-line interface
- `esp32_simulator.py`: Simulator for testing

## License

This project is licensed under the MIT License - see the LICENSE file for details. 