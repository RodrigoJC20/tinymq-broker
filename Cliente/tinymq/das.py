import json
import threading
import time
from typing import Optional, List, Dict, Any, Callable

import serial  # Librería para comunicación serial

from .db import Database


class DataAcquisitionService:
    """
    Data Acquisition Service for TinyMQ client using Serial communication.
    """

    def __init__(self, db: Database, serial_port: str = "COM3", baud_rate: int = 115200, verbose: bool = False):
        """
        Args:
            db: Instancia de la base de datos
            serial_port: Puerto serial (ej. "COM3" en Windows o "/dev/ttyUSB0" en Linux)
            baud_rate: Velocidad de transmisión en baudios
            verbose: Si se debe imprimir cada lectura recibida
        """
        self.db = db
        self.serial_port = serial_port
        self.baud_rate = baud_rate
        self.verbose = verbose

        self.serial_conn: Optional[serial.Serial] = None
        self.running = False
        self.thread: Optional[threading.Thread] = None

        self.on_data_received_callbacks: List[Callable[[str, Any], None]] = []
        self.total_readings_received = 0

    def start(self) -> bool:
        """Inicia el servicio de adquisición de datos desde el puerto serial."""
        if self.running:
            return True

        try:
            self.serial_conn = serial.Serial(self.serial_port, self.baud_rate, timeout=1)
            self.running = True
            self.thread = threading.Thread(target=self._read_serial_data)
            self.thread.daemon = True
            self.thread.start()
            print(f"DAS iniciado en el puerto serial {self.serial_port} a {self.baud_rate} baudios")
            return True
        except Exception as e:
            print(f"Error al iniciar DAS en el puerto serial: {e}")
            return False

    def stop(self) -> None:
        """Detiene el servicio de adquisición de datos."""
        self.running = False
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
        if self.thread:
            self.thread.join(timeout=1.0)
            self.thread = None

    def _read_serial_data(self) -> None:
        """Lee datos línea por línea desde el puerto serial, con reintentos si hay desconexión."""
        while self.running:
            if not self.serial_conn or not self.serial_conn.is_open:
                try:
                    self.serial_conn = serial.Serial(self.serial_port, self.baud_rate, timeout=1)
                    print(f"Conectado al puerto serial {self.serial_port}")
                except Exception as e:
                    print(f"No se pudo conectar al puerto serial {self.serial_port}: {e}")
                    time.sleep(5)  # Espera antes de reintentar
                    continue

            try:
                line = self.serial_conn.readline().decode('utf-8').strip()
                if line:
                    self._process_data(line.encode('utf-8'))
            except serial.SerialException as e:
                print(f"Error leyendo desde serial: {e}. Intentando reconectar...")
                try:
                    self.serial_conn.close()
                except Exception:
                    pass
                self.serial_conn = None
                time.sleep(5)  # Espera antes de reconectar
            except Exception as e:
                print(f"Error inesperado leyendo desde serial: {e}")
                time.sleep(1)
            
    def _process_data(self, data: bytes) -> int:
        """Procesa datos recibidos (en formato JSON)."""
        count = 0
        try:
            json_data = json.loads(data.decode('utf-8'))

            if isinstance(json_data, list):
                for sensor in json_data:
                    if not isinstance(sensor, dict):
                        continue

                    name = sensor.get("name")
                    value = sensor.get("value")
                    timestamp = sensor.get("timestamp", int(time.time()))
                    units = sensor.get("units", "")

                    if name is not None and value is not None:
                        self._store_sensor_reading(name, value, timestamp, units)
                        count += 1
            elif isinstance(json_data, dict):
                name = json_data.get("name")
                value = json_data.get("value")
                timestamp = json_data.get("timestamp", int(time.time()))
                units = json_data.get("units", "")

                if name is not None and value is not None:
                    self._store_sensor_reading(name, value, timestamp, units)
                    count += 1

        except json.JSONDecodeError:
            print(f"JSON inválido: {data.decode('utf-8')}")
        except Exception as e:
            print(f"Error al procesar datos: {e}")

        return count

    def _store_sensor_reading(self, name: str, value: Any, timestamp: int, units: str) -> None:
        """Almacena una lectura en la base de datos y ejecuta callbacks."""
        try:
            if not isinstance(value, str):
                value = str(value)

            self.db.add_reading(name, value, timestamp, units)
            self.total_readings_received += 1

            for callback in self.on_data_received_callbacks:
                try:
                    callback(name, {
                        "value": value,
                        "timestamp": timestamp,
                        "units": units
                    })
                except Exception as e:
                    print(f"Error en callback de datos: {e}")

            if self.verbose:
                print(f"Lectura almacenada: {name} = {value}{units} @ {timestamp}")
        except Exception as e:
            print(f"Error al guardar lectura: {e}")

    def add_data_callback(self, callback: Callable[[str, Any], None]) -> None:
        """Agrega una función callback que se ejecutará cuando llegue una nueva lectura."""
        self.on_data_received_callbacks.append(callback)

    def get_stats(self) -> Dict[str, Any]:
        """Devuelve estadísticas básicas del servicio."""
        return {
            "readings_received": self.total_readings_received,
            "running": self.running
        }

    def set_verbose(self, verbose: bool) -> None:
        """Activa o desactiva el modo detallado."""
        self.verbose = verbose
        
    def clear_callbacks(self) -> None:
        """Elimina todos los callbacks registrados."""
        self.on_data_received_callbacks = []