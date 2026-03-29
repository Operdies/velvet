FROM alpine:latest AS builder
RUN apk add --no-cache gcc musl-dev linux-headers make
COPY . /build
WORKDIR /build
RUN make release -j$(nproc) RELEASE_CFLAGS="-Os -DNDEBUG -DRELEASE_BUILD"

FROM alpine:latest
RUN apk add --no-cache gcc musl-dev make vim bash
RUN adduser -D demo
COPY --from=builder /build /etc/skel/velvet
COPY web/entrypoint.sh /usr/local/bin/entrypoint.sh
COPY web/skel /etc/skel
COPY shell-completion/bash/vv /etc/bash_completion.d/vv
RUN ln -s .config/velvet/init.lua /etc/skel/config.lua && \
    chown -R demo:demo /etc/skel
WORKDIR /home/demo
ENV SHELL=/bin/bash
ENTRYPOINT ["entrypoint.sh"]
