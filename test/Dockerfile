FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential bc flex bison libelf-dev libssl-dev dwarves \
        sparse busybox-static cpio curl git python3 \
        ca-certificates file \
        qemu-system-x86 qemu-system-arm qemu-system-misc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
