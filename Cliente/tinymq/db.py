"""
Database module for TinyMQ client.

This module handles local storage of sensor data and configuration.
"""
import json
import sqlite3
import time
from pathlib import Path
from typing import Dict, List, Optional, Any, Union, Tuple


class Database:
    """Database for TinyMQ client."""
    
    def __init__(self, db_path: str = "tinymq.db"):
        """
        Initialize the database.
        
        Args:
            db_path: Path to the SQLite database file
        """
        self.db_path = db_path
        self._ensure_tables()
    
    def _ensure_tables(self) -> None:
        """Ensure all required tables exist."""
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            
            # Configuration table
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS config (
                    key TEXT PRIMARY KEY,
                    value TEXT
                )
            """)
            
            # Sensors table
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS sensors (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    name TEXT UNIQUE,
                    last_value TEXT,
                    last_updated INTEGER
                )
            """)
            
            # Readings table
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS readings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    sensor_id INTEGER,
                    timestamp INTEGER,
                    value TEXT,
                    units TEXT,
                    FOREIGN KEY(sensor_id) REFERENCES sensors(id)
                )
            """)
            
            # Topics table
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS topics (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    name TEXT UNIQUE,
                    publish BOOLEAN DEFAULT 0
                )
            """)
            
            # Topic sensors (many-to-many relationship)
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS topic_sensors (
                    topic_id INTEGER,
                    sensor_id INTEGER,
                    PRIMARY KEY (topic_id, sensor_id),
                    FOREIGN KEY(topic_id) REFERENCES topics(id),
                    FOREIGN KEY(sensor_id) REFERENCES sensors(id)
                )
            """)
            
            # Subscriptions table
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS subscriptions (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    topic TEXT,
                    source_client_id TEXT,
                    active BOOLEAN DEFAULT 1
                )
            """)
            
            # Subscription data
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS subscription_data (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    subscription_id INTEGER,
                    timestamp INTEGER,
                    data TEXT,
                    FOREIGN KEY(subscription_id) REFERENCES subscriptions(id)
                )
            """)
            
            conn.commit()
    
    # Configuration methods
    
    def get_client_id(self) -> Optional[str]:
        """
        Get the client ID.
        
        Returns:
            The client ID, or None if not set
        """
        return self.get_config("client_id")
    
    def set_client_id(self, client_id: str) -> None:
        """
        Set the client ID.
        
        Args:
            client_id: The client ID to set
        """
        self.set_config("client_id", client_id)
    
    def get_client_metadata(self) -> Dict[str, str]:
        """
        Get the client metadata.
        
        Returns:
            A dictionary of metadata
        """
        metadata_str = self.get_config("metadata")
        if metadata_str:
            try:
                return json.loads(metadata_str)
            except json.JSONDecodeError:
                pass
        return {}
    
    def set_client_metadata(self, metadata: Dict[str, str]) -> None:
        """
        Set the client metadata.
        
        Args:
            metadata: A dictionary of metadata
        """
        self.set_config("metadata", json.dumps(metadata))
    
    def get_config(self, key: str) -> Optional[str]:
        """
        Get a configuration value.
        
        Args:
            key: The configuration key
            
        Returns:
            The configuration value, or None if not found
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT value FROM config WHERE key = ?", (key,))
            row = cursor.fetchone()
            if row:
                return row[0]
            return None
    
    def set_config(self, key: str, value: str) -> None:
        """
        Set a configuration value.
        
        Args:
            key: The configuration key
            value: The configuration value
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute(
                "INSERT OR REPLACE INTO config (key, value) VALUES (?, ?)",
                (key, value)
            )
            conn.commit()
    
    # Sensor methods
    
    def get_sensors(self) -> List[Dict[str, Any]]:
        """
        Get all sensors.
        
        Returns:
            A list of sensors with id, name, last_value, and last_updated
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            cursor.execute("SELECT id, name, last_value, last_updated FROM sensors ORDER BY id")
            return [dict(row) for row in cursor.fetchall()]
    
    def get_sensor(self, sensor_id_or_name: str) -> Optional[Dict[str, Any]]:
        """
        Get a sensor by ID or name.
        
        Args:
            sensor_id_or_name: Either sensor ID (numeric) or name
            
        Returns:
            Sensor data or None if not found
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            
            try:
                # Try as ID first
                sensor_id = int(sensor_id_or_name)
                cursor.execute(
                    "SELECT id, name, last_value, last_updated FROM sensors WHERE id = ?",
                    (sensor_id,)
                )
            except ValueError:
                # Not a number, try as name
                cursor.execute(
                    "SELECT id, name, last_value, last_updated FROM sensors WHERE name = ?",
                    (sensor_id_or_name,)
                )
            
            row = cursor.fetchone()
            return dict(row) if row else None
    
    def add_reading(self, name: str, value: str, timestamp: Optional[int] = None,
                   units: str = "") -> None:
        """
        Add a sensor reading.
        
        Args:
            name: The sensor name
            value: The sensor value
            timestamp: The timestamp (defaults to current time)
            units: The units (optional)
        """
        if timestamp is None:
            timestamp = int(time.time())
        
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            
            # Get or create sensor
            cursor.execute(
                """
                INSERT OR IGNORE INTO sensors (name, last_value, last_updated)
                VALUES (?, ?, ?)
                """,
                (name, value, timestamp)
            )
            
            cursor.execute("SELECT id FROM sensors WHERE name = ?", (name,))
            sensor_id = cursor.fetchone()[0]
            
            # Update sensor last value
            cursor.execute(
                """
                UPDATE sensors 
                SET last_value = ?, last_updated = ? 
                WHERE id = ?
                """,
                (value, timestamp, sensor_id)
            )
            
            # Add reading
            cursor.execute(
                """
                INSERT INTO readings (sensor_id, timestamp, value, units)
                VALUES (?, ?, ?, ?)
                """,
                (sensor_id, timestamp, value, units)
            )
            
            conn.commit()
    
    def get_readings(self, sensor_name: str, limit: int = 100,
                    start_time: Optional[int] = None,
                    end_time: Optional[int] = None) -> List[Dict[str, Any]]:
        """
        Get sensor readings.
        
        Args:
            sensor_name: The sensor name
            limit: Maximum number of readings to return
            start_time: Start timestamp (optional)
            end_time: End timestamp (optional)
            
        Returns:
            A list of readings
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            
            query = """
                SELECT r.timestamp, r.value, r.units
                FROM readings r
                JOIN sensors s ON r.sensor_id = s.id
                WHERE s.name = ?
            """
            params = [sensor_name]
            
            if start_time is not None:
                query += " AND r.timestamp >= ?"
                params.append(start_time)
            
            if end_time is not None:
                query += " AND r.timestamp <= ?"
                params.append(end_time)
            
            query += " ORDER BY r.timestamp DESC LIMIT ?"
            params.append(limit)
            
            cursor.execute(query, params)
            return [dict(row) for row in cursor.fetchall()]
    
    # Topic methods
    
    def get_topics(self) -> List[Dict[str, Any]]:
        """
        Get all topics.
        
        Returns:
            A list of topics with id, name, and publish flag
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            cursor.execute("SELECT id, name, publish FROM topics ORDER BY id")
            return [dict(row) for row in cursor.fetchall()]
    
    def get_topic(self, topic_id_or_name: str) -> Optional[Dict[str, Any]]:
        """
        Get a topic by ID or name.
        
        Args:
            topic_id_or_name: Either topic ID (numeric) or name
            
        Returns:
            Topic data or None if not found
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            
            try:
                # Try as ID first
                topic_id = int(topic_id_or_name)
                cursor.execute(
                    "SELECT id, name, publish FROM topics WHERE id = ?",
                    (topic_id,)
                )
            except ValueError:
                # Not a number, try as name
                cursor.execute(
                    "SELECT id, name, publish FROM topics WHERE name = ?",
                    (topic_id_or_name,)
                )
            
            row = cursor.fetchone()
            return dict(row) if row else None
    
    def create_topic(self, name: str, publish: bool = False) -> int:
        """
        Create a new topic.
        
        Args:
            name: The topic name
            publish: Whether to publish the topic to the broker
            
        Returns:
            The topic ID
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute(
                "INSERT OR IGNORE INTO topics (name, publish) VALUES (?, ?)",
                (name, publish)
            )
            
            if cursor.rowcount == 0:
                # Topic already exists, update publish flag
                cursor.execute(
                    "UPDATE topics SET publish = ? WHERE name = ?",
                    (publish, name)
                )
            
            cursor.execute("SELECT id FROM topics WHERE name = ?", (name,))
            topic_id = cursor.fetchone()[0]
            
            conn.commit()
            return topic_id
    
    def set_topic_publish(self, name: str, publish: bool) -> None:
        """
        Set whether to publish a topic.
        
        Args:
            name: The topic name
            publish: Whether to publish the topic to the broker
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute(
                "UPDATE topics SET publish = ? WHERE name = ?",
                (publish, name)
            )
            conn.commit()
    
    def add_sensor_to_topic(self, topic_name: str, sensor_name: str) -> None:
        """
        Add a sensor to a topic.
        
        Args:
            topic_name: The topic name
            sensor_name: The sensor name
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            
            # Get topic ID
            cursor.execute("SELECT id FROM topics WHERE name = ?", (topic_name,))
            row = cursor.fetchone()
            if not row:
                # Create topic if it doesn't exist
                self.create_topic(topic_name)
                cursor.execute("SELECT id FROM topics WHERE name = ?", (topic_name,))
                row = cursor.fetchone()
            
            topic_id = row[0]
            
            # Get sensor ID
            cursor.execute("SELECT id FROM sensors WHERE name = ?", (sensor_name,))
            row = cursor.fetchone()
            if not row:
                return  # Sensor doesn't exist
            
            sensor_id = row[0]
            
            # Add relationship
            cursor.execute(
                """
                INSERT OR IGNORE INTO topic_sensors (topic_id, sensor_id)
                VALUES (?, ?)
                """,
                (topic_id, sensor_id)
            )
            
            conn.commit()
    
    def remove_sensor_from_topic(self, topic_name: str, sensor_name: str) -> None:
        """
        Remove a sensor from a topic.
        
        Args:
            topic_name: The topic name
            sensor_name: The sensor name
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            
            # Get IDs
            cursor.execute("SELECT id FROM topics WHERE name = ?", (topic_name,))
            topic_row = cursor.fetchone()
            if not topic_row:
                return  # Topic doesn't exist
            
            cursor.execute("SELECT id FROM sensors WHERE name = ?", (sensor_name,))
            sensor_row = cursor.fetchone()
            if not sensor_row:
                return  # Sensor doesn't exist
            
            # Remove relationship
            cursor.execute(
                """
                DELETE FROM topic_sensors
                WHERE topic_id = ? AND sensor_id = ?
                """,
                (topic_row[0], sensor_row[0])
            )
            
            conn.commit()
    
    def get_topic_sensors(self, topic_name: str) -> List[Dict[str, Any]]:
        """
        Get sensors for a topic.
        
        Args:
            topic_name: The topic name
            
        Returns:
            A list of sensors in the topic
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            
            cursor.execute(
                """
                SELECT s.id, s.name, s.last_value, s.last_updated
                FROM sensors s
                JOIN topic_sensors ts ON s.id = ts.sensor_id
                JOIN topics t ON ts.topic_id = t.id
                WHERE t.name = ?
                """,
                (topic_name,)
            )
            
            return [dict(row) for row in cursor.fetchall()]
    
    def get_published_topics(self) -> List[Dict[str, Any]]:
        """
        Get topics that are published to the broker.
        
        Returns:
            A list of topics
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            cursor.execute("SELECT id, name FROM topics WHERE publish = 1")
            return [dict(row) for row in cursor.fetchall()]
    
    # Subscription methods
    
    def add_subscription(self, topic: str, source_client_id: str) -> None:
        """
        Add a subscription.
        
        Args:
            topic: The topic to subscribe to
            source_client_id: The source client ID
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute(
                """
                INSERT OR REPLACE INTO subscriptions (topic, source_client_id, active)
                VALUES (?, ?, 1)
                """,
                (topic, source_client_id)
            )
            conn.commit()
    
    def remove_subscription(self, topic: str, source_client_id: str) -> None:
        """
        Remove a subscription.
        
        Args:
            topic: The topic to unsubscribe from
            source_client_id: The source client ID
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute(
                "UPDATE subscriptions SET active = 0 WHERE topic = ? AND source_client_id = ?",
                (topic, source_client_id)
            )
            conn.commit()
    
    def add_subscription_data(self, topic: str, source_client_id: str,
                             timestamp: int, data: str) -> None:
        """
        Add subscription data.
        
        Args:
            topic: The topic
            source_client_id: The source client ID
            timestamp: The timestamp
            data: The data
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            
            # Get subscription ID
            cursor.execute(
                "SELECT id FROM subscriptions WHERE topic = ? AND source_client_id = ? AND active = 1",
                (topic, source_client_id)
            )
            row = cursor.fetchone()
            if not row:
                return  # No active subscription
            
            subscription_id = row[0]
            
            # Add data
            cursor.execute(
                """
                INSERT INTO subscription_data (subscription_id, timestamp, data)
                VALUES (?, ?, ?)
                """,
                (subscription_id, timestamp, data)
            )
            
            conn.commit()
    
    def get_subscriptions(self) -> List[Dict[str, Any]]:
        """
        Get active subscriptions.
        
        Returns:
            A list of active subscriptions
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            cursor.execute(
                "SELECT id, topic, source_client_id FROM subscriptions WHERE active = 1"
            )
            return [dict(row) for row in cursor.fetchall()]
    
    def get_subscription_data(self, topic: str, source_client_id: str,
                             limit: int = 100) -> List[Dict[str, Any]]:
        """
        Get subscription data.
        
        Args:
            topic: The topic
            source_client_id: The source client ID
            limit: Maximum number of data points to return
            
        Returns:
            A list of data points
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.cursor()
            
            cursor.execute(
                """
                SELECT sd.timestamp, sd.data
                FROM subscription_data sd
                JOIN subscriptions s ON sd.subscription_id = s.id
                WHERE s.topic = ? AND s.source_client_id = ? AND s.active = 1
                ORDER BY sd.timestamp DESC
                LIMIT ?
                """,
                (topic, source_client_id, limit)
            )
            
            return [dict(row) for row in cursor.fetchall()] 
        
    def get_broker_host(self):
        with sqlite3.connect(self.db_path) as conn:
            row = conn.execute("SELECT value FROM config WHERE key='broker_host'").fetchone()
            return row[0] if row else None
    
    def set_broker_host(self, host):
        with sqlite3.connect(self.db_path) as conn:
            conn.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('broker_host', ?)", (host,))
            conn.commit()
    
    def get_broker_port(self):
        with sqlite3.connect(self.db_path) as conn:
            row = conn.execute("SELECT value FROM config WHERE key='broker_port'").fetchone()
            return int(row[0]) if row else None
    
    def set_broker_port(self, port):
        with sqlite3.connect(self.db_path) as conn:
            conn.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('broker_port', ?)", (str(port),))
            conn.commit()