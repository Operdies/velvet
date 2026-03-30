FROM alpine:latest
RUN apk add --no-cache gcc musl-dev make vim bash linux-headers neovim
RUN adduser -D demo
COPY shell-completion/bash/vv /etc/bash_completion.d/vv
COPY web/skel /etc/skel
COPY . /etc/skel/velvet
# Override cflags. The makefile builds with native optimizations, but this is not
# supported in containers.
RUN make -C /etc/skel/velvet release -j$(nproc) RELEASE_CFLAGS="-Os -DNDEBUG -DRELEASE_BUILD"
WORKDIR /home/demo
RUN ln -s /etc/skel/.config/velvet/init.lua /home/demo/config.lua && \
    chown -R demo:demo /home/demo
ENV SHELL=/bin/bash
ENV TERM=xterm-256color
ENV COLORTERM=truecolor
USER demo
ENTRYPOINT ["/bin/sh", "-c", "cp -r /etc/skel/. /home/demo && /home/demo/velvet/release/vv"]
