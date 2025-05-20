#!/usr/bin/env python3
"""
ESP32 Simulator

This script simulates an ESP32 device sending sensor data to the TinyMQ client.
"""
import json
import math
import random
import socket
import time
from typing import Dict, List


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


class ESP32Simulator:
    """ESP32 Simulator."""
    
    def __init__(self, host: str = "localhost", port: int = 12345):
        """
        Initialize the ESP32 simulator.
        
        Args:
            host: Host to connect to
            port: Port to connect to
        """
        self.host = host
        self.port = port
        self.socket = None
        self.connected = False
    
    def connect(self) -> bool:
        """
        Connect to the TinyMQ client.
        
        Returns:
            True if connected successfully, False otherwise
        """
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            self.connected = True
            return True
        except Exception as e:
            print(f"{Colors.RED}Connection error: {e}{Colors.RESET}")
            return False
    
    def disconnect(self) -> None:
        """Disconnect from the TinyMQ client."""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
        self.connected = False
    
    def send_reading(self, data: Dict) -> bool:
        """
        Send a sensor reading.
        
        Args:
            data: Sensor data
            
        Returns:
            True if sent successfully, False otherwise
        """
        if not self.connected or not self.socket:
            return False
        
        try:
            json_data = json.dumps(data) + "\n"
            self.socket.sendall(json_data.encode('utf-8'))
            return True
        except Exception as e:
            print(f"{Colors.RED}Send error: {e}{Colors.RESET}")
            self.disconnect()
            return False
    
    def send_readings(self, data_list: List[Dict]) -> bool:
        """
        Send multiple sensor readings.
        
        Args:
            data_list: List of sensor data
            
        Returns:
            True if sent successfully, False otherwise
        """
        if not self.connected or not self.socket:
            return False
        
        try:
            json_data = json.dumps(data_list) + "\n"
            self.socket.sendall(json_data.encode('utf-8'))
            return True
        except Exception as e:
            print(f"{Colors.RED}Send error: {e}{Colors.RESET}")
            self.disconnect()
            return False
    
    def generate_temperature(self) -> Dict:
        """
        Generate a simulated temperature reading.
        
        Returns:
            Simulated temperature reading
        """
        # Simulate a temperature between 18 and 25 degrees C
        base_temp = 21.5
        variation = 3.5
        temp = base_temp + random.uniform(-variation, variation)
        return {
            "name": "temperature",
            "value": round(temp, 1),
            "timestamp": int(time.time()),
            "units": "C"
        }
    
    def generate_humidity(self) -> Dict:
        """
        Generate a simulated humidity reading.
        
        Returns:
            Simulated humidity reading
        """
        # Simulate a humidity between 30% and 60%
        base_humidity = 45
        variation = 15
        humidity = base_humidity + random.uniform(-variation, variation)
        return {
            "name": "humidity",
            "value": round(humidity, 1),
            "timestamp": int(time.time()),
            "units": "%"
        }
    
    def generate_pressure(self) -> Dict:
        """
        Generate a simulated pressure reading.
        
        Returns:
            Simulated pressure reading
        """
        # Simulate atmospheric pressure around 1013 hPa
        base_pressure = 1013
        variation = 5
        pressure = base_pressure + random.uniform(-variation, variation)
        return {
            "name": "pressure",
            "value": round(pressure, 1),
            "timestamp": int(time.time()),
            "units": "hPa"
        }
    
    def generate_light(self) -> Dict:
        """
        Generate a simulated light reading.
        
        Returns:
            Simulated light reading
        """
        # Simulate light level between 0 and 1000 lux
        hour = time.localtime().tm_hour
        # Model day/night cycle - peak at noon
        base_light = 500 * math.sin(math.pi * (hour / 24))
        base_light = max(0, base_light)  # No negative light
        variation = base_light * 0.2  # 20% variation
        light = base_light + random.uniform(-variation, variation)
        return {
            "name": "light",
            "value": round(light, 1),
            "timestamp": int(time.time()),
            "units": "lux"
        }
    
    def run(self, interval: float = 1.0, count: int = 0) -> None:
        """
        Run the simulator.
        
        Args:
            interval: Interval between readings in seconds
            count: Number of readings to send (0 for infinite)
        """
        print(f"\n{Colors.BOLD}{Colors.CYAN}ESP32 Simulator{Colors.RESET}")
        print(f"{Colors.YELLOW}Connecting to TinyMQ client at {self.host}:{self.port}...{Colors.RESET}")
        
        if not self.connect():
            print(f"{Colors.RED}Failed to connect. Exiting.{Colors.RESET}")
            return
        
        print(f"{Colors.GREEN}Connected to {self.host}:{self.port}{Colors.RESET}")
        
        if count > 0:
            print(f"{Colors.BLUE}Will send {count} readings at {interval}s intervals{Colors.RESET}")
        else:
            print(f"{Colors.BLUE}Will send readings every {interval}s until stopped{Colors.RESET}")
        print(f"{Colors.YELLOW}Press Ctrl+C to stop{Colors.RESET}")
        
        iteration = 0
        try:
            while count == 0 or iteration < count:
                readings = [
                    self.generate_temperature(),
                    self.generate_humidity(),
                    self.generate_pressure(),
                    self.generate_light()
                ]
                
                # Print sensor values
                print(f"\n{Colors.CYAN}Sending readings #{iteration + 1}:{Colors.RESET}")
                for reading in readings:
                    value_str = f"{reading['value']}{reading['units']}"
                    print(f"  {Colors.MAGENTA}{reading['name']:12}{Colors.RESET}: {Colors.GREEN}{value_str:8}{Colors.RESET}")
                
                if self.send_readings(readings):
                    print(f"{Colors.GREEN}✓ Sent {len(readings)} readings{Colors.RESET}")
                else:
                    print(f"{Colors.RED}✗ Failed to send readings, attempting to reconnect...{Colors.RESET}")
                    self.connect()
                
                time.sleep(interval)
                iteration += 1
                
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Stopping simulation...{Colors.RESET}")
        finally:
            self.disconnect()
            print(f"{Colors.GREEN}Disconnected{Colors.RESET}")


def main():
    """Main entry point for the simulator."""
    import argparse
    
    parser = argparse.ArgumentParser(description="ESP32 Simulator")
    parser.add_argument("--host", default="localhost", help="Host to connect to")
    parser.add_argument("--port", type=int, default=12345, help="Port to connect to")
    parser.add_argument("--interval", type=float, default=1.0, help="Interval between readings in seconds")
    parser.add_argument("--count", type=int, default=0, help="Number of readings to send (0 for infinite)")
    
    args = parser.parse_args()
    
    simulator = ESP32Simulator(args.host, args.port)
    simulator.run(args.interval, args.count)


if __name__ == "__main__":
    main() 