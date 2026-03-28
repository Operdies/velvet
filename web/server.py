#!/usr/bin/env python3
import asyncio
import fcntl
import http
import json
import os
import pty
import signal
import struct
import subprocess
import sys
import termios
import uuid
from pathlib import Path

import websockets
from websockets.datastructures import Headers
from websockets.http11 import Response

IMAGE = os.environ.get("VELVET_IMAGE", "velvet")
INDEX_HTML = Path(__file__).resolve().parent / "index.html"


def set_winsize(fd, rows, cols):
    winsize = struct.pack("HHHH", rows, cols, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, winsize)


async def serve_terminal(ws):
    master_fd, slave_fd = pty.openpty()
    set_winsize(master_fd, 40, 120)

    container_name = f"velvet-{uuid.uuid4().hex[:12]}"

    proc = subprocess.Popen(
        [
            "podman", "run",
            "-it",
            "--rm",
            "--name", container_name,
            "--hostname", "velvet",
            "--memory", "256m",
            "--cpus", "0.5",
            "--pids-limit", "64",
            "--user", "demo",
            "--read-only",
            "--tmpfs", "/tmp:size=256m",
            "--tmpfs", "/home/demo:uid=1000,size=256m",
            "--network", "none",
            "--cap-drop", "ALL",
            "--security-opt", "no-new-privileges",
            "-e", "TERM=xterm-256color",
            "-e", "COLORTERM=truecolor",
            IMAGE,
        ],
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        preexec_fn=os.setsid,
    )
    os.close(slave_fd)

    loop = asyncio.get_event_loop()

    pty_queue = asyncio.Queue()
    utf8_buf = bytearray()

    def on_pty_readable():
        nonlocal utf8_buf
        try:
            data = os.read(master_fd, 65536)
            if not data:
                return
            utf8_buf.extend(data)
            try:
                text = utf8_buf.decode("utf-8")
                utf8_buf.clear()
            except UnicodeDecodeError:
                for i in range(1, 4):
                    try:
                        text = utf8_buf[:-i].decode("utf-8")
                        utf8_buf = utf8_buf[-i:]
                        break
                    except UnicodeDecodeError:
                        continue
                else:
                    return
            pty_queue.put_nowait(text)
        except OSError:
            loop.remove_reader(master_fd)

    loop.add_reader(master_fd, on_pty_readable)

    async def pty_reader():
        try:
            while True:
                data = await pty_queue.get()
                await ws.send(data)
        except asyncio.CancelledError:
            pass

    reader_task = asyncio.create_task(pty_reader())

    try:
        async for msg in ws:
            message = json.loads(msg)
            if message["type"] == "data":
                os.write(master_fd, message["data"].encode())
            elif message["type"] == "resize":
                set_winsize(master_fd, message["rows"], message["cols"])
                try:
                    os.killpg(proc.pid, signal.SIGWINCH)
                except ProcessLookupError:
                    pass
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        reader_task.cancel()
        loop.remove_reader(master_fd)
        # Kill the container without blocking the event loop
        await asyncio.to_thread(subprocess.run,
            ["podman", "kill", container_name],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        await asyncio.to_thread(proc.wait)
        os.close(master_fd)


FONTS_DIR = Path(__file__).resolve().parent / "fonts"


async def process_request(connection, request):
    if request.headers.get("Upgrade", "").lower() == "websocket":
        return None
    if request.path == "/" or request.path == "/index.html":
        content = INDEX_HTML.read_bytes()
        return Response(200, "OK", Headers({"Content-Type": "text/html"}), content)
    if request.path.startswith("/fonts/") and request.path.endswith(".woff2"):
        font_path = FONTS_DIR / Path(request.path).name
        if font_path.is_file():
            content = font_path.read_bytes()
            return Response(200, "OK", Headers({"Content-Type": "font/woff2"}), content)
    return Response(404, "Not Found", Headers(), b"Not Found")


async def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

    async with websockets.serve(
        serve_terminal, "localhost", port, process_request=process_request
    ):
        print(f"Velvet web terminal: http://localhost:{port}")
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
