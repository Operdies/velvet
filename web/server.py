#!/usr/bin/env python3
import asyncio
import fcntl
import http
import http.server
import json
import os
import pty
import signal
import struct
import subprocess
import termios
import threading
from pathlib import Path

import websockets

PORT = 3000
VELVET = str(Path(__file__).resolve().parent.parent / "release" / "vv")
INDEX_HTML = Path(__file__).resolve().parent / "index.html"


def set_winsize(fd, rows, cols):
    winsize = struct.pack("HHHH", rows, cols, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, winsize)


async def serve_terminal(ws):
    master_fd, slave_fd = pty.openpty()
    set_winsize(master_fd, 40, 120)

    env = os.environ.copy()
    env["TERM"] = "xterm-256color"
    env["COLORTERM"] = "truecolor"

    proc = subprocess.Popen(
        [VELVET],
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        preexec_fn=os.setsid,
        env=env,
        cwd=os.environ["HOME"],
    )
    os.close(slave_fd)

    loop = asyncio.get_event_loop()

    # Use asyncio fd reader instead of polling
    pty_queue = asyncio.Queue()

    def on_pty_readable():
        try:
            data = os.read(master_fd, 65536)
            if data:
                pty_queue.put_nowait(data.decode("utf-8", errors="replace"))
        except OSError:
            loop.remove_reader(master_fd)

    loop.add_reader(master_fd, on_pty_readable)

    # PTY -> WebSocket
    async def pty_reader():
        try:
            while True:
                data = await pty_queue.get()
                await ws.send(data)
        except asyncio.CancelledError:
            pass

    reader_task = asyncio.create_task(pty_reader())

    # WebSocket -> PTY
    try:
        async for msg in ws:
            message = json.loads(msg)
            if message["type"] == "data":
                os.write(master_fd, message["data"].encode())
            elif message["type"] == "resize":
                set_winsize(master_fd, message["rows"], message["cols"])
                # SIGWINCH to the whole process group
                try:
                    os.killpg(proc.pid, signal.SIGWINCH)
                except ProcessLookupError:
                    pass
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        reader_task.cancel()
        loop.remove_reader(master_fd)
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        proc.wait()
        os.close(master_fd)


# Simple HTTP server for index.html
class HTTPHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            content = INDEX_HTML.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", len(content))
            self.end_headers()
            self.wfile.write(content)
        else:
            self.send_error(404)

    def log_message(self, *args):
        pass


async def main():
    httpd = http.server.HTTPServer(("localhost", PORT), HTTPHandler)
    http_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    http_thread.start()

    async with websockets.serve(serve_terminal, "localhost", PORT + 1):
        print(f"Velvet web terminal: http://localhost:{PORT}")
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
