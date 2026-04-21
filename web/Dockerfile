FROM alpine:latest
# Generously install tools to demonstrate lsp autocomplete and inline docs
RUN apk add --no-cache gcc musl-dev make linux-headers vim bash man-db jq bash-completion \
                      neovim neovim-doc lua-language-server \
                      tree-sitter tree-sitter-c tree-sitter-lua tree-sitter-markdown
RUN adduser -D demo
COPY web/skel /etc/skel
COPY . /etc/skel/velvet
# Override cflags. The makefile builds with native optimizations, but this is not
# supported in containers.
RUN make -C /etc/skel/velvet install -j$(nproc)
WORKDIR /home/demo
RUN ln -s /home/demo/.config/velvet/init.lua /home/demo/config.lua && \
    chown -R demo:demo /home/demo
ENV SHELL=/bin/bash
ENV TERM=xterm-256color
ENV COLORTERM=truecolor
ENV MANPAGER='nvim +Man! -'
USER demo
ENTRYPOINT ["/bin/sh", "-c", "cp -r /etc/skel/. /home/demo && { while true; do /home/demo/velvet/release/vv -S demo; done }"]
