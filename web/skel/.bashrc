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

yellow=$(printf '\x1b[33m')
red=$(printf '\x1b[31m')
blue=$(printf '\x1b[34m')
bold=$(printf '\x1b[1m')
italic=$(printf '\x1b[3m')
reset=$(printf '\x1b[m')
cat <<MOTD
${bold}Welcome to Velvet!${reset}

Velvet is a fully scriptable terminal multiplexer which draws heavy inspiration from tmux and neovim.
Like tmux, velvet enables controlling multiple terminals from the same screen.
Like neovim, velvet enables scripting nearly all system behavior through a LUA API.

To view current keybindings, press ${red}${bold}<C-x>h${reset}. This menu is automatically updated if you add ned bindings.

To get started, check out the samples in the home directory.
If you edit config.lua, you can apply the changes with the reload hot key: ${red}${bold}<C-x>r${reset}

Neovim is installed with a lua LSP configured with Velvet autocomplete,
so you can explore the samples if you wish.
You can get a fresh environment by refreshing the page or reloading with ${red}${bold}<C-x>r${reset}.

You can also reload by sending a chunk to the socket with 
${blue}${italic} > vv lua 'vv.api.reload()' ${reset}

For more detailed information, check the man page.
${blue}${italic} > man velvet${reset}
MOTD
