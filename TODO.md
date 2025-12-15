Tasks that need doing in no particular order of priority:

* "minimize" windows to 1 or 2 lines (maybe the top bar is minimized)
* Support multi-width characters (emojis, characters in other scripts?)
* Scrollback buffer (unless?)
* CSI: Configurable scroll region (needed for vim)
* Floating pty_hosts

Implement an efficient redraw algorithm. We definitely don't want to naively draw tiled pty_hosts and then naively fully redraw floating pty_hosts on every frame

* Replay mechanism

Record & replay sessions. The main use case is debugging scenarios and
end-to-end tests, but could be useful for automation.

* Mouse support
* Change all char to typedef utf8_t (unsigned char) to avoid confusing ascii and utf8 strings

* Performance testing 

perf binary which spawns vv / tmux in a pty and sends a bunch of commands. vv
doesn't need to be the fastest multiplexer in the world, but it would be good
to know if some sequences are being handled very poorly.

Don't spend time on this before before all basic terminal emulator features are supported

* Improve IO

Right now, pty_host->pty is being accessed willy-nilly. We have a `pending_output` buffer
which is flushed to the pty after a pty_host is processed. All writing can be handled from there.

Additionally, string buffers are being synchronously flushed to streams.
It would be great to early return on EAGAIN and add the write task to the main loop instead.

* Layout system

Tagging pty_hosts / toggling visible tags / keybind system, what this project is
all about..

Formally separate rendering, io dispatch, and layout systems. Currently
everything is interleaved in the main event loop, which is not really
sustainable.

The current output pipeline is very neatly running through a state machine, but
the input pipeline is completely ad-hoc.

* Session management + socket protocol

tbh, just rely on tmux for sessions and persistence until this thing is
actually stable

socket control motivation: I recently thought of using tmux as an "external"
terminal for nvim-dap if the nvim session is nested in a tmux session. The
below snippet works flawlessly. Communicating with the controlling vv session
is a must in order to implement this kind of integration.

```lua
if vim.fn.getenv("TMUX") ~= vim.NIL then
  dap.defaults.fallback.external_terminal = {
    command = 'tmux',
    args = { 'split-window', '-d',  '-h',  '-l', '80' }
  }
end
```

* Implement query support for all modes

Run it through the dispatcher?
Currently, dispatcher returns true / false. Would also need to be able to know
if something changed. Update dispatcher to return 0/1/2 ?

* Graphics support

Graphics comes in two flavors; 
 - sixel -- not widely supported, and no plans to implement it in alacritty /
 most mainstream terminals, except Windows Terminal.
- kitty graphics -- considered superior to sixel as it allows efficiently
specifying data sources and generally supports much more advanced features

I have no interest in implementing sixel support if alacritty will not support it,
which is likely never.

* Status bar

Something similar to the stock dwm bar. Make focusable (probably make it
special..) Use it as a launcher so focusing it and typing e.g. vim opens vim in
a new pty_host Maybe some keyboard shortcuts are only available in the status bar
(per pty_host bindings?) so it can be used as a management pty_host Consider making the
status bar a regular binary (rely on ^[[I/^[[O to switch mode)

If the status bar is a regular binary, how does it know about vv internals it
should display? (selected tag, what windows are on what tags, etc.) Feed this
information on stdin? And then the bar should interpret whether stdin is user
input based on whether it is focused or not?

* Bugs
