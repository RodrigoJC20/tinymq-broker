#!/usr/bin/env python3
"""
TinyMQ Client CLI

A modern, command-line interface for the TinyMQ client.
"""
import argparse
import cmd
import os
import signal
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Any, Tuple
import sqlite3
import json

from tinymq import Client, DataAcquisitionService, Database

# Terminal colors
class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN = "\033[96m"
    WHITE = "\033[97m"
    
    @staticmethod
    def success(text):
        return f"{Colors.GREEN}{text}{Colors.RESET}"
    
    @staticmethod
    def error(text):
        return f"{Colors.RED}{text}{Colors.RESET}"
    
    @staticmethod
    def warning(text):
        return f"{Colors.YELLOW}{text}{Colors.RESET}"
    
    @staticmethod
    def info(text):
        return f"{Colors.BLUE}{text}{Colors.RESET}"
    
    @staticmethod
    def highlight(text):
        return f"{Colors.CYAN}{text}{Colors.RESET}"
    
    @staticmethod
    def title(text):
        return f"{Colors.BOLD}{Colors.MAGENTA}{text}{Colors.RESET}"
    
    @staticmethod
    def strip_color(text):
        """Remove ANSI color codes for length calculations."""
        import re
        return re.sub(r'\033\[\d+m', '', text)
    
    @staticmethod
    def print_table(headers, rows, widths=None):
        """Print a table with proper alignment accounting for ANSI color codes.
        
        Args:
            headers: List of column headers
            rows: List of rows (each row is a list of values)
            widths: Optional list of column widths (computed from data if not provided)
        """
        if not widths:
            # Calculate widths based on content
            widths = []
            for i in range(len(headers)):
                col_values = [row[i] if i < len(row) else "" for row in rows]
                max_width = max(len(Colors.strip_color(str(val))) for val in [headers[i]] + col_values)
                widths.append(max_width + 2)  # Add padding
        
        # Print headers
        header_row = ""
        for i, header in enumerate(headers):
            header_row += f"{Colors.BOLD}{header}{' ' * (widths[i] - len(header))}{Colors.RESET}"
        print(header_row)
        
        # Print separator
        print("-" * sum(widths))
        
        # Print rows
        for row in rows:
            row_str = ""
            for i, cell in enumerate(row):
                if i < len(widths):
                    cell_str = str(cell)
                    padding = widths[i] - len(Colors.strip_color(cell_str))
                    row_str += f"{cell_str}{' ' * padding}"
            print(row_str)


