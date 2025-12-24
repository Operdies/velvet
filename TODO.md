Tasks that need doing in no particular order of priority:

* "minimize" windows to 1 or 2 lines (maybe the top bar is minimized)

* Layout system

Tagging velvet_windows / toggling visible tags / keybind system, what this project is
all about..

Formally separate rendering, io dispatch, and layout systems. Currently
everything is interleaved in the main event loop, which is not really
sustainable.

The current output pipeline is very neatly running through a state machine, but
the input pipeline is completely ad-hoc.


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
a new velvet_window Maybe some keyboard shortcuts are only available in the status bar
(per velvet_window bindings?) so it can be used as a management velvet_window Consider making the
status bar a regular binary (rely on ^[[I/^[[O to switch mode)

If the status bar is a regular binary, how does it know about vv internals it
should display? (selected tag, what windows are on what tags, etc.) Feed this
information on stdin? And then the bar should interpret whether stdin is user
input based on whether it is focused or not?

* Bugs

Plenty
