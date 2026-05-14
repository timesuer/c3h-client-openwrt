FROM alpine:3.18 AS builder

# 安装编译工具和依赖
RUN apk add --no-cache \
    build-base \
    libpcap-dev \
    libpcap \
    linux-headers \
    wget \
    tar

# 编译
WORKDIR /build
COPY src/ /build/

# 静态编译 c3h-client
RUN cc -DNDEBUG -Wall -O2 -c auth.c -o auth.o && \
    cc -DNDEBUG -Wall -O2 -c main.c -o main.o && \
    cc -DNDEBUG -Wall -O2 -c md5.c -o md5.o && \
    cc -DNDEBUG -Wall -O2 -c adapter.c -o adapter.o && \
    cc -static auth.o main.o md5.o adapter.o -lpcap -o c3h-client && \
    strip c3h-client

# 输出阶段
FROM scratch
COPY --from=builder /build/c3h-client /c3h-client
