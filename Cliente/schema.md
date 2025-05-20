# TinyMQ Client Database Schema

This document outlines the schema for the SQLite database used by the TinyMQ client.

## Tables

### `config`

Stores client configuration settings.

| Column | Type | Constraints | Description |
|---|---|---|---|
| `key` | TEXT | PRIMARY KEY | The configuration key (e.g., "client_id", "metadata") |
| `value` | TEXT |  | The configuration value |

### `sensors`

Stores information about individual sensors.

| Column | Type | Constraints | Description |
|---|---|---|---|
| `id` | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique identifier for the sensor |
| `name` | TEXT | UNIQUE | Name of the sensor (e.g., "temperature_kitchen") |
| `last_value` | TEXT |  | The last recorded value for this sensor |
| `last_updated` | INTEGER |  | Timestamp (Unix epoch) of the last update |

### `readings`

Stores historical readings for each sensor.

| Column | Type | Constraints | Description |
|---|---|---|---|
| `id` | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique identifier for the reading |
| `sensor_id` | INTEGER | FOREIGN KEY(sensor_id) REFERENCES sensors(id) | ID of the sensor this reading belongs to |
| `timestamp` | INTEGER |  | Timestamp (Unix epoch) of the reading |
| `value` | TEXT |  | The sensor value |
| `units` | TEXT |  | Units of the sensor value (e.g., "C", "mph") |

### `topics`

Stores information about topics that can be published or subscribed to.

| Column | Type | Constraints | Description |
|---|---|---|---|
| `id` | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique identifier for the topic |
| `name` | TEXT | UNIQUE | Name of the topic (e.g., "home/livingroom/temperature") |
| `publish` | BOOLEAN | DEFAULT 0 | Flag indicating if this topic's sensor data should be published to the broker (0 = False, 1 = True) |

### `topic_sensors`

A many-to-many relationship table linking sensors to topics. This defines which sensors' data is included when a topic is published.

| Column | Type | Constraints | Description |
|---|---|---|---|
| `topic_id` | INTEGER | PRIMARY KEY, FOREIGN KEY(topic_id) REFERENCES topics(id) | ID of the topic |
| `sensor_id` | INTEGER | PRIMARY KEY, FOREIGN KEY(sensor_id) REFERENCES sensors(id) | ID of the sensor |

### `subscriptions`

Stores information about subscriptions to topics from other clients or sources.

| Column | Type | Constraints | Description |
|---|---|---|---|
| `id` | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique identifier for the subscription |
| `topic` | TEXT |  | The topic string being subscribed to |
| `source_client_id` | TEXT |  | Identifier of the client/source that published the data on this topic |
| `active` | BOOLEAN | DEFAULT 1 | Flag indicating if this subscription is currently active (0 = False, 1 = True) |

### `subscription_data`

Stores data received from subscribed topics.

| Column | Type | Constraints | Description |
|---|---|---|---|
| `id` | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique identifier for the data point |
| `subscription_id` | INTEGER | FOREIGN KEY(subscription_id) REFERENCES subscriptions(id) | ID of the subscription this data belongs to |
| `timestamp` | INTEGER |  | Timestamp (Unix epoch) when the data was received/recorded |
| `data` | TEXT |  | The actual data payload received for the subscription (often JSON) | 