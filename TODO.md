Tasks that need doing in no particular order of priority:

* Chroming (visually distinguish panes)
* Scrollback buffer
* CSI: Configurable scroll region (needed for vim)
* Clear
* Reset
* Color / style CSIs
* Bracketed paste
* Integration testing
> test binary which sets up a pane -> emulates some stdin -> verify the layout of the result grid
* Performance testing 
> perf binary which spawns vv / tmux in a pty and sends a bunch of commands
* Layout system
> Tagging panes / toggling visible tags / keybind system, what this project is all about..
* Session management + socket protocol
> tbh, just rely on tmux for sessions and persistence until this thing is actually stable
