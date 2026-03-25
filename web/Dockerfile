FROM alpine:latest AS builder
RUN apk add --no-cache gcc musl-dev linux-headers make git
RUN git clone --recurse-submodules https://github.com/Operdies/velvet.git -b browser /build
WORKDIR /build
RUN make release -j$(nproc) RELEASE_CFLAGS="-Os -DNDEBUG -DRELEASE_BUILD"

FROM alpine:latest
RUN apk add --no-cache gcc musl-dev make vim bash
RUN adduser -D demo
COPY --from=builder /build/release/vv /usr/local/bin/vv
COPY --from=builder /build/lua /usr/local/lua
COPY web/entrypoint.sh /usr/local/bin/entrypoint.sh
USER demo
WORKDIR /home/demo
ENV SHELL=/bin/bash
ENTRYPOINT ["/bin/bash"]
