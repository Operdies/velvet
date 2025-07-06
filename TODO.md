Tasks that need doing in no particular order of priority:

* "minimize" windows to 1 or 2 lines (maybe the top bar is minimized)
* Support multi-width characters (emojis, characters in other scripts?)
* Scrollback buffer (unless?)
* CSI: Configurable scroll region (needed for vim)
* Floating panes

Implement an efficient redraw algorithm. We definitely don't want to naively draw tiled panes and then naively fully redraw floating panes on every frame

* Mouse support
* Change all char to typedef utf8_t (unsigned char) to avoid confusing ascii and utf8 strings
* Command buffer so stdin/out requests can be processed outside of the state
machine 

Some applications request the current cursor position which must be
provided on stdin. Other applications request information about terminal
features which velvet must request from the host emulator before it can
respond. Handling this inside the state machine complicates testing and adds
unnecessary IO latency.

In addition, reading stdin from the terminal is not really safe inside the
state machine since it could contain STDIN which must be processed in the main
loop. The main loop should always detect responses and dispatch them to the
appropriate pane.

* Performance testing 

perf binary which spawns vv / tmux in a pty and sends a bunch of commands. vv
doesn't need to be the fastest multiplexer in the world, but it would be good
to know if some sequences are being handled very poorly.

Don't spend time on this before before all basic terminal emulator features are supported

* Layout system

Tagging panes / toggling visible tags / keybind system, what this project is
all about..

* Session management + socket protocol

tbh, just rely on tmux for sessions and persistence until this thing is
actually stable

* Implement query support for all modes

Run it through the dispatcher?
Currently, dispatcher returns true / false. Would also need to be able to know
if something changed. Update dispatcher to return 0/1/2 ?

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

* Bugs
 - nvim + vim broken (scrolling, scroll region, missing queries)
 - MANPAGER=nvim +Man! hangs on startup
 - nvim shutdown slow (likely waiting for query response)
