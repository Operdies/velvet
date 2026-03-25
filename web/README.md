# Velvet Web Terminal

Run velvet in the browser using xterm.js.

## Setup

```bash
cd web
python3 -m venv .venv
.venv/bin/pip install websockets
```

## Usage

Build the release binary first if you haven't:

```bash
make release -j8
```

Then start the server:

```bash
.venv/bin/python server.py
```

Open http://localhost:3000.

## Notes

- Option/Alt keybindings are translated to work in the browser.
- Install a Nerd Font (e.g. MesloLGS Nerd Font) for glyph rendering.
