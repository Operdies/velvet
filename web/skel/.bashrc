# Prompt
PS1='\[\e[1;34m\]\w\[\e[0m\] \$ '

# History
HISTSIZE=1000
HISTCONTROL=ignoredups:erasedups
shopt -s histappend

# Navigation
shopt -s autocd
shopt -s cdspell

# Completion
set completion-ignore-case on
source /etc/bash_completion.d/vv

# Aliases
alias ls='ls --color=auto'
alias ll='ls -lah'
alias grep='grep --color=auto'

# Editor
export EDITOR=nvim
export VISUAL=nvim

cat <<MOTD
Welcome to Velvet! 

Velvet is a fully scriptable terminal multiplexer which draws heavy inspiration from TMUX and Neovim.
Like TMUX, velvet "multiplexes" IO between an arbitrary number of terminal applications in a single terminal window.
Like neovim, velvet allows scripting nearly all behavior through a LUA API.

Nearly all velvet behavior except the terminal emulator and process management is written in LUA, so it is possible
to create a truly custom velvet configuration. But 99% of users will want to use the default config included in this image.

To get started, press <C-x>h (that's ctrl+x, followed by h) to view the current keybindings.
Then take a look at the lua examples in this directory.

For more detailed information, check the man page! (man velvet)

Don't be afraid to experiment. You can always get back to a clean state by invoking the Reload hotkey: <C-x>r
Otherwise you can just refresh the page!
MOTD
