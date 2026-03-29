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
alias c='clear'

# Editor
export EDITOR=vim
export VISUAL=vim
