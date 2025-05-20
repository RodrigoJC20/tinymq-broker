#!/usr/bin/env python3
"""
TinyMQ Client GUI

Interfaz gráfica simplificada y organizada para el cliente TinyMQ.
"""
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, simpledialog  # Añadido simpledialog
import threading
import time
from datetime import datetime
from typing import Dict, List, Optional, Any, Tuple
import json
import re

from tinymq import Client, DataAcquisitionService, Database

class TinyMQGUI:
    """Interfaz gráficaa simplificada para el cliente TinyMQ."""

    def __init__(self, root):  # Cambiar _init_ a __init__
        self.root = root
        self.root.title("TinyMQ Client")
        self.root.geometry("900x600")
        self.db = Database()
        self.das = None
        self.client = None
        self.running = True
        self.topic_owners = {} 

        self.configure_style()
        self.create_widgets()
        self.start_das()

        self.update_thread = threading.Thread(target=self.update_data_loop, daemon=True)
        self.update_thread.start()

    def configure_style(self):
        style = ttk.Style()
        style.configure('TButton', font=('Helvetica', 10))
        style.configure('TLabel', font=('Helvetica', 10))
        style.configure('Header.TLabel', font=('Helvetica', 12, 'bold'))

    def create_widgets(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=10, pady=10)
        
        # Binding para detectar cuando se cambia de pestaña
        self.notebook.bind("<<NotebookTabChanged>>", self.on_tab_changed)

        self.create_dashboard_tab()
        self.create_sensors_tab()
        self.create_topics_tab()
        self.create_subscriptions_tab()
        self.create_admin_tab()

        # Barra de estado
        self.status_bar = ttk.Frame(self.root)
        self.status_bar.pack(fill="x", side="bottom")
        self.status_label = ttk.Label(self.status_bar, text="Listo", anchor="w")
        self.status_label.pack(side="left", padx=5)
        self.readings_label = ttk.Label(self.status_bar, text="Lecturas: 0", anchor="e")
        self.readings_label.pack(side="right", padx=10)

    def on_tab_changed(self, event):
        """Actualiza los datos de la pestaña seleccionada automáticamente."""
        tab_idx = self.notebook.index('current')
        
        # Actualizar según la pestaña activa (0=Dashboard, 1=Sensores, 2=Tópicos, 3=Suscripciones)
        if tab_idx == 0:  # Dashboard
            self.refresh_stats()
        elif tab_idx == 1:  # Sensores
            self.refresh_sensors()
        elif tab_idx == 2:  # Tópicos
            self.refresh_topics()
        elif tab_idx == 3:  # Suscripciones
            self.refresh_subscriptions()
            # Asegurarse de que esto se ejecuta
            print("Actualizando tópicos públicos...")
            self.refresh_public_topics()
        
        self.status_label.config(text=f"Pestaña '{self.notebook.tab(tab_idx, 'text')}' actualizada")
        self.readings_label.pack(side="right", padx=10)

    def create_dashboard_tab(self):
        tab = ttk.Frame(self.notebook)
        self.notebook.add(tab, text="Inicio")

        frame = ttk.LabelFrame(tab, text="Estado de Conexión")
        frame.pack(fill="x", padx=10, pady=10)

        # Sección para IP y puerto del servidor
        server_frame = ttk.Frame(frame)
        server_frame.pack(pady=5)
        ttk.Label(server_frame, text="IP del servidor:").pack(side="left", padx=5)
        # Cargar host y puerto guardados, si existen
        saved_host = self.db.get_broker_host() or "localhost"
        saved_port = self.db.get_broker_port() or 1505
        self.host_entry = ttk.Entry(server_frame, width=16)
        self.host_entry.pack(side="left", padx=5)
        self.host_entry.insert(0, saved_host)
        ttk.Label(server_frame, text="Puerto:").pack(side="left", padx=5)
        self.port_entry = ttk.Entry(server_frame, width=6)
        self.port_entry.pack(side="left", padx=5)
        self.port_entry.insert(0, str(saved_port))

        self.status_var = tk.StringVar(value="Desconectado")
        ttk.Label(frame, textvariable=self.status_var, style='Header.TLabel').pack(pady=5)

        btn_frame = ttk.Frame(frame)
        btn_frame.pack(pady=5)
        self.connect_btn = ttk.Button(btn_frame, text="Conectar", command=self.connect_to_broker)
        self.connect_btn.pack(side="left", padx=5)
        self.disconnect_btn = ttk.Button(btn_frame, text="Desconectar", command=self.disconnect_from_broker, state="disabled")
        self.disconnect_btn.pack(side="left", padx=5)

        # Cliente
        client_frame = ttk.LabelFrame(tab, text="Identidad")
        client_frame.pack(fill="x", padx=10, pady=5)
        
        # Inicializar todas las variables StringVar primero
        self.client_id_var = tk.StringVar()
        self.name_var = tk.StringVar()
        self.email_var = tk.StringVar()
        
        # Ahora cargar los datos y asignarlos a las variables
        current_id = self.db.get_client_id() or ""
        self.client_id_var.set(current_id)
        
        # Cargar metadatos
        metadata = self.db.get_client_metadata()
        if metadata:
            self.name_var.set(metadata.get("name", ""))
            self.email_var.set(metadata.get("email", ""))
        
        # Ahora crear los widgets con las variables ya inicializadas
        ttk.Label(client_frame, text="ID:").pack(side="left", padx=5)
        ttk.Entry(client_frame, textvariable=self.client_id_var, width=15).pack(side="left", padx=5)
        ttk.Button(client_frame, text="Cambiar ID", command=self.change_client_id).pack(side="left", padx=5)
        
        ttk.Label(client_frame, text="Nombre:").pack(side="left", padx=5)
        ttk.Entry(client_frame, textvariable=self.name_var, width=15).pack(side="left", padx=5)
        
        ttk.Label(client_frame, text="Email:").pack(side="left", padx=5)
        ttk.Entry(client_frame, textvariable=self.email_var, width=20).pack(side="left", padx=5)
        
        ttk.Button(client_frame, text="Actualizar", command=self.update_metadata).pack(side="left", padx=5)

        # Estadísticas
        stats_frame = ttk.LabelFrame(tab, text="Estadísticas")
        stats_frame.pack(fill="x", padx=10, pady=5)
        self.stats_text = tk.Text(stats_frame, height=12, wrap="word")
        self.stats_text.pack(fill="both", expand=True, padx=5, pady=5)
        self.stats_text.config(state="disabled")
        ttk.Button(stats_frame, text="Refrescar", command=self.refresh_stats).pack(pady=5)

    def create_sensors_tab(self):
        tab = ttk.Frame(self.notebook)
        self.notebook.add(tab, text="Sensores")

        main_frame = ttk.Frame(tab)
        main_frame.pack(fill="both", expand=True, padx=10, pady=10)

        # Lista de sensores
        left = ttk.LabelFrame(main_frame, text="Sensores")
        left.pack(side="left", fill="y", padx=(0, 10))
        self.sensors_listbox = tk.Listbox(left, width=30)
        self.sensors_listbox.pack(fill="y", expand=True, padx=5, pady=5)
        self.sensors_listbox.bind('<<ListboxSelect>>', self.on_sensor_selected)
        ttk.Button(left, text="Refrescar", command=self.refresh_sensors).pack(fill="x", padx=5, pady=5)

        # Detalles
        right = ttk.LabelFrame(main_frame, text="Detalles")
        right.pack(side="left", fill="both", expand=True)
        
        # Información básica del sensor
        info = ttk.Frame(right)
        info.pack(fill="x", padx=10, pady=10)
        ttk.Label(info, text="ID:").grid(row=0, column=0, sticky="w")
        self.sensor_id_var = tk.StringVar()
        ttk.Label(info, textvariable=self.sensor_id_var).grid(row=0, column=1, sticky="w")
        ttk.Label(info, text="Nombre:").grid(row=1, column=0, sticky="w")
        self.sensor_name_var = tk.StringVar()
        ttk.Label(info, textvariable=self.sensor_name_var).grid(row=1, column=1, sticky="w")
        ttk.Label(info, text="Último valor:").grid(row=2, column=0, sticky="w")
        self.sensor_value_var = tk.StringVar()
        ttk.Label(info, textvariable=self.sensor_value_var).grid(row=2, column=1, sticky="w")
        ttk.Label(info, text="Última actualización:").grid(row=3, column=0, sticky="w")
        self.sensor_updated_var = tk.StringVar()
        ttk.Label(info, textvariable=self.sensor_updated_var).grid(row=3, column=1, sticky="w")

        # Pestañas para tiempo real e historial
        self.sensor_data_notebook = ttk.Notebook(right)
        self.sensor_data_notebook.pack(fill="both", expand=True, padx=10, pady=5)
        
        # Pestaña de tiempo real
        realtime_frame = ttk.Frame(self.sensor_data_notebook)
        self.sensor_data_notebook.add(realtime_frame, text="Tiempo Real")
        
        # Opciones para tiempo real
        realtime_controls = ttk.Frame(realtime_frame)
        realtime_controls.pack(fill="x", pady=5)
        self.realtime_active_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(realtime_controls, text="Activar monitoreo", variable=self.realtime_active_var, 
                       command=self.toggle_realtime_monitoring).pack(side="left", padx=5)
        ttk.Button(realtime_controls, text="Limpiar", command=self.clear_realtime_data).pack(side="left", padx=5)
        
        # Vista de datos en tiempo real
        self.realtime_text = scrolledtext.ScrolledText(realtime_frame, height=8)
        self.realtime_text.pack(fill="both", expand=True, padx=5, pady=5)
        self.realtime_text.config(state="disabled")
        
        # Pestaña de historial (existente)
        history_frame = ttk.Frame(self.sensor_data_notebook)
        self.sensor_data_notebook.add(history_frame, text="Historial")
        
        # Controles de historial
        controls = ttk.Frame(history_frame)
        controls.pack(fill="x", pady=5)
        ttk.Label(controls, text="Límite:").pack(side="left", padx=5)
        self.history_limit_var = tk.StringVar(value="20")
        ttk.Combobox(controls, textvariable=self.history_limit_var, values=["10", "20", "50", "100"], width=5, state="readonly").pack(side="left", padx=5)
        ttk.Button(controls, text="Cargar", command=self.load_sensor_history).pack(side="left", padx=5)
        
        # Vista de historial
        self.history_text = scrolledtext.ScrolledText(history_frame, height=8)
        self.history_text.pack(fill="both", expand=True, padx=5, pady=5)
        self.history_text.config(state="disabled")

    # Métodos adicionales para el monitoreo en tiempo real
    def toggle_realtime_monitoring(self):
        """Activa o desactiva el monitoreo en tiempo real para el sensor seleccionado."""
        if self.realtime_active_var.get():
            sensor_id = self.sensor_id_var.get()
            if not sensor_id:
                messagebox.showinfo("Información", "Selecciona un sensor primero")
                self.realtime_active_var.set(False)
                return
            # Si activamos, limpiar la vista
            self.clear_realtime_data()
            self.realtime_text.config(state="normal")
            self.realtime_text.insert(tk.END, "Monitoreo en tiempo real activado. Esperando datos...\n\n")
            self.realtime_text.config(state="disabled")
        else:
            self.realtime_text.config(state="normal")
            self.realtime_text.insert(tk.END, "Monitoreo en tiempo real desactivado.\n")
            self.realtime_text.config(state="disabled")

    def clear_realtime_data(self):
        """Limpia los datos en tiempo real."""
        self.realtime_text.config(state="normal")
        self.realtime_text.delete("1.0", tk.END)
        self.realtime_text.config(state="disabled")
        
    def on_sensor_data(self, sensor_name, data):
        """Callback cuando se recibe un nuevo dato de sensor."""
        # Actualizar el monitoreo en tiempo real si está activo
        current_sensor_name = self.sensor_name_var.get()
        if self.realtime_active_var.get() and sensor_name == current_sensor_name:
            timestamp = datetime.fromtimestamp(data["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
            value_text = f"{data['value']} {data.get('units', '')}"
            
            # Actualizar la interfaz de usuario en el hilo principal
            self.root.after(0, lambda: self.update_realtime_display(timestamp, value_text))
        
        # También actualizar últimos valores si es el sensor actual
        if sensor_name == current_sensor_name:
            self.root.after(0, lambda: self.update_sensor_latest_value(data))
    
    def update_realtime_display(self, timestamp, value_text):
        """Actualiza la visualización en tiempo real (llamada desde el hilo principal)."""
        self.realtime_text.config(state="normal")
        
        # Mantener un máximo de líneas (por ejemplo, 100)
        lines = self.realtime_text.get("1.0", tk.END).splitlines()
        if len(lines) > 100:
            self.realtime_text.delete("1.0", f"{len(lines) - 100}.0")
        
        self.realtime_text.insert(tk.END, f"{timestamp}: {value_text}\n")
        self.realtime_text.see(tk.END)  # Desplazarse automáticamente al final
        self.realtime_text.config(state="disabled")
    
    def update_sensor_latest_value(self, data):
        """Actualiza los valores más recientes del sensor en la interfaz."""
        self.sensor_value_var.set(f"{data['value']} {data.get('units', '')}")
        timestamp = datetime.fromtimestamp(data["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
        self.sensor_updated_var.set(timestamp)
        
    def create_topics_tab(self):
        tab = ttk.Frame(self.notebook)
        self.notebook.add(tab, text="Tópicos")

        main_frame = ttk.Frame(tab)
        main_frame.pack(fill="both", expand=True, padx=10, pady=10)

        # Lista de tópicos con selección múltiple
        left = ttk.LabelFrame(main_frame, text="Tópicos")
        left.pack(side="left", fill="y", padx=(0, 10))
        self.topics_listbox = tk.Listbox(left, width=30, selectmode=tk.EXTENDED)  # Cambio aquí para permitir selección múltiple
        self.topics_listbox.pack(fill="y", expand=True, padx=5, pady=5)
        self.topics_listbox.bind('<<ListboxSelect>>', self.on_topic_selected)
        ttk.Button(left, text="Refrescar", command=self.refresh_topics).pack(fill="x", padx=5, pady=5)
        # Botón para crear tópico
        ttk.Button(left, text="Crear Tópico", command=self.open_create_topic_dialog).pack(fill="x", padx=5, pady=5)


        # Detalles y acciones
        right = ttk.LabelFrame(main_frame, text="Detalles")
        right.pack(side="left", fill="both", expand=True)
        info = ttk.Frame(right)
        info.pack(fill="x", padx=10, pady=10)
        ttk.Label(info, text="ID:").grid(row=0, column=0, sticky="w")
        self.topic_id_var = tk.StringVar()
        ttk.Label(info, textvariable=self.topic_id_var).grid(row=0, column=1, sticky="w")
        ttk.Label(info, text="Nombre:").grid(row=1, column=0, sticky="w")
        self.topic_name_var = tk.StringVar()
        ttk.Label(info, textvariable=self.topic_name_var).grid(row=1, column=1, sticky="w")
        ttk.Label(info, text="Publicando:").grid(row=2, column=0, sticky="w")
        self.topic_publish_var = tk.StringVar()
        ttk.Label(info, textvariable=self.topic_publish_var).grid(row=2, column=1, sticky="w")

        pub_frame = ttk.Frame(right)
        pub_frame.pack(fill="x", padx=10, pady=5)
        ttk.Button(pub_frame, text="Activar Publicación", command=lambda: self.toggle_topic_publish(True)).pack(side="left", padx=5)
        ttk.Button(pub_frame, text="Desactivar Publicación", command=lambda: self.toggle_topic_publish(False)).pack(side="left", padx=5)

        # Sensores asociados
        sensors_frame = ttk.LabelFrame(right, text="Sensores en el Tópico")
        sensors_frame.pack(fill="both", expand=True, padx=10, pady=5)
        self.topic_sensors_text = scrolledtext.ScrolledText(sensors_frame, height=5)
        self.topic_sensors_text.pack(fill="both", expand=True, padx=5, pady=5)
        self.topic_sensors_text.config(state="disabled")

        # Añadir sensor
        add_frame = ttk.Frame(sensors_frame)
        add_frame.pack(fill="x", padx=5, pady=5)
        ttk.Label(add_frame, text="Sensor:").pack(side="left", padx=5)
        self.sensor_to_add_var = tk.StringVar()
        self.sensor_combo = ttk.Combobox(add_frame, textvariable=self.sensor_to_add_var, state="readonly")
        self.sensor_combo.pack(side="left", padx=5, expand=True, fill="x")
        ttk.Button(add_frame, text="Agregar", command=self.add_sensor_to_topic).pack(side="left", padx=5)
        ttk.Button(add_frame, text="Eliminar", command=self.remove_sensor_from_topic).pack(side="left", padx=5)

    def open_create_topic_dialog(self):
        dialog = tk.Toplevel(self.root)
        dialog.title("Crear nuevo tópico")
        dialog.geometry("320x150")
        dialog.transient(self.root)
        dialog.grab_set()

        ttk.Label(dialog, text="Nombre del tópico:").pack(pady=(10, 2))
        name_var = tk.StringVar()
        name_entry = ttk.Entry(dialog, textvariable=name_var, width=25)
        name_entry.pack(pady=2)
        name_entry.focus()

        publish_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(dialog, text="Publicar al crear", variable=publish_var).pack(pady=2)

        # Botones
        button_frame = ttk.Frame(dialog)
        button_frame.pack(pady=10)
        ttk.Button(button_frame, text="Crear", command=lambda: on_create()).pack(side="left", padx=5)
        ttk.Button(button_frame, text="Cancelar", command=dialog.destroy).pack(side="left", padx=5)

        def on_create():
            name = name_var.get().strip()
            publish = publish_var.get()
            if not name:
                messagebox.showinfo("Información", "Debes ingresar un nombre para el tópico", parent=dialog)
                return
            try:
                # Crear tópico en la BD local
                self.db.create_topic(name, publish)
                
                if self.client and self.client.connected:
                    # Crear tópico en el broker
                    self.client.create_topic(name)
                    
                    # Si se debe publicar, actualizar estado
                    if publish:
                        self.client.set_topic_publish(name, True)
                
                messagebox.showinfo("Éxito", f"Tópico '{name}' creado correctamente", parent=dialog)
                self.refresh_topics()
                self.refresh_public_topics()
                dialog.destroy()
                
                # Añadir reconexión automática si el tópico se marcó para publicación
                if publish:
                    self.reconnect_to_broker()
            except Exception as e:
                messagebox.showerror("Error", f"No se pudo crear el tópico: {str(e)}", parent=dialog)
        
    def create_subscriptions_tab(self):
            tab = ttk.Frame(self.notebook)
            self.notebook.add(tab, text="Suscripciones")

            main_frame = ttk.Frame(tab)
            main_frame.pack(fill="both", expand=True, padx=10, pady=10)

            # Lista de suscripciones
            left = ttk.LabelFrame(main_frame, text="Suscripciones Activas")
            left.pack(side="left", fill="y", padx=(0, 10))
            self.subscriptions_listbox = tk.Listbox(left, width=35)
            self.subscriptions_listbox.pack(fill="y", expand=True, padx=5, pady=5)
            self.subscriptions_listbox.bind('<<ListboxSelect>>', self.on_subscription_selected)
            
            # Botones para gestionar suscripciones
            buttons_frame = ttk.Frame(left)
            buttons_frame.pack(fill="x", padx=5, pady=5)
            ttk.Button(buttons_frame, text="Refrescar", command=self.refresh_subscriptions).pack(side="left", expand=True, fill="x", padx=2)
            ttk.Button(buttons_frame, text="Desuscribirse", command=self.unsubscribe_from_topic).pack(side="left", expand=True, fill="x", padx=2)

            # NUEVO: Lista de tópicos públicos para suscribirse
            public_topics_frame = ttk.LabelFrame(left, text="Tópicos Públicos Disponibles")
            public_topics_frame.pack(fill="x", padx=5, pady=10)
            self.public_topics_combo = ttk.Combobox(public_topics_frame, state="readonly")
            self.public_topics_combo.pack(fill="x", padx=5, pady=5)
            # Vincular evento de clic para refrescar la lista de tópicos públicos
            self.public_topics_combo.bind("<ButtonPress-1>", lambda e: self.refresh_public_topics())
            ttk.Button(public_topics_frame, text="Suscribirse", command=self.subscribe_to_public_topic).pack(fill="x", padx=5, pady=5)

            # Detalles y acciones
            right = ttk.LabelFrame(main_frame, text="Detalles")
            right.pack(side="left", fill="both", expand=True)
            controls = ttk.Frame(right)
            controls.pack(fill="x", padx=10, pady=10)
            
            ttk.Label(controls, text="Tópico:").pack(side="left", padx=5)
            self.sub_topic_var = tk.StringVar()
            self.sub_topic_entry = ttk.Entry(controls, state="readonly", textvariable=self.sub_topic_var)
            self.sub_topic_entry.pack(side="left", padx=5)

            ttk.Label(controls, text="Cliente Origen:").pack(side="left", padx=5)
            self.sub_client_var = tk.StringVar()
            self.sub_client_entry = ttk.Entry(controls, state="readonly", textvariable=self.sub_client_var)
            self.sub_client_entry.pack(side="left", padx=5)
                        
            # Datos de suscripción
            data_frame = ttk.LabelFrame(right, text="Datos Recibidos")
            data_frame.pack(fill="both", expand=True, padx=10, pady=5)
            self.sub_data_text = scrolledtext.ScrolledText(data_frame, height=8)
            self.sub_data_text.pack(fill="both", expand=True, padx=5, pady=5)
            self.sub_data_text.config(state="disabled")
            # Botón para limpiar el texto de datos recibidos
            ttk.Button(data_frame, text="Limpiar", command=self.clear_sub_data).pack(pady=5)

            # NUEVO: Cuadro para escribir mensajes manuales
            message_frame = ttk.LabelFrame(right, text="Enviar Mensaje")
            message_frame.pack(fill="x", padx=10, pady=5)

            self.message_entry = ttk.Entry(message_frame)
            self.message_entry.pack(fill="x", padx=5, pady=5)

            # Botones de acción
            message_buttons = ttk.Frame(message_frame)
            message_buttons.pack(fill="x", padx=5, pady=(0, 5))

            ttk.Button(message_buttons, text="Enviar", command=self.send_message_placeholder).pack(side="left", expand=True, fill="x", padx=2)
            ttk.Button(message_buttons, text="Limpiar Entrada", command=lambda: self.message_entry.delete(0, tk.END)).pack(side="left", expand=True, fill="x", padx=2)

    def send_message_placeholder(self):
        topic_name = self.sub_topic_var.get().strip()
        client_id = self.sub_client_var.get().strip()
        message_text = self.message_entry.get().strip()

        print(f"[MSG'{topic_name} {client_id} { message_text}'")

        # Validar campos vacíos
        if not topic_name or not client_id:
            messagebox.showwarning("Faltan datos", "Por favor selecciona un tópico y un cliente origen.")
            return

        if not message_text:
            messagebox.showwarning("Mensaje vacío", "Escribe un mensaje antes de enviarlo.")
            return

        # Verificar si el cliente está conectado
        if not self.client or not self.client.connected:
            messagebox.showerror("Error de conexión", "El cliente no está conectado.")
            return

        try:
            message = {
                "cliente": client_id,
                "intruccion": message_text,
                "timestamp": time.time(),
            }
            json_message = json.dumps(message)
            result = self.client.publish(topic_name, json_message)
            if not result:
                messagebox.showerror("Error", f"No se pudo publicar en el tópico {topic_name}.")
            else:
                messagebox.showinfo("Éxito", f"Mensaje enviado a {topic_name}.")
                self.message_entry.delete(0, tk.END) 
        except Exception as e:
            messagebox.showerror("Error", f"Error al publicar el mensaje: {e}")
        
    def refresh_subscriptions(self):
        try:
            subscriptions = self.db.get_subscriptions()
            self.subscriptions_listbox.delete(0, tk.END)
            for sub in subscriptions:
                self.subscriptions_listbox.insert(tk.END, f"{sub['id']}: {sub['topic']} ({sub['source_client_id']})")
            self.status_label.config(text=f"Se encontraron {len(subscriptions)} suscripciones")
            # NUEVO: refrescar lista de tópicos públicos
            self.refresh_public_topics()
        except Exception as e:
            messagebox.showerror("Error", f"Error al refrescar suscripciones: {str(e)}")

    def refresh_public_topics(self):
        """Obtiene los tópicos públicos directamente del broker"""
        try:
            if not self.client or not self.client.connected:
                messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
                return
                
            # Mostrar que estamos actualizando
            self.status_label.config(text="Actualizando tópicos públicos...")
            
            # Dar tiempo al sistema para actualizar la interfaz
            self.root.update_idletasks()
            
            # Obtener los tópicos publicados del broker
            topics = self.client.get_published_topics()
        
            
            # Actualizar el combobox con los nombres de los tópicos
            topic_names = []
            topic_display_names = []  # Nuevo: para mostrar nombre(propietario)
            topic_owners = {}  # Diccionario para almacenar {nombre_tópico: cliente_propietario}
            
            for topic in topics:
                # Verificar que el diccionario tenga las claves esperadas
                if "name" in topic and "owner" in topic:
                    topic_name = topic["name"]
                    owner_id = topic["owner"]
                    
                    # Crear el nombre para mostrar en formato nombre(propietario)
                    display_name = f"{topic_name}({owner_id})"
                    
                    topic_names.append(topic_name)
                    topic_display_names.append(display_name)  # Añadir nombre de visualización
                    topic_owners[topic_name] = owner_id
                    print(f"DEBUG: Procesando tópico: {topic_name} (propietario: {owner_id})")
            
            # Guardar la información de propietarios para uso posterior
            self.topic_owners = topic_owners
            
            # Actualizar el combobox con los nombres formateados
            self.public_topics_combo['values'] = topic_display_names
            
            # Seleccionar el primer tópico si hay alguno
            if topic_display_names:
                self.public_topics_combo.current(0)
                
            self.status_label.config(text=f"Se encontraron {len(topic_names)} tópicos públicos")
        except Exception as e:
            import traceback
            print(f"ERROR: {traceback.format_exc()}")
            messagebox.showerror("Error", f"Error al obtener tópicos públicos: {str(e)}")
            
    def reconnect_to_broker(self):
        """Función auxiliar para reconectar al broker después de cambios en tópicos."""
        self.status_label.config(text="Reconectando automáticamente...")
        
        # Guardar datos de conexión actuales
        host = self.host_entry.get().strip() if hasattr(self, "host_entry") else "localhost"
        try:
            port = int(self.port_entry.get().strip()) if hasattr(self, "port_entry") else 1505
        except ValueError:
            port = 1505
        
        # Desconectar
        if self.client and self.client.connected:
            try:
                self.client.disconnect()
                self.status_var.set("Desconectado")
                self.connect_btn.config(state="normal")
                self.disconnect_btn.config(state="disabled")
            except Exception as e:
                print(f"Error al desconectar: {e}")
        
        # LÍNEA NUEVA: reiniciar los callbacks de DAS
        if self.das:
            # Guardar el callback original de sensor data
            original_sensor_callback = None
            for callback in self.das.on_data_received_callbacks:
                if callback.__name__ == self.on_sensor_data.__name__:
                    original_sensor_callback = callback
                    break
                    
            # Limpiar todos los callbacks
            self.das.on_data_received_callbacks = []
            
            # Restaurar el callback original de sensor data
            if original_sensor_callback:
                self.das.add_data_callback(original_sensor_callback)
            else:
                # Si no se encontró el original, agregar uno nuevo
                self.das.add_data_callback(self.on_sensor_data)
        
        # Pequeña pausa para asegurar que la conexión se cerró correctamente
        self.root.after(500, lambda: self._complete_reconnection(host, port))
        
    def _complete_reconnection(self, host, port):
        """Completa el proceso de reconexión."""
        client_id = self.db.get_client_id()
        if not client_id:
            messagebox.showerror("Error", "ID de cliente no establecido")
            return
        
        try:
            self.client = Client(client_id, host, port)
            if self.client.connect():
                self.status_var.set(f"Conectado (ID: {client_id})")
                self.connect_btn.config(state="disabled")
                self.disconnect_btn.config(state="normal")
                self.status_label.config(text=f"Reconectado exitosamente a {host}:{port}")

                # Iniciar publicación en los tópicos marcados como publicadores
                published_topics = self.db.get_published_topics()
                for topic_info in published_topics:
                    self._setup_topic_publishing(topic_info["name"])

                # Re-suscribirse a todos los tópicos guardados
                subscriptions = self.db.get_subscriptions()
                for sub in subscriptions:
                    topic = sub["topic"]
                    source_client = sub["source_client_id"]
                    
                    # Usar el callback centralizado
                    callback = self.create_subscription_callback(topic, source_client)
                    
                    broker_topic = topic if "/" in topic else f"{source_client}/{topic}"
                    print(f"[INFO] Re-suscribiéndose a tópico del broker: {broker_topic}")
                    success = self.client.subscribe(broker_topic, callback)

                    if success:
                        print(f"[SUCCESS] Suscrito exitosamente a '{broker_topic}'")
                    else:
                        print(f"[WARN] No se pudo suscribir a '{broker_topic}'")
                        
            else:
                messagebox.showerror("Error", "No se pudo reconectar al broker")
        except Exception as e:
            messagebox.showerror("Error de reconexión", str(e))
        
    def subscribe_to_public_topic(self):
        """Suscribirse a un tópico público sin solicitar ID del cliente"""
        display_name = self.public_topics_combo.get()
        if not display_name:
            messagebox.showinfo("Información", "Selecciona un tópico público para suscribirte")
            return
        
        # Extraer el nombre real del tópico del formato nombre(propietario)
        match = re.match(r'^(.+)\((.+)\)$', display_name)
        if match:
            topic_name = match.group(1)
            client_id = match.group(2)
        else:
            # Si por alguna razón no coincide con el patrón, usar el método anterior
            topic_name = display_name
            client_id = self.topic_owners.get(topic_name, "")
        
        if not client_id:
            messagebox.showinfo("Error", "No se pudo determinar el propietario del tópico")
            return
        
        # Verificar si ya existe una suscripción para este tópico y cliente
        subscriptions = self.db.get_subscriptions()
        for sub in subscriptions:
            if sub["topic"] == topic_name and sub["source_client_id"] == client_id:
                messagebox.showinfo("Información", f"Ya estás suscrito al tópico '{topic_name}' del cliente '{client_id}'")
                return
        
        # Si estamos conectados al broker, proceder con la suscripción
        if not self.client or not self.client.connected:
            messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
            return
                
        try:
            self.db.add_subscription(topic_name, client_id)
            
            # Usar el callback centralizado
            callback = self.create_subscription_callback(topic_name, client_id)
            
            # El formato CORRECTO del tópico en el broker es client_id/topic_name
            broker_topic = f"{client_id}/{topic_name}"
            print(f"Suscribiéndose a tópico del broker: {broker_topic}")
            success = self.client.subscribe(broker_topic, callback)
            
            if success:
                messagebox.showinfo("Éxito", f"Suscrito al tópico '{topic_name}' del cliente '{client_id}'")
                self.refresh_subscriptions()
            else:
                self.db.remove_subscription(topic_name, client_id)
                messagebox.showerror("Error", "No se pudo suscribir al tópico")
        except Exception as e:
            messagebox.showerror("Error", f"Error al suscribirse: {str(e)}")
        
    def start_das(self):
        try:
            self.das = DataAcquisitionService(self.db, verbose=False)
            self.das.add_data_callback(self.on_sensor_data)  # Asegúrate de que esta línea exista
            self.das.start()
            self.status_label.config(text="DAS iniciado correctamente")
        except Exception as e:
            messagebox.showerror("Error", f"Error al iniciar DAS: {str(e)}")

    def update_data_loop(self):
        while self.running:
            if self.das:
                try:
                    stats = self.das.get_stats()
                    readings_count = stats.get('readings_received', 0)
                    self.root.after(0, lambda c=readings_count: self.readings_label.config(text=f"Lecturas: {c}"))
                except Exception:
                    pass
            time.sleep(1)

    def connect_to_broker(self):
        host = getattr(self, "host_entry", None)
        port = getattr(self, "port_entry", None)
        host = host.get().strip() if host else "localhost"
        try:
            port = int(port.get().strip()) if port else 1505
        except ValueError:
            messagebox.showerror("Error", "El puerto debe ser un número")
            return

        # Guardar host y puerto en la base de datos
        self.db.set_broker_host(host)
        self.db.set_broker_port(port)

        client_id = self.db.get_client_id()
        if not client_id:
            messagebox.showerror("Error", "ID de cliente no establecido")
            return

        self.status_label.config(text=f"Conectando a {host}:{port}...")
        try:
            if self.client and self.client.connected:
                self.client.disconnect()

            self.client = Client(client_id, host, port)
            if self.client.connect():
                self.status_var.set(f"Conectado (ID: {client_id})")
                self.connect_btn.config(state="disabled")
                self.disconnect_btn.config(state="normal")
                self.status_label.config(text=f"Conectado a {host}:{port} (ID: {client_id})")

                # Iniciar publicación en los tópicos marcados como publicadores
                published_topics = self.db.get_published_topics()
                for topic_info in published_topics:
                    self._setup_topic_publishing(topic_info["name"])

                #Re-suscribirse a todos los tópicos guardados
                subscriptions = self.db.get_subscriptions()
                for sub in subscriptions:
                    topic = sub["topic"]
                    source_client = sub["source_client_id"]

                    # Usar el callback centralizado
                    callback = self.create_subscription_callback(topic, source_client)

                    broker_topic = topic if "/" in topic else f"{source_client}/{topic}"
                    print(f"[INFO] Re-suscribiéndose a tópico del broker: {broker_topic}")
                    success = self.client.subscribe(broker_topic, callback)

                    if success:
                        print(f"[SUCCESS] Suscrito exitosamente a '{broker_topic}'")
                    else:
                        print(f"[WARN] No se pudo suscribir a '{broker_topic}'")
            else:
                messagebox.showerror("Error", "No se pudo conectar al broker")
        except Exception as e:
            messagebox.showerror("Error de conexión", str(e))
            
    def disconnect_from_broker(self):
        if self.client and self.client.connected:
            try:
                self.client.disconnect()
                self.status_var.set("Desconectado")
                self.connect_btn.config(state="normal")
                self.disconnect_btn.config(state="disabled")
                self.status_label.config(text="Desconectado del broker")
            except Exception as e:
                messagebox.showerror("Error", f"Error al desconectar: {str(e)}")
        else:
            messagebox.showinfo("Información", "No hay conexión activa")
    def change_client_id(self):
        new_id = self.client_id_var.get().strip()
        if not new_id:
            messagebox.showerror("Error", "El ID del cliente no puede estar vacío")
            return
        
        # Verificar si está conectado
        if self.client and self.client.connected:
            respuesta = messagebox.askyesno("Atención", 
                "Cambiar el ID requiere desconectarse del broker. ¿Deseas continuar?")
            if not respuesta:
                return
            # Desconectar antes de cambiar el ID
            try:
                self.client.disconnect()
                self.status_var.set("Desconectado")
                self.connect_btn.config(state="normal")
                self.disconnect_btn.config(state="disabled")
            except Exception as e:
                messagebox.showerror("Error", f"Error al desconectar: {str(e)}")
                return
        
        try:
            self.db.set_client_id(new_id)
            messagebox.showinfo("Éxito", f"ID de cliente cambiado a: {new_id}")
        except Exception as e:
            messagebox.showerror("Error", f"Error al cambiar ID: {str(e)}")

    def update_metadata(self):
        name = self.name_var.get().strip()
        email = self.email_var.get().strip()
        
        # Verificar si está conectado
        if self.client and self.client.connected:
            respuesta = messagebox.askyesno("Atención", 
                "Actualizar metadatos requiere desconectarse del broker. ¿Deseas continuar?")
            if not respuesta:
                return
            # Desconectar antes de cambiar metadatos
            try:
                self.client.disconnect()
                self.status_var.set("Desconectado")
                self.connect_btn.config(state="normal")
                self.disconnect_btn.config(state="disabled")
            except Exception as e:
                messagebox.showerror("Error", f"Error al desconectar: {str(e)}")
                return
        
        metadata = self.db.get_client_metadata()
        if name:
            metadata["name"] = name
        if email:
            metadata["email"] = email
        try:
            self.db.set_client_metadata(metadata)
            messagebox.showinfo("Éxito", "Metadatos actualizados")
        except Exception as e:
            messagebox.showerror("Error", f"Error al actualizar metadatos: {str(e)}")

    def refresh_stats(self):
        self.stats_text.config(state="normal")
        self.stats_text.delete("1.0", tk.END)
        stats_text = ""
        if self.das:
            das_stats = self.das.get_stats()
            stats_text += f"Lecturas recibidas: {das_stats['readings_received']}\n"
            stats_text += f"DAS en ejecución: {'Sí' if das_stats['running'] else 'No'}\n"
        else:
            stats_text += "DAS no iniciado\n"
        if self.client and self.client.connected:
            stats_text += f"Conectado al broker: {self.client.host}:{self.client.port}\n"
            stats_text += f"ID de cliente: {self.client.client_id}\n"
        else:
            stats_text += "No conectado al broker\n"
        try:
            sensors_count = len(self.db.get_sensors())
            topics_count = len(self.db.get_topics())
            subscriptions_count = len(self.db.get_subscriptions())
            stats_text += f"Sensores registrados: {sensors_count}\n"
            stats_text += f"Tópicos registrados: {topics_count}\n"
            stats_text += f"Suscripciones activas: {subscriptions_count}\n"
        except Exception:
            stats_text += "Error al obtener estadísticas de la base de datos\n"
        self.stats_text.insert("1.0", stats_text)
        self.stats_text.config(state="disabled")

    def refresh_sensors(self):
        try:
            sensors = self.db.get_sensors()
            self.sensors_listbox.delete(0, tk.END)
            for sensor in sensors:
                self.sensors_listbox.insert(tk.END, f"{sensor['id']}: {sensor['name']}")
            self.status_label.config(text=f"Se encontraron {len(sensors)} sensores")
        except Exception as e:
            messagebox.showerror("Error", f"Error al refrescar sensores: {str(e)}")

    def on_sensor_selected(self, event):
        selection = self.sensors_listbox.curselection()
        if not selection:
            return
        selected_index = selection[0]
        selected_item = self.sensors_listbox.get(selected_index)
        sensor_id = selected_item.split(":")[0].strip()
        
        # Si se estaba monitoreando otro sensor, limpiar el área de tiempo real
        if self.realtime_active_var.get():
            self.clear_realtime_data()
        
        try:
            sensor = self.db.get_sensor(sensor_id)
            if not sensor:
                return
            self.sensor_id_var.set(str(sensor["id"]))
            self.sensor_name_var.set(sensor["name"])
            self.sensor_value_var.set(sensor["last_value"])
            timestamp = datetime.fromtimestamp(sensor["last_updated"]).strftime("%Y-%m-%d %H:%M:%S")
            self.sensor_updated_var.set(timestamp)
            self.load_sensor_history()
            
            # Si estaba activo el monitoreo, mostrar mensaje informativo
            if self.realtime_active_var.get():
                self.realtime_text.config(state="normal")
                self.realtime_text.insert(tk.END, f"Monitoreo en tiempo real activado para sensor: {sensor['name']}\nEsperando datos...\n\n")
                self.realtime_text.config(state="disabled")
        except Exception as e:
            messagebox.showerror("Error", f"Error al cargar detalles del sensor: {str(e)}")

    def load_sensor_history(self):
        sensor_id = self.sensor_id_var.get()
        if not sensor_id:
            messagebox.showinfo("Información", "Selecciona un sensor primero")
            return
        try:
            limit = int(self.history_limit_var.get())
        except ValueError:
            limit = 20
        try:
            sensor = self.db.get_sensor(sensor_id)
            if not sensor:
                return
            readings = self.db.get_readings(sensor["name"], limit=limit)
            self.history_text.config(state="normal")
            self.history_text.delete("1.0", tk.END)
            if not readings:
                self.history_text.insert(tk.END, "No hay lecturas para este sensor.")
            else:
                self.history_text.insert(tk.END, f"Historial de últimas {len(readings)} lecturas:\n\n")
                for reading in readings:
                    timestamp = datetime.fromtimestamp(reading["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
                    self.history_text.insert(tk.END, f"{timestamp}: {reading['value']} {reading['units']}\n")
            self.history_text.config(state="disabled")
        except Exception as e:
            messagebox.showerror("Error", f"Error al cargar historial: {str(e)}")

    def refresh_topics(self):
        try:
            topics = self.db.get_topics()
            self.topics_listbox.delete(0, tk.END)
            topic_names = []
            for topic in topics:
                status = "✓" if topic["publish"] else " "
                self.topics_listbox.insert(tk.END, f"{topic['id']}: {topic['name']} [{status}]")
                topic_names.append(topic['name'])
            sensors = self.db.get_sensors()
            sensor_names = [s["name"] for s in sensors]
            self.sensor_combo['values'] = sensor_names
            self.status_label.config(text=f"Se encontraron {len(topics)} tópicos")
        except Exception as e:
            messagebox.showerror("Error", f"Error al refrescar tópicos: {str(e)}")

    def on_topic_selected(self, event):
        selection = self.topics_listbox.curselection()
        if not selection:
            return
        
        # Usar el primer tópico seleccionado para mostrar detalles
        selected_index = selection[0]
        selected_item = self.topics_listbox.get(selected_index)
        topic_id = selected_item.split(":")[0].strip()
        try:
            topic = self.db.get_topic(topic_id)
            if not topic:
                return
            self.topic_id_var.set(str(topic["id"]))
            self.topic_name_var.set(topic["name"])
            self.topic_publish_var.set("Sí" if topic["publish"] else "No")
            sensors = self.db.get_topic_sensors(topic["name"])
            self.topic_sensors_text.config(state="normal")
            self.topic_sensors_text.delete("1.0", tk.END)
            if not sensors:
                self.topic_sensors_text.insert(tk.END, "No hay sensores asociados a este tópico.")
            else:
                for sensor in sensors:
                    self.topic_sensors_text.insert(tk.END, f"- {sensor['name']}: {sensor['last_value']}\n")
            self.topic_sensors_text.config(state="disabled")
        except Exception as e:
            messagebox.showerror("Error", f"Error al cargar detalles del tópico: {str(e)}")

    def toggle_topic_publish(self, publish):
        selection = self.topics_listbox.curselection()
        if not selection:
            messagebox.showinfo("Información", "Selecciona al menos un tópico primero")
            return
        
        # Almacenar IDs de tópicos para reselección posterior
        selected_topic_ids = []
        for idx in selection:
            item = self.topics_listbox.get(idx)
            topic_id = item.split(":")[0].strip()
            selected_topic_ids.append(topic_id)
        
        success_count = 0
        
        for selected_index in selection:
            selected_item = self.topics_listbox.get(selected_index)
            topic_id = selected_item.split(":")[0].strip()
            try:
                topic = self.db.get_topic(topic_id)
                if not topic:
                    continue
                
                # Saltar silenciosamente si el tópico ya está en el estado deseado
                if topic["publish"] == publish:
                    continue
                
                # Actualizar la base de datos local
                self.db.set_topic_publish(topic["name"], publish)
                
                # NUEVO: Actualizar el estado en el broker si estamos conectados
                if self.client and self.client.connected:
                    self.client.set_topic_publish(topic["name"], publish)
                    
                success_count += 1
            except Exception as e:
                messagebox.showerror("Error", f"Error en tópico ID {topic_id}: {str(e)}")
                
        # Actualizar UI si se realizaron cambios
        if success_count > 0:
            # Actualizar el panel de detalles
            if len(selection) > 0:
                self.topic_publish_var.set("Sí" if publish else "No")
            
            # Refrescar listas
            self.refresh_topics()
            self.refresh_public_topics()
            
            # Reseleccionar tópicos después de refrescar
            for i in range(self.topics_listbox.size()):
                item = self.topics_listbox.get(i)
                topic_id = item.split(":")[0].strip()
                if topic_id in selected_topic_ids:
                    self.topics_listbox.selection_set(i)
            
            # Mostrar mensaje de éxito
            state = "activada" if publish else "desactivada"
            messagebox.showinfo("Éxito", f"Publicación {state} para {success_count} tópico(s)")
            
            # Añadir esta línea al final del bloque para reconectar automáticamente
            self.reconnect_to_broker()

    def add_sensor_to_topic(self):
        selection = self.topics_listbox.curselection()
        if not selection:
            messagebox.showinfo("Información", "Selecciona al menos un tópico")
            return
            
        sensor_name = self.sensor_to_add_var.get()
        if not sensor_name:
            messagebox.showinfo("Información", "Selecciona un sensor para agregar")
            return
        
        success_count = 0
        for selected_index in selection:
            selected_item = self.topics_listbox.get(selected_index)
            topic_id = selected_item.split(":")[0].strip()
            try:
                topic = self.db.get_topic(topic_id)
                if not topic:
                    continue
                
                # Verificar si el sensor ya está en el tópico
                sensors = self.db.get_topic_sensors(topic["name"])
                sensor_exists = False
                for sensor in sensors:
                    if sensor["name"] == sensor_name:
                        sensor_exists = True
                        break
                
                if sensor_exists:
                    continue
                
                self.db.add_sensor_to_topic(topic["name"], sensor_name)
                success_count += 1
            except Exception as e:
                messagebox.showerror("Error", f"Error al agregar sensor al tópico ID {topic_id}: {str(e)}")
        
        if success_count > 0:
            messagebox.showinfo("Éxito", f"Sensor '{sensor_name}' añadido a {success_count} tópico(s)")
            self.on_topic_selected(None)
            # Añadir la reconexión automática
            self.reconnect_to_broker()
            
    def remove_sensor_from_topic(self):
        selection = self.topics_listbox.curselection()
        if not selection:
            messagebox.showinfo("Información", "Selecciona al menos un tópico")
            return
            
        sensor_name = self.sensor_to_add_var.get()
        if not sensor_name:
            messagebox.showinfo("Información", "Selecciona un sensor para eliminar")
            return
        
        success_count = 0
        not_found_topics = []
        
        for selected_index in selection:
            selected_item = self.topics_listbox.get(selected_index)
            topic_id = selected_item.split(":")[0].strip()
            try:
                topic = self.db.get_topic(topic_id)
                if not topic:
                    continue
                
                # Verificar si el sensor está en el tópico
                sensors = self.db.get_topic_sensors(topic["name"])
                sensor_exists = False
                for sensor in sensors:
                    if sensor["name"] == sensor_name:
                        sensor_exists = True
                        break
                
                if not sensor_exists:
                    not_found_topics.append(topic["name"])
                    continue
                
                self.db.remove_sensor_from_topic(topic["name"], sensor_name)
                success_count += 1
            except Exception as e:
                messagebox.showerror("Error", f"Error al eliminar sensor del tópico ID {topic_id}: {str(e)}")
        
        message = ""
        if success_count > 0:
            message = f"Sensor '{sensor_name}' eliminado de {success_count} tópico(s). "
            
        if not_found_topics:
            message += f"Advertencia: El sensor no estaba presente en los tópicos: {', '.join(not_found_topics)}"
            
        if message:
            messagebox.showinfo("Resultado", message)
        
        self.on_topic_selected(None)
        
        # Añadir la reconexión si hubo éxito
        if success_count > 0:
            self.reconnect_to_broker()
        
    def refresh_subscriptions(self):
        try:
            subscriptions = self.db.get_subscriptions()
            self.subscriptions_listbox.delete(0, tk.END)
            for sub in subscriptions:
                self.subscriptions_listbox.insert(tk.END, f"{sub['id']}: {sub['topic']} ({sub['source_client_id']})")
            self.status_label.config(text=f"Se encontraron {len(subscriptions)} suscripciones")
        except Exception as e:
            messagebox.showerror("Error", f"Error al refrescar suscripciones: {str(e)}")

    def on_subscription_selected(self, event):
        selection = self.subscriptions_listbox.curselection()
        if not selection:
            return
        selected_index = selection[0]
        selected_item = self.subscriptions_listbox.get(selected_index)
        match = re.match(r'^\d+:\s+(.+)\s+\((.+)\)$', selected_item)
        if not match:
            return
        topic, client = match.groups()
        
        # Actualizar las variables en lugar de usar delete/insert
        self.sub_topic_var.set(topic)
        self.sub_client_var.set(client)
        self.view_sub_data()
        
    def subscribe_to_topic(self):
        if not self.client or not self.client.connected:
            messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
            return
        
        # Usar las variables en lugar de .get()
        topic = self.sub_topic_var.get().strip()
        source_client = self.sub_client_var.get().strip()
        if not topic or not source_client:
            messagebox.showinfo("Información", "Completa tópico y cliente origen")
            return
        
        # Verificar si ya existe una suscripción para este tópico y cliente
        subscriptions = self.db.get_subscriptions()
        for sub in subscriptions:
            if sub["topic"] == topic and sub["source_client_id"] == source_client:
                messagebox.showinfo("Información", f"Ya estás suscrito al tópico '{topic}' del cliente '{source_client}'")
                return
                
        try:
            self.db.add_subscription(topic, source_client)
            
            # Usar el callback centralizado
            callback = self.create_subscription_callback(topic, source_client)
            
            broker_topic = topic if "/" in topic else f"{source_client}/{topic}"
            print(f"Suscribiéndose a tópico del broker: {broker_topic}")
            success = self.client.subscribe(broker_topic, callback)
            if success:
                messagebox.showinfo("Éxito", f"Suscrito al tópico '{topic}' del cliente '{source_client}'")
                self.refresh_subscriptions()
            else:
                self.db.remove_subscription(topic, source_client)
                messagebox.showerror("Error", "No se pudo suscribir al tópico")
        except Exception as e:
            messagebox.showerror("Error", f"Error al suscribirse: {str(e)}")

    def unsubscribe_from_topic(self):
        selection = self.subscriptions_listbox.curselection()
        if not selection:
            messagebox.showinfo("Información", "Selecciona una suscripción primero")
            return
        selected_index = selection[0]
        selected_item = self.subscriptions_listbox.get(selected_index)
        match = re.match(r'^\d+:\s+(.+)\s+\((.+)\)$', selected_item)
        if not match:
            return
        topic, client = match.groups()
        try:
            broker_topic = topic if "/" in topic else f"{client}/{topic}"
            if self.client and self.client.connected:
                self.client.unsubscribe(f"{broker_topic}")
            self.db.remove_subscription(topic, client)
            messagebox.showinfo("Éxito", f"Cancelada suscripción al tópico '{topic}' del cliente '{client}'")
            self.refresh_subscriptions()
        except Exception as e:
            messagebox.showerror("Error", f"Error al cancelar suscripción: {str(e)}")

    def on_sensor_data(self, sensor_name, data):
        """Callback cuando se recibe un nuevo dato de sensor."""
        # Actualizar el monitoreo en tiempo real si está activo
        current_sensor_name = self.sensor_name_var.get()
        if self.realtime_active_var.get() and sensor_name == current_sensor_name:
            timestamp = datetime.fromtimestamp(data["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
            value_text = f"{data['value']} {data.get('units', '')}"
            
            # Actualizar la interfaz de usuario en el hilo principal
            self.root.after(0, lambda: self.update_realtime_display(timestamp, value_text))
        
        # También actualizar últimos valores si es el sensor actual
        if sensor_name == current_sensor_name:
            self.root.after(0, lambda: self.update_sensor_latest_value(data))

    def add_realtime_message(self, source, content):
        """Muestra mensajes recibidos en las suscripciones en tiempo real."""
        print(f"DEBUG: add_realtime_message recibió: {source}, {content}")
        
        # Verificar si hay una suscripción seleccionada que coincida con el origen
        topic = self.sub_topic_var.get()
        client = self.sub_client_var.get()
        
        # Extraer información del contenido
        message_start = content.find("\nMensaje: ")
        if message_start > 0:
            topic_info = content[:message_start]  # Extraer información del tópico
            message_text = content[message_start + 10:]  # +10 para saltar "\nMensaje: "
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            
            print(f"DEBUG: Mensaje para mostrar: [{timestamp}] {message_text}")
            
            # Mostrar todos los mensajes recibidos, sin importar el tópico seleccionado
            if source == "Recibido":
                if not topic or topic_info.find(topic) >= 0:
                    # Se corrigió el corchete faltante en la timestamp
                    self.root.after(0, lambda: self.append_to_sub_data(f"[{timestamp}] {client}/{topic}  {message_text}\n"))
        else:
            print(f"DEBUG: Formato incorrecto en contenido: {content}")

    def append_to_sub_data(self, text):
        """Añade texto al área de datos de suscripción."""
        try:
            print(f"DEBUG: Intentando añadir texto a sub_data_text: {text[:50]}...")
            self.sub_data_text.config(state="normal")
            self.sub_data_text.insert(tk.END, text)
            self.sub_data_text.see(tk.END)  # Auto-scroll al final
            self.sub_data_text.config(state="disabled")
            print("DEBUG: Texto añadido correctamente")
        except Exception as e:
            print(f"ERROR: No se pudo añadir texto a sub_data_text: {e}")
            import traceback
            traceback.print_exc()
    
    def view_sub_data(self):
        # Usar las variables en lugar de .get()
        topic = self.sub_topic_var.get()
        client = self.sub_client_var.get()
        if not topic or not client:
            messagebox.showinfo("Información", "Selecciona una suscripción primero")
            return
        try:
            data = self.db.get_subscription_data(topic, client, limit=20)
            self.sub_data_text.config(state="normal")
            self.sub_data_text.delete("1.0", tk.END)
            if not data:
                self.sub_data_text.insert(tk.END, f"No hay datos para el tópico '{topic}' del cliente '{client}'")
            else:
                self.sub_data_text.insert(tk.END, f"Datos para el tópico '{topic}' del cliente '{client}':\n\n")
                for item in data:
                    timestamp = datetime.fromtimestamp(item["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
                    self.sub_data_text.insert(tk.END, f"[{timestamp}] {client}/{topic} - {item['data']}\n\n")
                self.sub_data_text.config(state="disabled")
        except Exception as e:
            messagebox.showerror("Error", f"Error al cargar datos: {str(e)}")

    def clear_sub_data(self):
        self.sub_data_text.config(state="normal")
        self.sub_data_text.delete("1.0", tk.END)
        self.sub_data_text.config(state="disabled")

    def _setup_topic_publishing(self, topic_name: str) -> None:
        """
        Setup publishing for a topic.
        
        Args:
            topic_name: Name of topic to publish
        """
        if not self.das or not self.client or not self.client.connected:
            return
        
        sensors = self.db.get_topic_sensors(topic_name)
        if not sensors:
            return
            
        sensor_names = [s["name"] for s in sensors]
        
        def publish_callback(sensor_name: str, data: Dict[str, Any]) -> None:
            current_topic_info = self.db.get_topic(topic_name)
            if not current_topic_info or not current_topic_info["publish"]:
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
                        messagebox.showinfo("Error", f'Error al publicar en el topico {topic_name}')
                except Exception as e:
                    messagebox.showinfo("Error", f'Error: {e}')
        
        self.das.add_data_callback(publish_callback)

    def create_subscription_callback(self, topic, source_client):
        """Crea un callback configurado para una suscripción específica.
        
        Args:
            topic: Nombre del tópico
            source_client: ID del cliente origen
            
        Returns:
            Función callback configurada para esta suscripción
        """
        def callback(topic_str, message):
            try:
                print(f"\n👉 RECIBIDO mensaje en tópico: '{topic_str}'")
                message_str = message.decode('utf-8') if isinstance(message, bytes) else str(message)
                timestamp = int(time.time())
                print(f"👉 CONTENIDO: '{message_str}'")
                
                # Normalizar el formato de tópico
                if topic_str.startswith('['):
                    try:
                        import json
                        topic_str = json.loads(topic_str)[0]
                        print(f"👉 Tópico JSON decodificado: {topic_str}")
                    except Exception as e:
                        print(f"ERROR decodificando JSON: {e}")
                
                # Separar client_id/topic
                parts = topic_str.split('/', 1)
                if len(parts) == 2:
                    actual_client_id = parts[0]
                    actual_topic_name = parts[1]
                else:
                    actual_client_id = source_client
                    actual_topic_name = topic
                
                print(f"👉 Identificado como: Cliente='{actual_client_id}', Tópico='{actual_topic_name}'")
                
                # Guardar en BD
                self.db.add_subscription_data(topic, source_client, timestamp, message_str)
                
                # SIEMPRE mostrar el mensaje en tiempo real
                time_fmt = datetime.fromtimestamp(timestamp).strftime("%Y-%m-%d %H:%M:%S")
                msg_text = f"[{time_fmt}] {actual_client_id}/{actual_topic_name} - {message_str}\n\n"
                self.root.after(0, lambda text=msg_text: self.append_to_sub_data(text))
                
            except Exception as e:
                print(f"⚠️ ERROR EN CALLBACK: {e}")
                import traceback
                traceback.print_exc()
        
        return callback

    def create_admin_tab(self):
        """Crea la pestaña de administración de tópicos."""
        tab = ttk.Frame(self.notebook)
        self.notebook.add(tab, text="Administración")

        # Marco principal con Notebook para diferentes vistas
        admin_notebook = ttk.Notebook(tab)
        admin_notebook.pack(fill="both", expand=True, padx=10, pady=10)
        
        # Panel 1: Solicitudes recibidas (como dueño)
        received_tab = ttk.Frame(admin_notebook)
        admin_notebook.add(received_tab, text="Solicitudes recibidas")
        
        # Lista de solicitudes pendientes
        received_frame = ttk.LabelFrame(received_tab, text="Solicitudes pendientes")
        received_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        self.requests_listbox = tk.Listbox(received_frame, width=50, height=6)
        self.requests_listbox.pack(fill="both", expand=True, padx=5, pady=5)
        
        # Botones de acción
        btn_frame = ttk.Frame(received_frame)
        btn_frame.pack(fill="x", padx=5, pady=5)
        ttk.Button(btn_frame, text="Aceptar", command=self.approve_admin_request).pack(side="left", padx=5)
        ttk.Button(btn_frame, text="Rechazar", command=self.reject_admin_request).pack(side="left", padx=5)
        ttk.Button(btn_frame, text="Refrescar", command=self.refresh_admin_requests).pack(side="left", padx=5)
        
        # Panel 2: Tópicos administrados (como administrador)
        admin_tab = ttk.Frame(admin_notebook)
        admin_notebook.add(admin_tab, text="Tópicos administrados")
        
        # Lista de tópicos donde el usuario es administrador
        admin_topics_frame = ttk.LabelFrame(admin_tab, text="Tópicos donde eres administrador")
        admin_topics_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        self.admin_topics_listbox = tk.Listbox(admin_topics_frame, width=50, height=6)
        self.admin_topics_listbox.pack(fill="both", expand=True, padx=5, pady=5)
        self.admin_topics_listbox.bind('<<ListboxSelect>>', self.on_admin_topic_selected)
        
        # Panel para mostrar y configurar sensores
        sensor_config_frame = ttk.LabelFrame(admin_tab, text="Configuración de sensores")
        sensor_config_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        self.admin_sensors_tree = ttk.Treeview(sensor_config_frame, columns=("sensor", "status"), show="headings")
        self.admin_sensors_tree.heading("sensor", text="Sensor")
        self.admin_sensors_tree.heading("status", text="Estado")
        self.admin_sensors_tree.pack(fill="both", expand=True, padx=5, pady=5)
        
        sensor_btns = ttk.Frame(sensor_config_frame)
        sensor_btns.pack(fill="x", padx=5, pady=5)
        ttk.Button(sensor_btns, text="Activar", command=lambda: self.set_admin_sensor_status(True)).pack(side="left", padx=5)
        ttk.Button(sensor_btns, text="Desactivar", command=lambda: self.set_admin_sensor_status(False)).pack(side="left", padx=5)
        
        # Panel 3: Solicitar ser administrador
        request_tab = ttk.Frame(admin_notebook)
        admin_notebook.add(request_tab, text="Solicitar administración")
        
        # Panel para mostrar tópicos suscritos que no sean propios
        subscribable_frame = ttk.LabelFrame(request_tab, text="Tópicos a los que estás suscrito")
        subscribable_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        self.admin_subscribable_topics_listbox = tk.Listbox(subscribable_frame, width=50, height=6)
        self.admin_subscribable_topics_listbox.pack(fill="both", expand=True, padx=5, pady=5)
        
        # Botones para refrescar y solicitar administración
        sub_btn_frame = ttk.Frame(subscribable_frame)
        sub_btn_frame.pack(fill="x", padx=5, pady=5)
        ttk.Button(sub_btn_frame, text="Refrescar", command=self.refresh_subscribable_topics).pack(side="left", padx=5)
        ttk.Button(sub_btn_frame, text="Solicitar administración", 
                command=self.request_admin_for_selected).pack(side="left", padx=5)
        
        # Panel para solicitud manual
        request_frame = ttk.LabelFrame(request_tab, text="Solicitud manual")
        request_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        form_frame = ttk.Frame(request_frame)
        form_frame.pack(fill="x", padx=10, pady=10)
        
        ttk.Label(form_frame, text="Tópico:").grid(row=0, column=0, sticky="w", padx=5, pady=5)
        self.req_topic_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.req_topic_var, width=30).grid(row=0, column=1, sticky="w", padx=5, pady=5)
        
        ttk.Label(form_frame, text="ID del dueño:").grid(row=1, column=0, sticky="w", padx=5, pady=5)
        self.req_owner_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.req_owner_var, width=30).grid(row=1, column=1, sticky="w", padx=5, pady=5)
        
        ttk.Button(form_frame, text="Enviar solicitud", 
                command=self.send_admin_request).grid(row=2, column=0, columnspan=2, pady=10)
        
        # Actualizar las listas
        self.refresh_subscribable_topics()
        self.refresh_admin_requests()

    def refresh_admin_requests(self):
        """Refresca la lista de solicitudes de administración pendientes."""
        if not self.client or not self.client.connected:
            return
        
        try:
            # Obtener solicitudes del servidor
            requests = self.client.get_admin_requests()
            
            self.requests_listbox.delete(0, tk.END)
            if not requests:
                self.requests_listbox.insert(tk.END, "No hay solicitudes pendientes")
            else:
                for req in requests:
                    self.requests_listbox.insert(tk.END, 
                                            f"{req['id']}: {req['requester_id']} solicita {req['topic']}")
        except Exception as e:
            self.requests_listbox.delete(0, tk.END)
            self.requests_listbox.insert(tk.END, "Error al obtener solicitudes")
            print(f"ERROR: {e}")

    def send_admin_request(self):
        """Envía una solicitud para ser administrador de un tópico."""
        topic = self.req_topic_var.get().strip()
        owner = self.req_owner_var.get().strip()
        
        if not topic or not owner:
            messagebox.showinfo("Error", "Debe especificar tópico y dueño")
            return
                
        if not self.client or not self.client.connected:
            messagebox.showinfo("Error", "No está conectado al broker")
            return
        
        # Verificar que no soy el dueño
        my_client_id = self.db.get_client_id()
        if owner == my_client_id:
            messagebox.showinfo("Información", "No puedes solicitar administrar tu propio tópico")
            return
                
        success = self.client.request_admin_status(topic, owner)
        if success:
            messagebox.showinfo("Éxito", f"Solicitud enviada al dueño {owner}")
        else:
            messagebox.showerror("Error", "No se pudo enviar la solicitud")

    def setup_admin_notifications(self):
        """Configura las notificaciones para administración."""
        if self.client and self.client.connected:
            self.client.register_admin_notification_handler(self.on_admin_notification)

    def on_admin_notification(self, notification):
        """Maneja una notificación administrativa recibida."""
        notification_type = notification.get("type")
        
        if notification_type == "request":
            # Nueva solicitud de administrador recibida
            requester_id = notification.get("requester_id", "desconocido")
            topic_name = notification.get("topic_name", "desconocido")
            
            # Mostrar notificación visual
            self.show_admin_notification(
                f"Nueva solicitud de administración recibida",
                f"{requester_id} solicita administrar tu tópico '{topic_name}'"
            )
            
            # Actualizar contador en la pestaña Admin (badge)
            self._update_admin_tab_badge()
            
            # Si estamos en la pestaña de administración, refrescar la lista
            current_tab = self.notebook.index("current")
            if self.notebook.tab(current_tab, "text").startswith("Administración"):
                self.refresh_admin_requests()

    def show_admin_notification(self, title, message):
        """Muestra una ventana de notificación flotante."""
        # Crear ventana flotante
        popup = tk.Toplevel(self.root)
        popup.title("Notificación")
        popup.geometry("300x150+50+50")
        popup.attributes("-topmost", True)
        
        # Configurar ventana como modal
        popup.transient(self.root)
        popup.grab_set()
        
        # Contenido
        ttk.Label(popup, text=title, font=("Helvetica", 12, "bold")).pack(pady=(15,5), padx=10)
        ttk.Label(popup, text=message, wraplength=280).pack(pady=10, padx=10)
        ttk.Button(popup, text="Ver ahora", command=lambda: self._view_admin_requests(popup)).pack(pady=5)
        ttk.Button(popup, text="Más tarde", command=popup.destroy).pack(pady=5)
        
        # Reproducir sonido (opcional)
        try:
            import winsound
            winsound.MessageBeep()
        except:
            pass

    def _view_admin_requests(self, popup=None):
        """Muestra la pestaña de solicitudes administrativas."""
        # Cerrar notificación si existe
        if popup:
            popup.destroy()
        
        # Cambiar a la pestaña de administración
        for i in range(self.notebook.index("end")):
            if self.notebook.tab(i, "text").startswith("Administración"):
                self.notebook.select(i)
                break
        
        # Refrescar las solicitudes
        self.refresh_admin_requests()
        
    def _update_admin_tab_badge(self):
        """Actualiza el contador de notificaciones en la pestaña de admin."""
        # Obtener cantidad de solicitudes pendientes
        count = 0
        if self.client and self.client.connected:
            try:
                requests = self.client.get_admin_requests()
                count = len(requests)
            except:
                pass
        
        # Actualizar nombre de la pestaña
        for i in range(self.notebook.index("end")):
            tab_text = self.notebook.tab(i, "text") 
            if tab_text.startswith("Administración"):
                if count > 0:
                    self.notebook.tab(i, text=f"Administración ({count})")
                else:
                    self.notebook.tab(i, text="Administración")
                break

    def approve_admin_request(self):
        """Aprueba la solicitud de administrador seleccionada."""
        if not self.client or not self.client.connected:
            messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
            return
            
        selection = self.requests_listbox.curselection()
        if not selection:
            messagebox.showinfo("Selección requerida", "Selecciona una solicitud primero")
            return
        
        selected_item = self.requests_listbox.get(selection[0])
        match = re.match(r'^(\d+):\s+(\S+)\s+solicita\s+(.+)$', selected_item)
        if not match:
            messagebox.showerror("Error", "Formato de solicitud inválido")
            return
            
        request_id = int(match.group(1))
        requester_id = match.group(2) 
        topic_name = match.group(3)
        
        confirm = messagebox.askyesno(
            "Confirmar",
            f"¿Realmente deseas aprobar a {requester_id} como administrador de '{topic_name}'?"
        )
        
        if confirm:
            success = self.client.respond_to_admin_request(request_id, topic_name, requester_id, True)
            if success:
                messagebox.showinfo("Éxito", f"Se ha aprobado a {requester_id} como administrador")
                self.refresh_admin_requests()
                self._update_admin_tab_badge()
            else:
                messagebox.showerror("Error", "No se pudo aprobar la solicitud")

    def reject_admin_request(self):
        """Rechaza la solicitud de administrador seleccionada."""
        # Similar a approve_admin_request pero con approve=False
        
    def on_admin_topic_selected(self, event):
        """Maneja la selección de un tópico administrado."""
        selection = self.admin_topics_listbox.curselection()
        if not selection:
            return
            
        # Similar a on_topic_selected pero para tópicos administrados
        # Llena el TreeView de sensores con sus estados

    def set_admin_sensor_status(self, active):
        """Activa o desactiva un sensor como administrador."""
        selection = self.admin_sensors_tree.selection()
        if not selection:
            messagebox.showinfo("Selección requerida", "Selecciona un sensor primero")
            return
        
        # Obtener sensor seleccionado
        item = selection[0]
        sensor_name = self.admin_sensors_tree.item(item, "values")[0]
        
        # Obtener tópico
        topic_selection = self.admin_topics_listbox.curselection()
        if not topic_selection:
            messagebox.showinfo("Selección requerida", "Selecciona un tópico primero")
            return
        
        topic_item = self.admin_topics_listbox.get(topic_selection[0])
        # Extraer nombre del tópico y dueño
        
        # Enviar configuración
        if self.client and self.client.connected:
            success = self.client.set_sensor_status(topic_name, sensor_name, active)
            if success:
                # Actualizar vista
                status = "Activo" if active else "Inactivo"
                self.admin_sensors_tree.item(item, values=(sensor_name, status))
                messagebox.showinfo("Éxito", f"Sensor {sensor_name} ahora está {status.lower()}")
            else:
                messagebox.showerror("Error", "No se pudo cambiar el estado del sensor")

    def request_topic_admin(self):
        """Solicita ser administrador del tópico seleccionado."""
        selection = self.topics_listbox.curselection()
        if not selection:
            messagebox.showinfo("Información", "Selecciona un tópico primero")
            return
        
        selected_index = selection[0]
        selected_item = self.topics_listbox.get(selected_index)
        topic_id = selected_item.split(":")[0].strip()
        
        try:
            topic = self.db.get_topic(topic_id)
            if not topic:
                messagebox.showinfo("Error", "No se pudo obtener información del tópico")
                return
                
            topic_name = topic["name"]
            owner_id = topic["owner_client_id"]
            
            # Verificar que no soy el dueño
            my_client_id = self.db.get_client_id()
            if owner_id == my_client_id:
                messagebox.showinfo("Información", "No puedes solicitar administrar tu propio tópico")
                return
            
            # Confirmar solicitud
            confirm = messagebox.askyesno(
                "Confirmar solicitud",
                f"¿Deseas solicitar ser administrador de '{topic_name}' (dueño: {owner_id})?"
            )
            
            if not confirm:
                return
            
            # Enviar solicitud
            if self.client and self.client.connected:
                success = self.client.request_admin_status(topic_name, owner_id)
                if success:
                    messagebox.showinfo("Éxito", f"Solicitud enviada al dueño {owner_id}")
                else:
                    messagebox.showerror("Error", "No se pudo enviar la solicitud")
            else:
                messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
                
        except Exception as e:
            messagebox.showerror("Error", f"Error al solicitar administración: {str(e)}")
            
    def refresh_subscribable_topics(self):
        """Actualiza la lista de tópicos disponibles para solicitar administración"""
        try:
            # Limpiar la lista primero
            self.admin_subscribable_topics_listbox.delete(0, tk.END)
            
            # Obtener las suscripciones del usuario
            subscriptions = self.db.get_subscriptions()
            
            # Mostrar mensaje si no hay suscripciones
            if not subscriptions:
                self.admin_subscribable_topics_listbox.insert(tk.END, "No hay suscripciones activas")
                return
                    
            # Obtener mi ID de cliente
            my_client_id = self.db.get_client_id()
            if not my_client_id:
                self.admin_subscribable_topics_listbox.insert(tk.END, "Error: ID de cliente no configurado")
                return
            
            # Debug para verificar valores
            print(f"ID de cliente: {my_client_id}")
            print(f"Suscripciones encontradas: {len(subscriptions)}")
            for sub in subscriptions:
                print(f"- Suscripción: {sub}")
                
            # Para cada suscripción, verificar si el usuario es dueño del tópico
            found_topics = False
            for sub in subscriptions:
                topic = sub.get('topic')
                owner_id = sub.get('source_client_id')
                
                if not topic or not owner_id:
                    continue
                    
                # Añadir todos los tópicos a los que estamos suscritos
                # - No necesitamos filtrar por dueño ya que eso se verificará al solicitar
                self.admin_subscribable_topics_listbox.insert(tk.END, f"{topic} ({owner_id})")
                found_topics = True
                        
            if not found_topics:
                self.admin_subscribable_topics_listbox.insert(tk.END, "No hay tópicos disponibles para solicitar administración")
                    
        except Exception as e:
            self.admin_subscribable_topics_listbox.delete(0, tk.END)
            self.admin_subscribable_topics_listbox.insert(tk.END, f"Error: {str(e)}")
            print(f"Error al actualizar tópicos disponibles para administración: {e}")
            import traceback
            traceback.print_exc()
        
    def request_admin_for_selected(self):
        """Solicita administración para el tópico seleccionado en la lista"""
        selection = self.admin_subscribable_topics_listbox.curselection()
        if not selection:
            messagebox.showinfo("Selección requerida", "Selecciona un tópico primero")
            return
        
        selected_item = self.admin_subscribable_topics_listbox.get(selection[0])
        # Formato esperado: "topic (owner_id)"
        match = re.match(r'^(.+)\s+\((.+)\)$', selected_item)
        if not match:
            messagebox.showerror("Error", "Formato de tópico inválido")
            return
            
        topic_name = match.group(1)
        owner_id = match.group(2)
        
        # Verificar que no soy el dueño
        my_client_id = self.db.get_client_id()
        if owner_id == my_client_id:
            messagebox.showinfo("Información", "No puedes solicitar administrar tu propio tópico")
            return
            
        if not self.client or not self.client.connected:
            messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
            return
            
        confirm = messagebox.askyesno(
            "Confirmar solicitud",
            f"¿Deseas solicitar ser administrador de '{topic_name}' (dueño: {owner_id})?"
        )
        
        if confirm:
            try:
                success = self.client.request_admin_status(topic_name, owner_id)
                if success:
                    messagebox.showinfo("Éxito", f"Solicitud enviada al dueño {owner_id}")
                else:
                    messagebox.showerror("Error", "No se pudo enviar la solicitud")
            except Exception as e:
                messagebox.showerror("Error", f"Error al enviar solicitud: {str(e)}")

    def approve_admin_request(self):
        """Aprueba la solicitud de administrador seleccionada."""
        if not self.client or not self.client.connected:
            messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
            return
                
        selection = self.requests_listbox.curselection()
        if not selection:
            messagebox.showinfo("Selección requerida", "Selecciona una solicitud primero")
            return
            
        selected_item = self.requests_listbox.get(selection[0])
        match = re.match(r'^(\d+):\s+(\S+)\s+solicita\s+(.+)$', selected_item)
        if not match:
            messagebox.showerror("Error", "Formato de solicitud inválido")
            return
                
        request_id = int(match.group(1))
        requester_id = match.group(2) 
        topic_name = match.group(3)
            
        confirm = messagebox.askyesno(
            "Confirmar",
            f"¿Realmente deseas aprobar a {requester_id} como administrador de '{topic_name}'?"
        )
            
        if confirm:
            try:
                success = self.client.respond_to_admin_request(request_id, topic_name, requester_id, True)
                if success:
                    messagebox.showinfo("Éxito", f"Se ha aprobado a {requester_id} como administrador")
                    self.refresh_admin_requests()
                    self._update_admin_tab_badge()
                else:
                    messagebox.showerror("Error", "No se pudo aprobar la solicitud")
            except Exception as e:
                messagebox.showerror("Error", f"Error al aprobar solicitud: {str(e)}")

    def reject_admin_request(self):
        """Rechaza la solicitud de administrador seleccionada."""
        if not self.client or not self.client.connected:
            messagebox.showwarning("No conectado", "Debes conectarte al broker primero")
            return
                
        selection = self.requests_listbox.curselection()
        if not selection:
            messagebox.showinfo("Selección requerida", "Selecciona una solicitud primero")
            return
            
        selected_item = self.requests_listbox.get(selection[0])
        match = re.match(r'^(\d+):\s+(\S+)\s+solicita\s+(.+)$', selected_item)
        if not match:
            messagebox.showerror("Error", "Formato de solicitud inválido")
            return
                
        request_id = int(match.group(1))
        requester_id = match.group(2) 
        topic_name = match.group(3)
            
        confirm = messagebox.askyesno(
            "Confirmar",
            f"¿Realmente deseas rechazar la solicitud de {requester_id} para administrar '{topic_name}'?"
        )
            
        if confirm:
            try:
                success = self.client.respond_to_admin_request(request_id, topic_name, requester_id, False)
                if success:
                    messagebox.showinfo("Éxito", f"Se ha rechazado la solicitud de {requester_id}")
                    self.refresh_admin_requests()
                    self._update_admin_tab_badge()
                else:
                    messagebox.showerror("Error", "No se pudo rechazar la solicitud")
            except Exception as e:
                messagebox.showerror("Error", f"Error al rechazar solicitud: {str(e)}")

def main():
    root = tk.Tk()
    app = TinyMQGUI(root)
    def on_closing():
        app.running = False
        if app.das:
            app.das.stop()
        if app.client and app.client.connected:
            app.client.disconnect()
        root.destroy()
    root.protocol("WM_DELETE_WINDOW", on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()