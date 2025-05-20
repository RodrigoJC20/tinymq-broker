"""
TinyMQ client implementation.

This module provides the client functionality for the TinyMQ protocol.
"""
import json
import socket
import threading
import time
from typing import Dict, Callable, Optional, List

from .packet import Packet, PacketType


class Client:
    """TinyMQ client implementation."""
    
    def __init__(self, client_id: str, host: str = "localhost", port: int = 1505):
        """
        Initialize a TinyMQ client.
        
        Args:
            client_id: Unique identifier for this client
            host: Broker hostname or IP address
            port: Broker port
        """
        
        self._recv_lock = threading.Lock()
        self._cached_admin_requests = []  # Inicializar la variable
        self.client_id = client_id
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.connected = False
        
        self.topic_handlers: Dict[str, Callable[[str, bytes], None]] = {}
        self.read_thread: Optional[threading.Thread] = None
        self.running = False
        
        self._recv_buffer = bytearray()
        self._recv_lock = threading.Lock()
    
    def create_topic(self, topic: str, callback: Callable[[str, bytes], None] = None) -> bool:
        """
        Crea un tópico (publicando un mensaje especial) y se suscribe automáticamente a él.
        """
        if not self.connected:
            print("No conectado al broker.")
            return False

        if callback is None:
            def callback(t, m):
                print(f"[AUTO] Mensaje recibido en '{t}': {m}")
        
        sub_ok = self.subscribe(topic, callback)
        if not sub_ok:
            print(f"No se pudo suscribir al tópico '{topic}'")
            return False

        # Crear un mensaje especial que el broker pueda identificar
        topic_create_message = json.dumps({
            "__topic_create": True,
            "client_id": self.client_id,
            "topic_name": topic,
            "timestamp": int(time.time())
        })

        # Publicar el mensaje especial en lugar de un mensaje vacío
        pub_ok = self.publish(topic, topic_create_message)
        if not pub_ok:
            print(f"No se pudo crear/publicar en el tópico '{topic}'")
            return False

        print(f"[SUCCESS] Tópico '{topic}' creado correctamente en el broker")
        return True
    
    def connect(self) -> bool:
        """
        Connect to the TinyMQ broker.
        
        Returns:
            True if connected successfully, False otherwise.
        """
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            
            # Send CONN packet with client ID
            conn_packet = Packet(
                packet_type=PacketType.CONN,
                payload=self.client_id.encode('utf-8')
            )
            self._send_packet(conn_packet)
            
            # Start the read thread
            self.running = True
            self.read_thread = threading.Thread(target=self._read_loop)
            self.read_thread.daemon = True
            self.read_thread.start()
            
            # Wait for CONNACK
            start_time = time.time()
            while not self.connected and time.time() - start_time < 5:
                time.sleep(0.1)
            
            return self.connected
        except Exception as e:
            print(f"Connection error: {e}")
            return False
    
    def disconnect(self) -> None:
        """Disconnect from the TinyMQ broker."""
        self.running = False
        if self.read_thread:
            # Only attempt to join the thread if we're not in the read thread itself
            current_thread = threading.current_thread()
            if current_thread != self.read_thread:
                self.read_thread.join(timeout=1.0)
        
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
        
        self.connected = False
    
    def publish(self, topic: str, message: str) -> bool:
        """
        Publish a message to a topic.
        
        Args:
            topic: Topic to publish to
            message: Message to publish
            
        Returns:
            True if the message was sent, False otherwise.
        """
        if not self.connected:
            return False
        
        try:
            broker_topic = f"{self.client_id}/{topic}"
            wrapped_topic = json.dumps([broker_topic])  # Esto crea '["client_id/topic"]'

            broker_topic_bytes = wrapped_topic.encode('utf-8')
            topic_length = len(broker_topic_bytes)
            
            if topic_length > 255:
                print(f"Error: Topic '{broker_topic}' is too long (max 255 bytes).")
                return False
            
            message_bytes = message.encode('utf-8')
            
            payload = bytes([topic_length]) + broker_topic_bytes + message_bytes
            
            packet = Packet(packet_type=PacketType.PUB, payload=payload)
            # Print packet details
            print(f"Sending packet: Type={packet.packet_type.name}, Flags={packet.flags}, Payload Length={len(packet.payload)}")
            result = self._send_packet(packet)
            return result
        except Exception as e:
            print(f"Publish error: {e}")
            return False
    
    def subscribe(self, topic: str, callback: Callable[[str, bytes], None]) -> bool:
        """
        Subscribe to a topic.
        
        Args:
            topic: Topic to subscribe to
            callback: Function to call when a message is received
            
        Returns:
            True if the subscription request was sent, False otherwise.
        """
        if not self.connected:
            return False
        
        try:
            # Format: ["topic_name"]
            payload = json.dumps([topic]).encode('utf-8')
            packet = Packet(packet_type=PacketType.SUB, payload=payload)
            
            if self._send_packet(packet):
                self.topic_handlers[topic] = callback
                return True
            return False
        except Exception as e:
            print(f"Subscribe error: {e}")
            return False
    
    def unsubscribe(self, topic: str) -> bool:
        """
        Unsubscribe from a topic.
        
        Args:
            topic: Topic to unsubscribe from
            
        Returns:
            True if the unsubscribe request was sent, False otherwise.
        """
        if not self.connected:
            return False
        
        try:
            # Format: ["topic_name"]
            payload = json.dumps([topic]).encode('utf-8')
            packet = Packet(packet_type=PacketType.UNSUB, payload=payload)
            
            if self._send_packet(packet):
                if topic in self.topic_handlers:
                    del self.topic_handlers[topic]
                return True
            return False
        except Exception as e:
            print(f"Unsubscribe error: {e}")
            return False
    
    def _send_packet(self, packet: Packet) -> bool:
        """Send a packet to the broker."""
        if not self.socket:
            return False
        
        try:
            data = packet.serialize()
            self.socket.sendall(data)
            return True
        except Exception as e:
            print(f"Send error: {e}")
            self.disconnect()
            return False
    
    def _read_loop(self) -> None:
        """Read packets from the broker."""
        while self.running and self.socket:
            try:
                # Read some data
                data = self.socket.recv(4096)
                if not data:
                    # Connection closed
                    break
                # Append to buffer
                with self._recv_lock:
                    self._recv_buffer.extend(data)
                    buffer = self._recv_buffer.copy()
                
                # Process packets
                consumed = 0
                while consumed < len(buffer):
                    packet, bytes_consumed = Packet.deserialize(buffer[consumed:])
                    if packet is None:
                        # Need more data
                        break
                    self._handle_packet(packet)
                    consumed += bytes_consumed
                
                # Remove processed data
                with self._recv_lock:
                    self._recv_buffer = self._recv_buffer[consumed:]
                
            except Exception as e:
                #print(f"Read error: {e}")
                break
        
        # Ensure we're disconnected on error, but don't call disconnect() directly
        # as it would try to join the current thread
        self.running = False
        self.connected = False
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
    
    def _handle_packet(self, packet: Packet) -> None:
        """
        Handle a received packet.
        
        Args:
            packet: Packet to handle
        """
        # Añadir debug para todos los paquetes
        print(f"DEBUG: Recibido paquete tipo {packet.packet_type.name}, tamaño payload: {len(packet.payload)} bytes")

        if packet.packet_type == PacketType.CONNACK:
            self.connected = True
        
        elif packet.packet_type == PacketType.PUBACK:
            # Could track message IDs for QoS in future
            print(f"DEBUG: PacketType.PUBACK")
            pass
            
        elif packet.packet_type == PacketType.SUBACK:
            # Could track subscription IDs in future
            print(f"DEBUG: PacketType.SUBACK")
            pass
            
        elif packet.packet_type == PacketType.UNSUBACK:
            print(f"DEBUG: PacketType.UNSUBACK")
            # Could track unsubscription IDs in future
            pass
            
        elif packet.packet_type == PacketType.PUB:
            try:
                data = json.loads(packet.payload.decode('utf-8'))
                topic_raw = data.get("topic")

                # Aquí convertimos topic_raw de JSON para que sea lista o string limpio
                try:
                    topic_parsed = json.loads(topic_raw)
                except Exception:
                    topic_parsed = topic_raw
                # Si es lista, extraemos el primer elemento (string)
                if isinstance(topic_parsed, list):
                    topic = topic_parsed[0]
                else:
                    topic = topic_parsed
                message = data.get("message", "")

                if topic and topic in self.topic_handlers:
                    print(f"[DEBUG] Handler JSON para tópico '{topic}'")
                    self.topic_handlers[topic](topic, message)

            except json.JSONDecodeError:
                raw = packet.payload

                try:
                    topic_len = raw[0]
                    topic_bytes = raw[1:1 + topic_len]
                    topic_raw = topic_bytes.decode('utf-8')

                    try:
                        topic = json.loads(topic_raw)[0] 
                    except Exception:
                        topic = topic_raw.strip('"') 

                    msg_bytes = raw[1 + topic_len:]
                    message = msg_bytes.decode('utf-8')

                    if topic in self.topic_handlers:
                        self.topic_handlers[topic](topic, message)
                    else:
                        print(f"[WARN] No hay handler registrado para tópico '{topic}'")
                except Exception as e2:
                    print(f"[ERROR] Falló el parseo personalizado del paquete PUB: {e2}")

                except Exception as e:
                    print(f"[ERROR] Fallo en formato personalizado: {e}")
                    print(f"[ERROR] Payload completo: {raw!r}")

            except Exception as e:
                print(f"[ERROR] Error general al manejar PUB packet: {e}")

        # Añadir el manejador para ADMIN_RESP
        elif packet.packet_type == PacketType.ADMIN_RESP:
            try:
                data = json.loads(packet.payload.decode('utf-8'))
                self._cached_admin_requests = data
                print(f"[INFO] Recibidas {len(data)} solicitudes administrativas")
                # Opcionalmente, puedes notificar a la interfaz gráfica que hay nuevas solicitudes
                # si tienes algún tipo de mecanismo de callback configurado
            except Exception as e:
                print(f"[ERROR] Error procesando respuesta administrativa: {e}")

        # Añadir el manejador para TOPIC_RESP
        elif packet.packet_type == PacketType.TOPIC_RESP:
            print(f"[DEBUG] Recibido paquete TOPIC_RESP estándar")
            # Este caso debería ser manejado por _register_temp_packet_handler
            # pero se añade aquí para completitud

    def get_published_topics(self) -> List[Dict[str, str]]:
        """
        Obtiene una lista de tópicos publicados desde el broker.
        
        Returns:
            Lista de diccionarios con información de los tópicos publicados.
            Cada diccionario contiene 'name' y 'owner' como claves.
        """
        if not self.connected:
            print("No conectado al broker.")
            return []
        
        try:
            # Crear y enviar el paquete de solicitud
            packet = Packet(packet_type=PacketType.TOPIC_REQ)
            if not self._send_packet(packet):
                print("Error al enviar solicitud TOPIC_REQ")
                return []
            
            # Añadir manejador para la respuesta
            topic_response = []
            response_received = False
            
            def topic_response_handler(packet_type, payload):
                nonlocal topic_response, response_received
                if packet_type == PacketType.TOPIC_RESP:
                    try:
                        # Decodificar el JSON
                        json_str = payload.decode('utf-8')
                        topics_data = json.loads(json_str)
                        topic_response = topics_data
                        response_received = True
                    except Exception as e:
                        print(f"Error decodificando lista de tópicos: {e}")
                    
            # Registrar temporalmente un handler para procesar la respuesta
            self._register_temp_packet_handler(PacketType.TOPIC_RESP, topic_response_handler)
            
            # Esperar por la respuesta con timeout
            start_time = time.time()
            while not response_received and time.time() - start_time < 10:  # 10 segundos de timeout
                time.sleep(0.1)  # Pequeña pausa para reducir uso de CPU
            
            if not response_received:
                print("Timeout esperando respuesta del broker")
                
            return topic_response
            
        except Exception as e:
            print(f"Error solicitando tópicos publicados: {e}")
            return []
        
    def _register_temp_packet_handler(self, packet_type, handler_func):
        """
        Registra un manejador temporal para un tipo específico de paquete.
        
        Args:
            packet_type: El tipo de paquete a manejar
            handler_func: Función que recibe (packet_type, payload)
        """
        original_handle_packet = self._handle_packet
        
        def wrapper_handler(packet):
            if packet.packet_type == packet_type:
                handler_func(packet.packet_type, packet.payload)
            return original_handle_packet(packet)
        
        self._handle_packet = wrapper_handler
        
    def set_topic_publish(self, topic: str, publish: bool = True) -> bool:
        """
        Cambia el estado de publicación de un tópico en el broker.
        
        Args:
            topic: Nombre del tópico a modificar
            publish: True para activar publicación, False para desactivar
            
        Returns:
            True si se envió el comando correctamente, False en caso contrario
        """
        if not self.connected:
            print("No conectado al broker.")
            return False
            
        try:
            # Crear un mensaje especial para cambiar el estado de publicación
            publish_message = json.dumps({
                "__topic_publish": True,
                "client_id": self.client_id,
                "topic_name": topic,
                "publish": publish,
                "timestamp": int(time.time())
            })
            
            # Publicar este mensaje en el tópico correspondiente
            result = self.publish(topic, publish_message)
            if result:
                print(f"[INFO] Estado de publicación para '{topic}' cambiado a: {'ON' if publish else 'OFF'}")
            return result
        except Exception as e:
            print(f"Error al cambiar estado de publicación: {e}")
            return False
        
    def request_admin_status(self, topic_name: str, owner_id: str) -> bool:
        """
        Solicita ser administrador de un tópico.
        
        Args:
            topic_name: Nombre del tópico
            owner_id: ID del cliente dueño del tópico
            
        Returns:
            True si la solicitud se envió correctamente
        """
        if not self.connected:
            return False
            
        try:
            # Crear mensaje especial para solicitud de administrador
            request_message = json.dumps({
                "__admin_request": True,
                "client_id": self.client_id,
                "topic_name": topic_name,
                "owner_id": owner_id,
                "timestamp": int(time.time())
            })
            
            # El tópico para enviar la solicitud es uno especial
            admin_topic = f"{owner_id}/admin"  # Mantener esta estructura
            
            # Publicar mensaje
            result = self.publish(admin_topic, request_message)
            return result
        except Exception as e:
            print(f"Error al solicitar estado de administrador: {e}")
            return False

    def register_admin_notification_handler(self, callback):
            """Registra un handler para recibir notificaciones de administración"""
            if not self.connected:
                return False
                
            try:
                # Suscribirse al tópico especial para notificaciones administrativas
                notification_topic = f"{self.client_id}/admin"  # Cambiar a esta estructura

                
                def notification_handler(topic_str, message):
                    try:
                        data = json.loads(message)
                        if "__admin_notification" in data:
                            callback(data)
                    except Exception as e:
                        print(f"Error procesando notificación: {e}")
                
                return self.subscribe(notification_topic, notification_handler)
            except Exception as e:
                print(f"Error registrando handler de notificaciones: {e}")
                return False
            
    def get_admin_requests(self):
        """Obtiene las solicitudes de administración pendientes"""
        try:
            # Verificar que estamos conectados
            if not self.connected:
                return []
            
            # Solicitar las peticiones al broker
            packet = Packet(packet_type=PacketType.ADMIN_REQ)
            if not self._send_packet(packet):
                print("Error al enviar solicitud de administración")
                return []
                
            # Implementación real que devuelve las solicitudes
            # Esto es solo un ejemplo y debe adaptarse a tu sistema
            return self._cached_admin_requests  # Esta variable debería poblarse cuando llegan notificaciones
        except Exception as e:
            print(f"Error al obtener solicitudes de administración: {e}")
            return []
        
    def respond_to_admin_request(self, request_id, topic_name, requester_id, approve):
        """
        Responde a una solicitud de administración.
        
        Args:
            request_id: ID de la solicitud
            topic_name: Nombre del tópico
            requester_id: ID del cliente solicitante
            approve: True para aprobar, False para rechazar
            
        Returns:
            True si se envió correctamente
        """
        if not self.connected:
            return False
            
        try:
            # Crear mensaje de respuesta
            response = json.dumps({
                "__admin_response": True,
                "client_id": self.client_id,
                "request_id": request_id,
                "topic_name": topic_name,
                "requester_id": requester_id,
                "approved": approve,
                "timestamp": int(time.time())
            })
            
            # Enviar a tópico especial 
            response_topic = f"system/admin/responses"
            
            # Publicar respuesta
            return self.publish(response_topic, response)
        except Exception as e:
            print(f"Error respondiendo a solicitud: {e}")
            return False
        
    def set_sensor_status(self, topic_name, sensor_name, active):
        """
        Configura el estado de un sensor como administrador.
        
        Args:
            topic_name: Nombre del tópico
            sensor_name: Nombre del sensor
            active: True para activar, False para desactivar
            
        Returns:
            True si se envió correctamente
        """
        if not self.connected:
            return False
            
        try:
            # Crear mensaje de configuración
            config = json.dumps({
                "__admin_sensor_config": True,
                "client_id": self.client_id,
                "topic_name": topic_name,
                "sensor_name": sensor_name, 
                "active": active,
                "timestamp": int(time.time())
            })
            
            # Tópico para configuración
            config_topic = f"system/admin/config"
            
            # Publicar configuración
            return self.publish(config_topic, config)
        except Exception as e:
            print(f"Error configurando sensor: {e}")
            return False