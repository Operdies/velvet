Tasks that need doing in no particular order of priority:

* Chroming (visually distinguish panes)
* Scrollback buffer (unless?)
* CSI: Configurable scroll region (needed for vim)
* Color / style CSIs
* Bracketed paste
* Mouse support
* Performance testing 

perf binary which spawns vv / tmux in a pty and sends a bunch of commands vv
doesn't need to be the fastest multiplexer in the world, but it would be good
to know if some sequences are being handled very poorly.

Don't spend time on this before before all basic terminal emulator features are supported

* Layout system

Tagging panes / toggling visible tags / keybind system, what this project is
all about..

* Session management + socket protocol

tbh, just rely on tmux for sessions and persistence until this thing is
actually stable

* Status bar

Something similar to the stock dwm bar. Make focusable (probably make it
special..) Use it as a launcher so focusing it and typing e.g. vim opens vim in
a new pane Maybe some keyboard shortcuts are only available in the status bar
(per pane bindings?) so it can be used as a management pane Consider making the
status bar a regular binary (rely on ^[[I/^[[O to switch mode)

If the status bar is a regular binary, how does it know about vv internals it
should display? (selected tag, what windows are on what tags, etc.) Feed this
information on stdin? And then the bar should interpret whether stdin is user
input based on whether it is focused or not?
