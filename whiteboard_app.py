import argparse
import json
import socket
import threading
import tkinter as tk
from dataclasses import dataclass, field
from tkinter import colorchooser, messagebox
from typing import Dict, List, Tuple

DEFAULT_HOST = '127.0.0.1'
DEFAULT_PORT = 5050


@dataclass
class WhiteboardState:
    strokes: List[dict] = field(default_factory=list)


class WhiteboardServer:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.state = WhiteboardState()
        self.clients: Dict[socket.socket, Tuple[str, int]] = {}
        self.lock = threading.Lock()
        self._server_socket = None
        self._running = False

    def start(self) -> None:
        if self._running:
            return

        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_socket.bind((self.host, self.port))
        self._server_socket.listen()
        self._running = True

        print(f"Whiteboard server listening on {self.host}:{self.port}")

        while self._running:
            try:
                client_socket, address = self._server_socket.accept()
            except OSError:
                break

            with self.lock:
                self.clients[client_socket] = address

            threading.Thread(target=self._handle_client, args=(client_socket,), daemon=True).start()

    def _handle_client(self, client_socket: socket.socket) -> None:
        file_obj = client_socket.makefile('rwb')

        try:
            self._send(client_socket, {'type': 'sync', 'strokes': self.state.strokes})

            while True:
                raw_line = file_obj.readline()
                if not raw_line:
                    break

                try:
                    message = json.loads(raw_line.decode('utf-8'))
                except json.JSONDecodeError:
                    continue

                message_type = message.get('type')
                if message_type == 'draw' and isinstance(message.get('stroke', {}).get('points'), list):
                    with self.lock:
                        self.state.strokes.append(message['stroke'])
                    self._broadcast({'type': 'draw', 'stroke': message['stroke']})
                elif message_type == 'clear':
                    with self.lock:
                        self.state.strokes.clear()
                    self._broadcast({'type': 'clear'})
        finally:
            with self.lock:
                self.clients.pop(client_socket, None)
            file_obj.close()
            client_socket.close()

    def _send(self, client_socket: socket.socket, payload: dict) -> None:
        try:
            client_socket.sendall((json.dumps(payload) + '\n').encode('utf-8'))
        except OSError:
            pass

    def _broadcast(self, payload: dict) -> None:
        with self.lock:
            sockets = list(self.clients.keys())

        for client_socket in sockets:
            self._send(client_socket, payload)


class WhiteboardClientApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title('Shared Whiteboard (Remote Client)')
        self.root.geometry('1200x760')

        self.socket = None
        self.reader = None
        self.connected = False

        self.current_color = '#111111'
        self.current_size = 3
        self.current_points: List[Tuple[int, int]] = []

        self._build_ui()

    def _build_ui(self) -> None:
        toolbar = tk.Frame(self.root, padx=8, pady=8)
        toolbar.pack(fill='x')

        tk.Label(toolbar, text='Server Host:').pack(side='left')
        self.host_entry = tk.Entry(toolbar, width=18)
        self.host_entry.insert(0, DEFAULT_HOST)
        self.host_entry.pack(side='left', padx=(4, 8))

        tk.Label(toolbar, text='Port:').pack(side='left')
        self.port_entry = tk.Entry(toolbar, width=8)
        self.port_entry.insert(0, str(DEFAULT_PORT))
        self.port_entry.pack(side='left', padx=(4, 8))

        self.connect_btn = tk.Button(toolbar, text='Connect', command=self.connect)
        self.connect_btn.pack(side='left', padx=(0, 8))

        tk.Button(toolbar, text='Pick Color', command=self.pick_color).pack(side='left', padx=(0, 8))

        self.size_scale = tk.Scale(toolbar, from_=1, to=20, orient='horizontal', label='Brush Size', command=self._set_size)
        self.size_scale.set(self.current_size)
        self.size_scale.pack(side='left', padx=(0, 8))

        tk.Button(toolbar, text='Clear Board', command=self.clear_board).pack(side='left', padx=(0, 8))

        self.status_label = tk.Label(toolbar, text='Disconnected', fg='#aa3333')
        self.status_label.pack(side='right')

        self.canvas = tk.Canvas(self.root, bg='white', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, padx=8, pady=(0, 8))

        self.canvas.bind('<ButtonPress-1>', self.on_press)
        self.canvas.bind('<B1-Motion>', self.on_drag)
        self.canvas.bind('<ButtonRelease-1>', self.on_release)

    def _set_size(self, value: str) -> None:
        self.current_size = int(float(value))

    def pick_color(self) -> None:
        color = colorchooser.askcolor(color=self.current_color)[1]
        if color:
            self.current_color = color

    def _get_host_port(self) -> Tuple[str, int]:
        host = self.host_entry.get().strip() or DEFAULT_HOST
        port = int(self.port_entry.get().strip())
        return host, port

    def connect(self) -> None:
        if self.connected:
            return

        try:
            host, port = self._get_host_port()
        except ValueError:
            messagebox.showerror('Invalid Port', 'Port must be a number.')
            return

        try:
            self.socket = socket.create_connection((host, port), timeout=5)
            self.socket.settimeout(None)
            self.reader = self.socket.makefile('rb')
            self.connected = True
            self.status_label.config(text=f'Connected to {host}:{port}', fg='#227722')
            self.connect_btn.config(state='disabled')
            threading.Thread(target=self.listen_loop, daemon=True).start()
        except OSError as error:
            messagebox.showerror('Connection Error', str(error))

    def listen_loop(self) -> None:
        try:
            while True:
                raw_line = self.reader.readline()
                if not raw_line:
                    break

                try:
                    message = json.loads(raw_line.decode('utf-8'))
                except json.JSONDecodeError:
                    continue

                self.root.after(0, self.handle_message, message)
        finally:
            self.root.after(0, self.disconnect_ui)

    def disconnect_ui(self) -> None:
        self.connected = False
        self.status_label.config(text='Disconnected', fg='#aa3333')
        self.connect_btn.config(state='normal')

    def handle_message(self, message: dict) -> None:
        message_type = message.get('type')
        if message_type == 'sync':
            self.canvas.delete('all')
            for stroke in message.get('strokes', []):
                self.draw_stroke(stroke)
        elif message_type == 'draw':
            self.draw_stroke(message.get('stroke', {}))
        elif message_type == 'clear':
            self.canvas.delete('all')

    def draw_stroke(self, stroke: dict) -> None:
        points = stroke.get('points', [])
        if len(points) < 2:
            return

        flattened = []
        for point in points:
            flattened.extend([point['x'], point['y']])

        self.canvas.create_line(
            *flattened,
            fill=stroke.get('color', '#111111'),
            width=stroke.get('size', 3),
            capstyle=tk.ROUND,
            joinstyle=tk.ROUND,
            smooth=True,
        )

    def send(self, payload: dict) -> None:
        if not self.connected or not self.socket:
            return

        try:
            self.socket.sendall((json.dumps(payload) + '\n').encode('utf-8'))
        except OSError:
            self.disconnect_ui()

    def clear_board(self) -> None:
        self.send({'type': 'clear'})

    def on_press(self, event: tk.Event) -> None:
        self.current_points = [(event.x, event.y)]

    def on_drag(self, event: tk.Event) -> None:
        if not self.current_points:
            return

        self.current_points.append((event.x, event.y))
        if len(self.current_points) >= 2:
            p1 = self.current_points[-2]
            p2 = self.current_points[-1]
            self.canvas.create_line(
                p1[0], p1[1], p2[0], p2[1],
                fill=self.current_color,
                width=self.current_size,
                capstyle=tk.ROUND,
                joinstyle=tk.ROUND,
                smooth=True,
            )

    def on_release(self, _event: tk.Event) -> None:
        if len(self.current_points) < 2:
            self.current_points = []
            return

        stroke = {
            'color': self.current_color,
            'size': self.current_size,
            'points': [{'x': x, 'y': y} for x, y in self.current_points],
        }
        self.send({'type': 'draw', 'stroke': stroke})
        self.current_points = []


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description='Shared Whiteboard one-file app')
    parser.add_argument('--server', action='store_true', help='Run in dedicated server mode (non-GUI)')
    parser.add_argument('--host', default=DEFAULT_HOST, help='Host/IP to bind server mode or default connect host')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help='TCP port for server/client')
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if args.server:
        WhiteboardServer(args.host, args.port).start()
        return

    global DEFAULT_HOST, DEFAULT_PORT
    DEFAULT_HOST = args.host
    DEFAULT_PORT = args.port

    root = tk.Tk()
    WhiteboardClientApp(root)
    root.mainloop()


if __name__ == '__main__':
    main()
