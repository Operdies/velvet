Tasks that need doing in no particular order of priority:

* "minimize" windows to 1 or 2 lines (maybe the top bar is minimized)
* Support multi-width characters (emojis, characters in other scripts?)
* Scrollback buffer (unless?)
* CSI: Configurable scroll region (needed for vim)
* Floating panes

Implement an efficient redraw algorithm. We definitely don't want to naively draw tiled panes and then naively fully redraw floating panes on every frame

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

* Layout system

Tagging panes / toggling visible tags / keybind system, what this project is
all about..

Formally separate rendering, io dispatch, and layout systems. Currently
everything is interleaved in the main event loop, which is not really
sustainable.

The current output pipeline is very neatly running through a state machine, but
the input pipeline is completely ad-hoc.

* Session management + socket protocol

tbh, just rely on tmux for sessions and persistence until this thing is
actually stable

* Implement query support for all modes

Run it through the dispatcher?
Currently, dispatcher returns true / false. Would also need to be able to know
if something changed. Update dispatcher to return 0/1/2 ?

* Improve query dispatch 

I initially assumed query responses would require communicating with the host
emulator, so I bubbled it up to the main event loop, but on further inspection
it looks like all output is known by the state machine, and the information
which is not known is static enough that it can be hardcoded or read once and
reused. It would be a great simplification to queue the response directly
rather than bubbling it up


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

* Neat thing

Encountered this C macro in the wild:
It solves a limitation of C macros when you want to declare related pieces
of data together, and then join them in separate static arrays:

```c
// load keymap table for linux
#define KEYMAP(k, s)    k,
static int keymap_linux_val[] = {
#include "keymap_linux.def"
};
#undef KEYMAP

#define KEYMAP(k, s)    s,
static char *keymap_linux_str[] = {
#include "keymap_linux.def"
};
#undef KEYMAP
```

the KEYMAP macro is invoked in the `keymap_linx.def` file, but different arguments
passed to the macro are extracted in each pass. This is awesome for static comp time configuration.

* Bugs
 - nvim + vim broken (scrolling, scroll region, missing queries)
 - MANPAGER=nvim +Man! hangs on startup
 - nvim shutdown slow (likely waiting for query response)
