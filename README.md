# Velvet

Velvet is a fully scriptable terminal multiplexer which draws heavy inspiration from tmux and neovim.
Like tmux, velvet "multiplexes" IO between an arbitrary number of terminal applications in a single terminal session.
Like neovim, velvet supports scripting nearly all behavior through its lua API.

Nearly all velvet behavior except the terminal emulator and process management is written in LUA, so it is possible
to create a truly custom velvet configuration. But 99% of users will want to use the default configuration.

Velvet is tested on Linux and macOS. It may or may not work on other unixes. (Makefile patching required for sure)

## Try it out

Try the live web demo at https://velvet.opie.lol

## Dependencies

Velvet depends on [lua version 5.5](https://www.lua.org/download.html)

and [utf8proc version 2.11.3](https://github.com/JuliaStrings/utf8proc)

Both are included in the source tree, and statically linked.

I also recommend installing a [patched nerd font](https://github.com/ryanoasis/nerd-fonts) to get access to additional glyphs and icons.

## Installation

Velvet is not packaged. You need to compile it from source. This should be
simple since building Velvet only requires `make`, `git`, and a C compiler
(tested with `gcc` and `clang`). An internet connection is required on the first
build to pull utf8proc.

```sh
$ make install
```

This installs velvet to PREFIX (default /usr/local)

## Documentation

For documentation, check the manual pages installedby `make install`.

``` sh
$ man velvet
```