class TinyMQCLI(cmd.Cmd):
    """Interactive TinyMQ Client CLI."""
    
    intro = f"""
{Colors.title('Welcome to TinyMQ Client')}
{Colors.info('Type "help" or "?" to list available commands.')}
{Colors.info('Type "exit" or Ctrl+D to exit.')}
    """
    prompt = f"{Colors.BOLD}{Colors.CYAN}tinymq> {Colors.RESET}"
    
    def __init__(self):
        """Initialize the CLI."""
        super().__init__()
        self.db = Database()
        self.das: Optional[DataAcquisitionService] = None
        self.client: Optional[Client] = None
        
        # Signal handling for clean shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def _signal_handler(self, sig, frame) -> None:
        """Handle signals for clean shutdown."""
        print(f"\n{Colors.warning('Shutting down...')}")
        if self.das:
            self.das.stop()
        if self.client and self.client.connected:
            self.client.disconnect()
        sys.exit(0)
    
    def clear_screen(self) -> None:
        """Clear the terminal screen."""
        os.system('cls' if os.name == 'nt' else 'clear')
    
    def start(self) -> None:
        """Start the CLI."""
        self.clear_screen()
        
        # Ensure client ID is set
        client_id = self.db.get_client_id()
        if not client_id:
            print(f"{Colors.title('Welcome to TinyMQ Client')}")
            print(f"\n{Colors.info('First-time setup: Please set your client ID (student ID) to continue.')}")
            client_id = input(f"{Colors.BOLD}Client ID: {Colors.RESET}").strip()
            if not client_id:
                print(f"{Colors.error('Client ID is required. Exiting.')}")
                return
            self.db.set_client_id(client_id)
            print(f"{Colors.success(f'Client ID set to: {client_id}')}")
        
        # Start DAS
        self.das = DataAcquisitionService(self.db, verbose=False)
        if not self.das.start():
            print(f"{Colors.error('Failed to start Data Acquisition Service. Exiting.')}")
            return
        
        # Start command loop
        self.cmdloop()
    
    def emptyline(self) -> bool:
        """Do nothing on empty line."""
        return False
    
    def default(self, line: str) -> bool:
        """Handle unknown command."""
        print(f"{Colors.error(f'Unknown command: {line}')}")
        print(f"{Colors.info('Type "help" to see available commands')}")
        return False
    
    def do_exit(self, arg: str) -> bool:
        """Exit the program."""
        if self.das:
            self.das.stop()
        if self.client and self.client.connected:
            self.client.disconnect()
        print(f"{Colors.warning('Goodbye!')}")
        return True
    
    def do_EOF(self, arg: str) -> bool:
        """Exit on Ctrl+D."""
        print()  # Print newline before exit message
        return self.do_exit(arg)
    
    def do_help(self, arg: str) -> None:
        """Show help for commands."""
        if arg:
            # Use the default help for a specific command
            super().do_help(arg)
            return
        
        print(f"\n{Colors.title('TinyMQ Client Commands')}")
        
        command_groups = {
            "General": [
                ("help", "?", "Show this help message"),
                ("exit", "Ctrl+D", "Exit the program"),
                ("clear", "", "Clear the screen"),
                ("reset_db", "", "Reset the database to initial state"),
                ("stats", "", "Show system statistics"),
                ("verbose", "", "Toggle verbose mode [on|off]"),
            ],
            "List Commands": [
                ("ls sensors", "sensors, s", "List all sensors"),
                ("ls topics", "topics, t", "List all topics"),
                ("ls subs", "subs", "List all subscriptions"),
            ],
            "Sensor Commands": [
                ("sensor <id|name> [limit]", "s", "Show history for a sensor"),
            ],
            "Topic Commands": [
                ("create <name> [publish]", "create_topic", "Create a new topic"),
                ("topic <id|name>", "", "Show sensors in a topic"),
                ("add <topic_id|name> <sensor_id|name>[,...]", "add_sensor", "Add sensors to a topic"),
                ("remove <topic_id|name> <sensor_id|name>[,...]", "rm", "Remove sensors from a topic"),
                ("pub <id|name> [on|off]", "publish_topic", "Toggle publishing for a topic"),
            ],
            "Broker Commands": [
                ("status", "", "Show connection status"),
                ("connect [host] [port]", "", "Connect to a broker"),
                ("disconnect", "", "Disconnect from the broker"),
            ],
            "Subscription Commands": [
                ("sub <topic_id|name> <client_id>", "subscribe", "Subscribe to a topic"),
                ("unsub <topic_id|name> <client_id>", "unsubscribe", "Unsubscribe from a topic"),
                ("subdata <topic_id|name> <client_id> [limit]", "subscription_data", "View subscription data"),
            ],
            "Identity Commands": [
                ("id", "", "Show current client ID and metadata"),
                ("set_id <new_id>", "", "Set client ID"),
                ("set_metadata", "", "Set client metadata (name and email)"),
            ],
        }
        
        for group, commands in command_groups.items():
            print(f"\n{Colors.BOLD}{Colors.BLUE}{group}:{Colors.RESET}")
            for cmd, aliases, desc in commands:
                aliases_str = f" (aliases: {aliases})" if aliases else ""
                print(f"  {Colors.CYAN}{cmd:<40}{Colors.RESET} {desc}{aliases_str}")
        
        print(f"\n{Colors.info('Type "help <command>" for more information on a specific command.')}")
    
    def do_clear(self, arg: str) -> None:
        """Clear the screen."""
        self.clear_screen()
    
    def do_stats(self, arg: str) -> None:
        """Show system statistics."""
        if not self.das:
            print(f"{Colors.error('Data Acquisition Service is not running.')}")
            return
        
        stats = self.das.get_stats()
        
        print(f"\n{Colors.title('System Statistics')}")
        print(f"{Colors.BOLD}Total readings received:{Colors.RESET} {stats['readings_received']}")
        print(f"{Colors.BOLD}DAS running:{Colors.RESET} {Colors.GREEN if stats['running'] else Colors.RED}{stats['running']}{Colors.RESET}")
        
        if self.client and self.client.connected:
            print(f"{Colors.BOLD}Connected to broker:{Colors.RESET} {Colors.GREEN}Yes{Colors.RESET} ({self.client.host}:{self.client.port})")
        else:
            print(f"{Colors.BOLD}Connected to broker:{Colors.RESET} {Colors.RED}No{Colors.RESET}")
    
    def do_reset_db(self, arg: str) -> None:
        """Reset the database to its initial state (erases all data)."""
        print(f"\n{Colors.warning('WARNING: This will erase ALL data in the database!')}")
        print(f"{Colors.warning('This includes all sensors, readings, topics, and subscriptions.')}")
        print(f"{Colors.warning('Client ID and metadata will also be reset.')}")
        print(f"{Colors.warning('This action cannot be undone.')}")
        
        confirm = input(f"{Colors.BOLD}Are you sure you want to erase all data? (type 'RESET' to confirm): {Colors.RESET}").strip()
        if confirm != 'RESET':
            print(f"{Colors.info('Database reset cancelled.')}")
            return
        
        try:
            # Stop DAS temporarily
            if self.das:
                self.das.stop()
            
            # Disconnect from broker
            if self.client and self.client.connected:
                self.client.disconnect()
                self.client = None
            
            # Simply clear all tables using SQL
            with sqlite3.connect(self.db.db_path) as conn:
                cursor = conn.cursor()
                
                # Get list of all tables
                cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
                tables = cursor.fetchall()
                
                # Drop all tables except sqlite_sequence (which manages autoincrement)
                for table in tables:
                    table_name = table[0]
                    if table_name != 'sqlite_sequence':
                        cursor.execute(f"DROP TABLE IF EXISTS {table_name}")
                
                # Also clear the sqlite_sequence to reset autoincrement counters
                cursor.execute("DELETE FROM sqlite_sequence")
                
                # Commit the changes
                conn.commit()
            
            # Recreate the tables
            self.db._ensure_tables()
            
            print(f"{Colors.success('Database reset complete. All data has been erased.')}")
            
            # Restart DAS
            if self.das:
                self.das = DataAcquisitionService(self.db, verbose=False)
                if not self.das.start():
                    print(f"{Colors.error('Failed to restart Data Acquisition Service.')}")
            
            return
        except Exception as e:
            print(f"{Colors.error(f'Error resetting database: {e}')}")
            
            # Try to restart DAS if it was stopped
            if self.das is None or not self.das.running:
                self.das = DataAcquisitionService(self.db, verbose=False)
                self.das.start()
    
    def do_verbose(self, arg: str) -> None:
        """Toggle verbose mode (usage: verbose [on|off])."""
        if not self.das:
            print(f"{Colors.error('Data Acquisition Service is not running.')}")
            return
        
        arg = arg.strip().lower()
        
        if not arg:
            # Toggle mode
            current = self.das.verbose
            self.das.set_verbose(not current)
            state = "enabled" if not current else "disabled"
        elif arg in ('on', 'true', 'yes', 'y', '1'):
            self.das.set_verbose(True)
            state = "enabled"
        elif arg in ('off', 'false', 'no', 'n', '0'):
            self.das.set_verbose(False)
            state = "disabled"
        else:
            print(f"{Colors.error('Usage: verbose [on|off]')}")
            return
        
        print(f"{Colors.success(f'Verbose mode {state}.')}")
        if state == "enabled":
            print(f"{Colors.info('All sensor readings will be printed to console.')}")
        else:
            print(f"{Colors.info('Only connection events will be printed to console.')}")
    
    # Client identity commands
    
    def do_id(self, arg: str) -> None:
        """Show current client ID and metadata."""
        client_id = self.db.get_client_id()
        metadata = self.db.get_client_metadata()
        
        print(f"\n{Colors.title('Client Identity')}")
        print(f"{Colors.BOLD}Client ID:{Colors.RESET} {client_id}")
        
        if metadata:
            print(f"\n{Colors.BOLD}Metadata:{Colors.RESET}")
            for key, value in metadata.items():
                print(f"  {Colors.highlight(key)}: {value}")
    
    def do_set_id(self, arg: str) -> None:
        """Set client ID (usage: set_id <new_id>)."""
        new_id = arg.strip()
        if not new_id:
            print(f"{Colors.error('Usage: set_id <new_id>')}")
            return
        
        current_id = self.db.get_client_id()
        print(f"\nCurrent client ID: {current_id}")
        print(f"{Colors.warning('Warning: Changing your client ID will disconnect you from the broker.')}")
        print(f"{Colors.warning('You will need to reconnect and resubscribe to topics.')}")
        
        confirm = input(f"{Colors.BOLD}Are you sure? (y/N): {Colors.RESET}").strip().lower()
        if confirm != 'y':
            return
        
        try:
            # Disconnect from broker if connected
            if self.client and self.client.connected:
                self.client.disconnect()
                self.client = None
            
            self.db.set_client_id(new_id)
            print(f"{Colors.success(f'Client ID changed to: {new_id}')}")
        except Exception as e:
            print(f"{Colors.error(f'Error changing client ID: {e}')}")
    
    def do_set_metadata(self, arg: str) -> None:
        """Set client metadata (name and email)."""
        current_metadata = self.db.get_client_metadata()
        
        print(f"\n{Colors.title('Update Metadata')}")
        
        if current_metadata:
            print(f"\n{Colors.BOLD}Current metadata:{Colors.RESET}")
            for key, value in current_metadata.items():
                print(f"  {Colors.highlight(key)}: {value}")
        
        print(f"\n{Colors.info('Enter metadata values (leave empty to keep current value)')}")
        
        name = input(f"{Colors.BOLD}Name: {Colors.RESET}").strip()
        email = input(f"{Colors.BOLD}Email: {Colors.RESET}").strip()
        
        # Update only non-empty values
        metadata = current_metadata.copy()
        if name:
            metadata["name"] = name
        if email:
            metadata["email"] = email
        
        try:
            self.db.set_client_metadata(metadata)
            print(f"{Colors.success('Metadata updated successfully.')}")
        except Exception as e:
            print(f"{Colors.error(f'Error updating metadata: {e}')}")
    
    # Sensor commands
    
    def do_sensors(self, arg: str) -> None:
        """List all sensors. Alias: ls sensors"""
        sensors = self.db.get_sensors()
        
        if not sensors:
            print(f"{Colors.warning('No sensors found.')}")
            print(f"{Colors.info('Connect an ESP32 device to receive sensor data.')}")
            return
        
        print(f"\n{Colors.title('Sensors')}")
        
        headers = ["ID", "Name", "Status", "Last Value", "Last Updated"]
        rows = []
        
        current_time = time.time()
        for sensor in sensors:
            # Calculate time since last update
            time_since_update = current_time - sensor["last_updated"]
            
            # Determine status based on time since last update
            if time_since_update < 5:  # Active: less than 5 seconds
                status = f"{Colors.GREEN}●{Colors.RESET} Active"
            elif time_since_update < 30:  # Waiting: between 5-30 seconds
                status = f"{Colors.YELLOW}●{Colors.RESET} Waiting"
            else:  # Inactive: more than 30 seconds
                status = f"{Colors.RED}●{Colors.RESET} Inactive"
            
            last_updated = datetime.fromtimestamp(sensor["last_updated"]).strftime("%Y-%m-%d %H:%M:%S")
            
            rows.append([
                f"{Colors.CYAN}{sensor['id']}{Colors.RESET}",
                f"{Colors.highlight(sensor['name'])}",
                status,
                sensor['last_value'],
                last_updated
            ])
        
        Colors.print_table(headers, rows, [5, 20, 20, 15, 20])
    
    def do_ls(self, arg: str) -> None:
        """Unified list command. Usage: list [sensors|topics|subs]"""
        args = arg.strip().split()
        if not args:
            print(f"{Colors.error('Usage: list [sensors|topics|subs|s|t|sub]')}")
            return
        
        item_type = args[0].lower()
        
        if item_type in ('sensors', 's'):
            self.do_sensors("")
        elif item_type in ('topics', 't'):
            self.do_topics("")
        elif item_type in ('subs', 'subscriptions', 'sub'):
            self.do_subscriptions("")
        else:
            print(f"{Colors.error(f'Unknown list type: {item_type}')}")
            print(f"{Colors.info('Available types: sensors, topics, subs')}")
    
    def do_sensor(self, arg: str) -> None:
        """Show history for a sensor (usage: sensor <id|name> [limit]). Alias: s"""
        args = arg.strip().split()
        if not args:
            print(f"{Colors.error('Usage: sensor <id|name> [limit]')}")
            return
        
        sensor_id_or_name = args[0]
        try:
            limit = int(args[1]) if len(args) > 1 else 10
        except ValueError:
            print(f"{Colors.error('Limit must be a number')}")
            return
        
        sensor = self.db.get_sensor(sensor_id_or_name)
        if not sensor:
            print(f"{Colors.error(f'Sensor not found: {sensor_id_or_name}')}")
            return
        
        readings = self.db.get_readings(sensor["name"], limit=limit)
        
        if not readings:
            print(f"{Colors.warning(f'No readings found for sensor: {sensor["name"]}')}")
            return
        
        print(f"\n{Colors.title(f'History for sensor: {sensor["name"]} (ID: {sensor["id"]})')}")
        print(f"{Colors.BOLD}{'Timestamp':<20} {'Value':<15} {'Units':<10}{Colors.RESET}")
        print("-" * 45)
        
        for reading in readings:
            timestamp = datetime.fromtimestamp(reading["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
            print(f"{timestamp:<20} {Colors.highlight(reading['value']):<15} {reading['units']:<10}")
    
    def do_s(self, arg: str) -> None:
        """Alias for sensor command."""
        return self.do_sensor(arg)
    
    # Topic commands
    
    def do_topics(self, arg: str) -> None:
        """List all topics. Alias: t, list topics"""
        topics = self.db.get_topics()
        
        if not topics:
            print(f"{Colors.warning('No topics found.')}")
            print(f"{Colors.info('Use "create" or "create_topic" to create a topic.')}")
            return
        
        print(f"\n{Colors.title('Topics')}")
        
        headers = ["ID", "Name", "Published", "Sensors"]
        rows = []
        
        for topic in topics:
            sensors = self.db.get_topic_sensors(topic["name"])
            published_status = "Yes" if topic["publish"] else "No"
            published = f"{Colors.GREEN if topic['publish'] else Colors.RED}{published_status}{Colors.RESET}"
            
            rows.append([
                f"{Colors.CYAN}{topic['id']}{Colors.RESET}",
                f"{Colors.highlight(topic['name'])}",
                published,
                str(len(sensors))
            ])
        
        Colors.print_table(headers, rows, [5, 20, 15, 10])
    
    def do_t(self, arg: str) -> None:
        """Alias for topics command."""
        return self.do_topics(arg)
    
    def do_create_topic(self, arg: str) -> None:
        """Create a new topic (usage: create_topic <name> [publish]). Alias: create"""
        args = arg.strip().split()
        if not args:
            print(f"{Colors.error('Usage: create_topic <name> [publish]')}")
            return
        
        name = args[0]
        publish = len(args) > 1 and args[1].lower() in ('true', 'yes', 'y', '1')
        
        try:
            self.db.create_topic(name, publish)
            print(f"{Colors.success(f'Topic "{name}" created successfully.')}")
        except Exception as e:
            print(f"{Colors.error(f'Error creating topic: {e}')}")
    
    def do_create(self, arg: str) -> None:
        """Alias for create_topic command."""
        return self.do_create_topic(arg)
    
    def do_topic(self, arg: str) -> None:
        """Show sensors in a topic (usage: topic <id|name>)."""
        topic_id_or_name = arg.strip()
        if not topic_id_or_name:
            print(f"{Colors.error('Usage: topic <id|name>')}")
            return
        
        topic = self.db.get_topic(topic_id_or_name)
        if not topic:
            print(f"{Colors.error(f'Topic not found: {topic_id_or_name}')}")
            return
        
        sensors = self.db.get_topic_sensors(topic["name"])
        
        print(f"\n{Colors.title(f'Topic: {topic["name"]} (ID: {topic["id"]})')}")
        publish_status = "Yes" if topic["publish"] else "No"
        print(f"{Colors.BOLD}Publishing:{Colors.RESET} {Colors.GREEN if topic['publish'] else Colors.RED}{publish_status}{Colors.RESET}")
        
        if not sensors:
            print(f"\n{Colors.warning('No sensors in this topic.')}")
            print(f"{Colors.info('Use "add <topic_id> <sensor_id1,sensor_id2,...>" to add sensors.')}")
            return
        
        headers = ["ID", "Name", "Status", "Last Value", "Last Updated"]
        rows = []
        
        current_time = time.time()
        for sensor in sensors:
            # Calculate time since last update
            time_since_update = current_time - sensor["last_updated"]
            
            # Determine status based on time since last update
            if time_since_update < 5:  # Active: less than 5 seconds
                status = f"{Colors.GREEN}●{Colors.RESET} Active"
            elif time_since_update < 30:  # Waiting: between 5-30 seconds
                status = f"{Colors.YELLOW}●{Colors.RESET} Waiting"
            else:  # Inactive: more than 30 seconds
                status = f"{Colors.RED}●{Colors.RESET} Inactive"
                
            last_updated = datetime.fromtimestamp(sensor["last_updated"]).strftime("%Y-%m-%d %H:%M:%S")
            
            rows.append([
                f"{Colors.CYAN}{sensor['id']}{Colors.RESET}",
                f"{Colors.highlight(sensor['name'])}",
                status,
                sensor['last_value'],
                last_updated
            ])
        
        Colors.print_table(headers, rows, [5, 20, 20, 15, 20])
    
    def do_add_sensor(self, arg: str) -> None:
        """Add sensors to a topic (usage: add_sensor <topic_id|name> <sensor_id|name>[,sensor_id|name]...). Alias: add"""
        args = arg.strip().split(None, 1)
        if len(args) != 2:
            print(f"{Colors.error('Usage: add_sensor <topic_id|name> <sensor_id|name>[,sensor_id|name]...')}")
            return
        
        topic_id_or_name = args[0]
        sensor_ids_or_names = args[1].split(',')
        
        # Verify topic exists
        topic = self.db.get_topic(topic_id_or_name)
        if not topic:
            print(f"{Colors.error(f'Topic not found: {topic_id_or_name}')}")
            return
        
        success_count = 0
        for sensor_id_or_name in sensor_ids_or_names:
            sensor_id_or_name = sensor_id_or_name.strip()
            if not sensor_id_or_name:
                continue
                
            # Verify sensor exists
            sensor = self.db.get_sensor(sensor_id_or_name)
            if not sensor:
                print(f"{Colors.error(f'Sensor not found: {sensor_id_or_name}')}")
                continue
            
            try:
                self.db.add_sensor_to_topic(topic["name"], sensor["name"])
                print(f"{Colors.success(f'Added sensor "{sensor["name"]}" to topic "{topic["name"]}".')}")
                success_count += 1
            except Exception as e:
                print(f"{Colors.error(f'Error adding sensor {sensor_id_or_name} to topic: {e}')}")
        
        if success_count > 0:
            print(f"{Colors.success(f'Added {success_count} sensor(s) to topic "{topic["name"]}".')}")
    
    def do_add(self, arg: str) -> None:
        """Alias for add_sensor command."""
        return self.do_add_sensor(arg)
    
    def do_remove_sensor(self, arg: str) -> None:
        """Remove sensors from a topic (usage: remove_sensor <topic_id|name> <sensor_id|name>[,sensor_id|name]...). Alias: remove, rm"""
        args = arg.strip().split(None, 1)
        if len(args) != 2:
            print(f"{Colors.error('Usage: remove_sensor <topic_id|name> <sensor_id|name>[,sensor_id|name]...')}")
            return
        
        topic_id_or_name = args[0]
        sensor_ids_or_names = args[1].split(',')
        
        # Verify topic exists
        topic = self.db.get_topic(topic_id_or_name)
        if not topic:
            print(f"{Colors.error(f'Topic not found: {topic_id_or_name}')}")
            return
        
        success_count = 0
        for sensor_id_or_name in sensor_ids_or_names:
            sensor_id_or_name = sensor_id_or_name.strip()
            if not sensor_id_or_name:
                continue
                
            # Verify sensor exists
            sensor = self.db.get_sensor(sensor_id_or_name)
            if not sensor:
                print(f"{Colors.error(f'Sensor not found: {sensor_id_or_name}')}")
                continue
            
            try:
                self.db.remove_sensor_from_topic(topic["name"], sensor["name"])
                print(f"{Colors.success(f'Removed sensor "{sensor["name"]}" from topic "{topic["name"]}".')}")
                success_count += 1
            except Exception as e:
                print(f"{Colors.error(f'Error removing sensor {sensor_id_or_name} from topic: {e}')}")
        
        if success_count > 0:
            print(f"{Colors.success(f'Removed {success_count} sensor(s) from topic "{topic["name"]}".')}")
    
    def do_remove(self, arg: str) -> None:
        """Alias for remove_sensor command."""
        return self.do_remove_sensor(arg)
    
    def do_rm(self, arg: str) -> None:
        """Alias for remove_sensor command."""
        return self.do_remove_sensor(arg)
    
    def do_publish_topic(self, arg: str) -> None:
        """Set a topic to be published (usage: publish_topic <id|name> [on|off]). Alias: pub"""
        args = arg.strip().split()
        if not args:
            print(f"{Colors.error('Usage: publish_topic <id|name> [on|off]')}")
            return
        
        topic_id_or_name = args[0]
        publish = True
        if len(args) > 1:
            publish_str = args[1].lower()
            if publish_str in ('off', 'false', 'no', 'n', '0'):
                publish = False
            elif publish_str in ('on', 'true', 'yes', 'y', '1'):
                publish = True
            else:
                print(f"{Colors.error('Invalid value. Use on/off, true/false, yes/no, or 1/0.')}")
                return
        
        # Check if topic exists
        topic = self.db.get_topic(topic_id_or_name)
        if not topic:
            print(f"{Colors.error(f'Topic not found: {topic_id_or_name}')}")
            return
        
        if topic["publish"] == publish:
            state = "published" if publish else "unpublished"
            print(f"{Colors.info(f'Topic "{topic["name"]}" is already {state}.')}")
            return
        
        self.db.set_topic_publish(topic["name"], publish)
        
        state = "published" if publish else "unpublished"
        print(f"{Colors.success(f'Topic "{topic["name"]}" is now {state}.')}")
        
        if publish:
            if self.client and self.client.connected:
                print(f"{Colors.info(f'Setting up publishing for topic: {topic["name"]}')}")
                self._setup_topic_publishing(topic["name"])
            else:
                print(f"\n{Colors.info('You are not connected to a broker.')}")
                connect_str = input(f"{Colors.BOLD}Connect to broker now? (y/N): {Colors.RESET}").strip().lower()
                if connect_str == 'y':
                    self.do_connect("")
        else:
            print(f"{Colors.info(f'Publishing for topic \\"{topic["name"]}\\" turned off. Active callbacks will check DB status.')}")
    
    def do_pub(self, arg: str) -> None:
        """Alias for publish_topic command."""
        return self.do_publish_topic(arg)
    
    # Broker commands
    
    def do_status(self, arg: str) -> None:
        """Show connection status."""
        print(f"\n{Colors.title('Connection Status')}")
        
        if self.client and self.client.connected:
            print(f"{Colors.BOLD}Status:{Colors.RESET} {Colors.GREEN}Connected{Colors.RESET}")
            print(f"{Colors.BOLD}Broker:{Colors.RESET} {self.client.host}:{self.client.port}")
            print(f"{Colors.BOLD}Client ID:{Colors.RESET} {self.client.client_id}")
            
            published_topics = self.db.get_published_topics()
            if published_topics:
                print(f"\n{Colors.BOLD}Published Topics:{Colors.RESET}")
                for topic in published_topics:
                    print(f"  - {Colors.highlight(topic['name'])}")
            else:
                print(f"\n{Colors.info('No topics are currently being published.')}")
        else:
            print(f"{Colors.BOLD}Status:{Colors.RESET} {Colors.RED}Disconnected{Colors.RESET}")
            print(f"{Colors.info('Use "connect" to connect to a broker')}")
    
    def do_connect(self, arg: str) -> None:
        """Connect to a broker (usage: connect [host] [port])."""
        args = arg.strip().split()
        
        host = args[0] if args else "localhost"
        
        try:
            port = int(args[1]) if len(args) > 1 else 1505
        except ValueError:
            print(f"{Colors.error('Port must be a number.')}")
            return
        
        client_id = self.db.get_client_id()
        if not client_id:
            print(f"{Colors.error('Client ID not set. Use set_id to set your identity first.')}")
            return
        
        try:
            if self.client and self.client.connected:
                print(f"{Colors.warning('Already connected to a broker. Disconnecting...')}")
                self.client.disconnect()
            
            print(f"{Colors.info(f'Connecting to {host}:{port}...')}")
            self.client = Client(client_id, host, port)
            
            if self.client.connect():
                print(f"{Colors.success(f'Connected to broker at {host}:{port}')}")
                
                # Start publishing topics marked for publishing
                published_topics = self.db.get_published_topics()
                for topic_info in published_topics:
                    print(f"{Colors.info(f'Publishing topic: {topic_info["name"]}')}")
                    # Add callback for sensor data to publish to this topic
                    self._setup_topic_publishing(topic_info["name"])
                
            else:
                print(f"{Colors.error('Failed to connect to broker.')}")
                self.client = None
        except Exception as e:
            print(f"{Colors.error(f'Error connecting to broker: {e}')}")
            self.client = None
    
    def do_disconnect(self, arg: str) -> None:
        """Disconnect from the broker."""
        if not self.client or not self.client.connected:
            print(f"{Colors.warning('Not connected to a broker.')}")
            return
        
        try:
            self.client.disconnect()
            self.client = None
            print(f"{Colors.success('Disconnected from broker.')}")
        except Exception as e:
            print(f"{Colors.error(f'Error disconnecting from broker: {e}')}")
    
    # Subscription commands
    
    def do_subscriptions(self, arg: str) -> None:
        """List active subscriptions. Alias: subs, list subs"""
        subscriptions = self.db.get_subscriptions()
        
        if not subscriptions:
            print(f"{Colors.warning('No active subscriptions.')}")
            print(f"{Colors.info('Use "sub <topic> <client_id>" to subscribe to a topic.')}")
            return
        
        print(f"\n{Colors.title('Active Subscriptions')}")
        
        headers = ["ID", "Topic", "Source Client"]
        rows = []
        
        for sub in subscriptions:
            rows.append([
                f"{Colors.CYAN}{sub['id']}{Colors.RESET}",
                f"{Colors.highlight(sub['topic'])}",
                sub['source_client_id']
            ])
        
        Colors.print_table(headers, rows, [5, 20, 20])
    
    def do_subs(self, arg: str) -> None:
        """Alias for subscriptions command."""
        return self.do_subscriptions(arg)
    
    def do_subscribe(self, arg: str) -> None:
        """Subscribe to a topic (usage: subscribe <topic_id|name> <client_id>). Alias: sub"""
        args = arg.strip().split()
        if len(args) != 2:
            print(f"{Colors.error('Usage: subscribe <topic_id|name> <client_id>')}")
            return
        
        topic_id_or_name = args[0]
        source_client = args[1]
        
        # Check if we're connected to a broker
        if not self.client or not self.client.connected:
            print(f"{Colors.warning('Not connected to a broker.')}")
            connect_str = input(f"{Colors.BOLD}Connect to broker now? (y/N): {Colors.RESET}").strip().lower()
            if connect_str == 'y':
                self.do_connect("")
            else:
                return
            
            if not self.client or not self.client.connected:
                return  # Failed to connect
        
        # Check if topic exists if it's a local topic ID
        try:
            topic_id = int(topic_id_or_name)
            topic = self.db.get_topic(topic_id)
            if topic:
                topic_id_or_name = topic["name"]
        except ValueError:
            pass  # Not a numeric ID, use as is
        
        # Create local subscription record
        try:
            self.db.add_subscription(topic_id_or_name, source_client)
            
            # Subscribe with the broker
            def subscription_callback(topic: str, message: bytes) -> None:
                """Handle subscription messages."""
                try:
                    message_str = message.decode('utf-8') if isinstance(message, bytes) else str(message)
                    timestamp = int(time.time())
                    self.db.add_subscription_data(topic_id_or_name, source_client, timestamp, message_str)
                    print(f"{Colors.info(f'Received data for {topic_id_or_name} from {source_client}: {message_str}')}")
                except Exception as e:
                    print(f"{Colors.error(f'Error handling subscription data: {e}')}")
            
            broker_topic = f"{source_client}/{topic_id_or_name}"
            success = self.client.subscribe(broker_topic, subscription_callback)
            
            if success:
                print(f"{Colors.success(f'Subscribed to topic "{topic_id_or_name}" from client "{source_client}".')}")
            else:
                print(f"{Colors.error('Failed to subscribe with broker.')}")
                # Rollback the subscription record
                self.db.remove_subscription(topic_id_or_name, source_client)
        except Exception as e:
            print(f"{Colors.error(f'Error subscribing to topic: {e}')}")
    
    def do_sub(self, arg: str) -> None:
        """Alias for subscribe command."""
        return self.do_subscribe(arg)
    
    def do_unsubscribe(self, arg: str) -> None:
        """Unsubscribe from a topic (usage: unsubscribe <topic_id|name> <client_id>). Alias: unsub"""
        args = arg.strip().split()
        if len(args) != 2:
            print(f"{Colors.error('Usage: unsubscribe <topic_id|name> <client_id>')}")
            return
        
        topic_id_or_name = args[0]
        source_client = args[1]
        
        # Check if topic exists if it's a local topic ID
        try:
            topic_id = int(topic_id_or_name)
            topic = self.db.get_topic(topic_id)
            if topic:
                topic_id_or_name = topic["name"]
        except ValueError:
            pass  # Not a numeric ID, use as is
        
        # Unsubscribe with broker if connected
        if self.client and self.client.connected:
            self.client.unsubscribe(f"{source_client}/{topic_id_or_name}")
        
        # Update local record
        self.db.remove_subscription(topic_id_or_name, source_client)
        print(f"{Colors.success(f'Unsubscribed from topic "{topic_id_or_name}" by client "{source_client}".')}")
    
    def do_unsub(self, arg: str) -> None:
        """Alias for unsubscribe command."""
        return self.do_unsubscribe(arg)
    
    def do_test_pub(self, arg: str) -> None:
        """Test publish a message to a topic (usage: test_pub <topic> <message>)."""
        args = arg.strip().split(None, 1)
        if len(args) != 2:
            print(f"{Colors.error('Usage: test_pub <topic> <message>')}")
            return
        
        topic = args[0]
        message = args[1]
        
        # Check if we're connected to a broker
        if not self.client or not self.client.connected:
            print(f"{Colors.warning('Not connected to a broker.')}")
            connect_str = input(f"{Colors.BOLD}Connect to broker now? (y/N): {Colors.RESET}").strip().lower()
            if connect_str == 'y':
                self.do_connect("")
            else:
                return
            
            if not self.client or not self.client.connected:
                return  # Failed to connect
        
        try:
            # Publish the test message
            result = self.client.publish(topic, message)
            
            if result:
                print(f"{Colors.success(f'Published test message to topic \"{topic}\"')}")
                print(f"{Colors.info(f'Message: {message}')}")
            else:
                print(f"{Colors.error(f'Failed to publish test message to topic \"{topic}\"')}")
        except Exception as e:
            print(f"{Colors.error(f'Error publishing test message: {e}')}")
    
    def do_subscription_data(self, arg: str) -> None:
        """View subscription data (usage: subscription_data <topic_id|name> <client_id> [limit]). Alias: subdata"""
        args = arg.strip().split()
        if len(args) < 2:
            print(f"{Colors.error('Usage: subscription_data <topic_id|name> <client_id> [limit]')}")
            return
        
        topic_id_or_name = args[0]
        source_client = args[1]
        
        # Check if topic exists if it's a local topic ID
        try:
            topic_id = int(topic_id_or_name)
            topic = self.db.get_topic(topic_id)
            if topic:
                topic_id_or_name = topic["name"]
        except ValueError:
            pass  # Not a numeric ID, use as is
        
        try:
            limit = int(args[2]) if len(args) > 2 else 10
        except ValueError:
            print(f"{Colors.error('Limit must be a number.')}")
            return
        
        data = self.db.get_subscription_data(topic_id_or_name, source_client, limit=limit)
        
        if not data:
            print(f"{Colors.warning('No data found for this subscription.')}")
            return
        
        print(f"\n{Colors.title(f'Data for topic "{topic_id_or_name}" from client "{source_client}"')}")
        print(f"{Colors.BOLD}{'Timestamp':<20} {'Data'}{Colors.RESET}")
        print("-" * 60)
        
        for item in data:
            timestamp = datetime.fromtimestamp(item["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
            print(f"{timestamp:<20} {Colors.highlight(item['data'])}")
    
    def do_subdata(self, arg: str) -> None:
        """Alias for subscription_data command."""
        return self.do_subscription_data(arg)
    
    def _setup_topic_publishing(self, topic_name: str) -> None:
        """
        Setup publishing for a topic.
        
        Args:
            topic_name: Name of topic to publish
        """
        if not self.das or not self.client or not self.client.connected:
            print(f"{Colors.error('Cannot setup publishing: DAS or client not available')}")
            return
        
        sensors = self.db.get_topic_sensors(topic_name)
        if not sensors:
            print(f"{Colors.warning(f'No sensors in topic {topic_name} to publish')}")
            return
            
        sensor_names = [s["name"] for s in sensors]
        print(f"{Colors.info(f'Setting up publishing for topic {topic_name} with sensors: {", ".join(sensor_names)}')}")
        
        def publish_callback(sensor_name: str, data: Dict[str, Any]) -> None:
            current_topic_info = self.db.get_topic(topic_name)
            if not current_topic_info or not current_topic_info["publish"]:
                print(f"{Colors.warning(f'Topic {topic_name} no longer marked for publishing, skipping')}")
                return
            
            if sensor_name in sensor_names and self.client and self.client.connected:
                message = {
                    "sensor": sensor_name,
                    "value": data["value"],
                    "timestamp": data["timestamp"],
                    "units": data["units"]
                }
                try:
                    json_message = json.dumps(message)
                    
                    result = self.client.publish(topic_name, json_message)
                    if not result:
                        print(f"{Colors.error(f'Failed to publish message to topic {topic_name}')}")
                except Exception as e:
                    print(f"{Colors.error(f'Error publishing to topic: {e}')}")
        
        print(f"{Colors.success(f'Registered publish callback for topic {topic_name}')}")
        self.das.add_data_callback(publish_callback)


def main():
    """Main entry point for the CLI."""
    try:
        cli = TinyMQCLI()
        cli.start()
    except Exception as e:
        print(f"{Colors.error(f'Error: {e}')}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main()) 