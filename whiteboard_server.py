import json
import socket
import threading
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

HOST = '0.0.0.0'
PORT = 5050


@dataclass
class WhiteboardState:
    strokes: List[dict] = field(default_factory=list)


class WhiteboardServer:
    def __init__(self, host: str = HOST, port: int = PORT):
        self.host = host
        self.port = port
        self.state = WhiteboardState()
        self.clients: Dict[socket.socket, Tuple[str, int]] = {}
        self.lock = threading.Lock()

    def start(self) -> None:
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind((self.host, self.port))
        server_socket.listen()

        print(f"Whiteboard server running on {self.host}:{self.port}")

        while True:
            client_socket, address = server_socket.accept()
            with self.lock:
                self.clients[client_socket] = address
            print(f"Client connected: {address[0]}:{address[1]}")
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
                address = self.clients.pop(client_socket, ('unknown', 0))
            print(f"Client disconnected: {address[0]}:{address[1]}")
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


if __name__ == '__main__':
    WhiteboardServer().start()
