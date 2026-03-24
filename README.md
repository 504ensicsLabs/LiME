# LiME ~ Linux Memory Extractor

A Loadable Kernel Module (LKM) for volatile memory
acquisition from Linux and Linux-based devices, such as
Android. LiME minimizes its interaction between user and
kernel space processes during acquisition, producing memory
captures that are more forensically sound than those of
other tools designed for Linux memory acquisition.

## Table of Contents

* [Features](#features)
* [Usage](#usage)
  * [Examples](#examples)
* [Available Digests](#available-digests)
* [Compression](#compression)
* [Presentation](#presentation)

## Features

* Full memory acquisition from Linux (and Android) systems
* Acquisition over network interface or to local disk
* Multiple output formats (raw, lime, padded)
* Optional hashing with sidecar digest file
* Optional zlib compression
* Minimal process footprint

## Usage

Detailed documentation on LiME's usage and internals can
be found in the "docs" directory of the project.

LiME uses the insmod command to load the module,
passing required arguments for its execution.

```text
insmod ./lime-$(uname -r).ko "path=<outfile | tcp:<port>>
    format=<raw|padded|lime>
    [digest=<digest>]
    [dio=<0|1>]
    [compress=<0|1>]
    [localhostonly=<0|1>]
    [timeout=<ms>]"

path (required):
    outfile ~ name of file to write to on local system
    tcp:port ~ network port to communicate over

format (required):
    padded ~ pads all non-System RAM ranges with 0s,
             starting from physical address 0
    lime ~ each range prepended with fixed-size header
           containing address space info
    raw ~ concatenates all System RAM ranges
          (warning: original position of dumped memory
          is likely to be lost therefore making analysis
          in most forensics tools impossible. This format
          is not recommended except for advanced users)

digest (optional):
    Hash the RAM and provide a sidecar file with the sum.
    The sidecar filename is the output path with the
    digest algorithm appended (e.g., ram.lime.sha256).
    Supports kernel version 2.6.11 and up. See below for
    available digest options.
    Note: enabling digest increases code complexity during
    acquisition and will overwrite additional memory. Only
    use when integrity verification is required.

compress (optional):
    1 ~ compress output with zlib
    0 ~ do not compress (default)
    Only available when CONFIG_ZLIB_DEFLATE is enabled
    in the kernel.
    Note: enabling compression allocates additional kernel
    memory (~24 KB) and increases code complexity during
    acquisition, disturbing more of the target system's
    memory. Only use when the speed or size benefit is
    required.

dio (optional):
    1 ~ attempt to enable Direct IO
    0 ~ do not attempt Direct IO (default)

localhostonly (optional):
    1 ~ restricts tcp to only listen on localhost
    0 ~ binds on all interfaces (default)

timeout (optional):
    1000 ~ max milliseconds tolerated to read/write a
           page (default, 1 second). If a page exceeds the timeout,
           the rest of that memory range is skipped.
       0 ~ disable the timeout so the slow region will
           be acquired.
    This feature is only available on kernels >= 2.6.35.
```

## Examples

### Linux

Acquiring memory to a file:

```bash
insmod ./lime-$(uname -r).ko "path=/tmp/ram.lime format=lime"
```

Acquiring memory over the network:

```bash
insmod ./lime-$(uname -r).ko "path=tcp:4444 format=lime"
```

Then on the receiving machine:

```bash
nc <target-ip> 4444 > ram.lime
```

### Android

Use adb to load LiME and acquire memory over the network:

```bash
adb push lime.ko /sdcard/lime.ko
adb forward tcp:4444 tcp:4444
adb shell
su
insmod /sdcard/lime.ko "path=tcp:4444 format=lime"
```

On the host machine, capture the memory dump using netcat:

```bash
nc localhost 4444 > ram.lime
```

Acquiring to the SD card:

```bash
insmod /sdcard/lime.ko "path=/sdcard/ram.lime format=lime"
```

## Available Digests

LiME supports any digest algorithm available in the
kernel's crypto library. Collecting a digest file when
dumping over TCP requires 2 separate connections.

```bash
nc localhost 4444 > ram.lime
nc localhost 4444 > ram.sha1
```

For quick reference, here is a list of supported digests.

### 2.6.11 and up

```text
crc32c
md4, md5
sha1, sha224, sha256, sha384, sha512
wp512, wp384, wp256
```

### 3.0 and up

```text
rmd128, rmd160, rmd256, rmd320
```

### 4.10 and up

```text
sha3-224, sha3-256, sha3-384, sha3-512
```

## Compression

Compression can significantly reduce the time required
to acquire a memory capture. It can achieve a speedup of
4x over uncompressed transfers with minimal memory
overhead (~24 KB).

The RAM file will be in the zlib format, which is
different from the gzip or zip formats. The reason is
that the deflate library embedded in the kernel does not
support them.

To decompress it you can use
[pigz](https://zlib.net/pigz/) or any zlib-compatible
library.

```bash
nc localhost 4444 | unpigz > ram.lime
```

Note that only the RAM file is compressed. The digest
file is not compressed, and the hash value will match
the uncompressed data.

## Presentation

LiME was first presented at Shmoocon 2012 by Joe Sylve.

Youtube~
[Android Mind Reading: Memory Acquisition and Analysis
with DMD and Volatility][shmoocon-talk]

[shmoocon-talk]: https://www.youtube.com/watch?v=oWkOyphlmM8
