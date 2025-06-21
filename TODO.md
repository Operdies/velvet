Tasks that need doing in no particular order of priority:

* Chroming (visually distinguish panes)
* Scrollback buffer (unless?)
* CSI: Configurable scroll region (needed for vim)
* Color / style CSIs
* Bracketed paste
* Mouse support
* Performance testing 
> perf binary which spawns vv / tmux in a pty and sends a bunch of commands
* Layout system
> Tagging panes / toggling visible tags / keybind system, what this project is all about..
* Session management + socket protocol
> tbh, just rely on tmux for sessions and persistence until this thing is actually stable
* Status bar
> Something similar to the dwm stock bar. Make focusable (probably make it
> special..) Use it as a launcher so focusing it and typing e.g. vim opens vim
> in a new pane Maybe some keyboard shortcuts are only available in the status
> bar (per pane bindings?) so it can be used as a management pane
> Consider making the status bar a regular binary (rely on ^[[I/^[[O to switch mode)
