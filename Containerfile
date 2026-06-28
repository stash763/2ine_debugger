FROM docker.io/i386/debian:bookworm

RUN apt-get update && apt-get install -y \
    cmake \
    gcc \
    g++ \
    make \
    libncursesw5-dev \
    libsdl2-dev \
    libsdl2-2.0-0 \
    libdwarf-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
