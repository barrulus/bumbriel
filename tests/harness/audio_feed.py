"""Upstream effect-audio producer for scene integration checks."""
import json
import os
import socket
import threading


class AudioFeed:
    def __init__(self):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(os.environ['UMBRIEL_SOCKET'])
        self.level = 0.0
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.error = None
        self.set(0.0)
        self.worker = threading.Thread(target=self._feed, daemon=True)
        self.worker.start()

    def _send(self):
        self.socket.sendall((json.dumps({'cmd': 'effect-audio', 'version': 1, 'level': self.level}) + '\n').encode())
        reply = b''
        while not reply.endswith(b'\n'):
            chunk = self.socket.recv(256)
            if not chunk:
                raise EOFError('audio producer disconnected')
            reply += chunk
        assert json.loads(reply) == {'ok': True}, reply

    def set(self, level):
        with self.lock:
            if self.error:
                raise self.error
            self.level = level
            self._send()

    def _feed(self):
        try:
            while not self.stop.wait(.02):
                with self.lock:
                    self._send()
        except Exception as error:
            self.error = error

    def close(self):
        self.stop.set()
        self.worker.join(4)
        self.socket.close()
        assert not self.worker.is_alive(), 'audio producer did not stop'
        if self.error:
            raise self.error
