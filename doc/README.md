# LiME ~ Linux Memory Extractor

## Contents

* [Compiling LiME](#compiling-lime)
  * [Linux](#linux)
  * [External](#external)
  * [Debug](#debug)
  * [Symbols](#symbols)
  * [Android](#android)
* [Usage](#usage)
  * [Parameters](#parameters)
  * [Acquisition of Memory over TCP](#acquisition-of-memory-over-tcp)
  * [Acquisition of Memory to Disk](#acquisition-of-memory-to-disk)
* [LiME Memory Range Header Version 1
  Specification](#lime-memory-range-header-version-1-specification)

## Compiling LiME

### Linux

LiME is a Loadable Kernel Module (LKM). LiME ships with a
default Makefile that should be suitable for compilation on
most modern Linux systems.

For detailed instructions on using LKM see
<https://www.kernel.org/doc/Documentation/kbuild/modules.txt>.

### External

LiME can be compiled externally from the target in order to
provide a more forensically sound and secure method. Follow
this [guide](./external_modules.md) to learn how.

### Debug

When compiling LiME with the default Makefile, using the
command "make debug" will compile a LiME module with extra
debug output. The output can be read by using the dmesg
command on Linux.

### Symbols

When compiling LiME with the default Makefile, using the
command "make symbols" will compile a LiME module without
stripping symbols. This is useful for tools such as
Volatility where one can create a profile without loading
a second module.

### Android

In order to cross-compile LiME for use on an Android device,
additional steps are required.

#### Prerequisites

Disclaimer: This list may be incomplete. Please let us know
if we've missed anything.

* Install the general android prerequisites found at
  <http://source.android.com/source/initializing.html>
* Download and un(zip|tar) the android NDK found at
  <http://developer.android.com/tools/sdk/ndk/index.html>.
* Download and un(zip|tar) the android SDK found at
  <http://developer.android.com/sdk/index.html>.
* Download and untar the kernel source for your device. This
  can usually be found on the website of your device
  manufacturer or by a quick Google search.
* Root your device. In order to run custom kernel modules,
  you must have a rooted device.
* Plug the device into computer via a USB cable.

#### Setting Up the Environment

In order to simplify the process, we will first set some
environment variables. In a terminal, type the following
commands.

```bash
export SDK_PATH=/path/to/android-sdk-linux/
export NDK_PATH=/path/to/android-ndk/
export KSRC_PATH=/path/to/kernel-source/
export CC_PATH=$NDK_PATH/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86/bin/
export LIME_SRC=/path/to/lime/src
```

#### Preparing the Kernel Source

We must retrieve and copy the kernel config from our device.

```bash
cd $SDK_PATH/platform-tools
./adb pull /proc/config.gz
gunzip ./config.gz
cp config $KSRC_PATH/.config
```

Next we have to prepare our kernel source for our module.

```bash
cd $KSRC_PATH
make ARCH=arm CROSS_COMPILE=$CC_PATH/arm-eabi- modules_prepare
```

#### Preparing the Module for Compilation

We need to create a Makefile to cross-compile our kernel
module. A sample Makefile for cross-compiling is shipped with
the LiME source. The contents of your Makefile should be
similar to the following:

```text
obj-m := lime.o
lime-objs := tcp.o disk.o main.o hash.o deflate.o
KDIR := /path/to/kernel-source
PWD := $(shell pwd)
CCPATH := /path/to/android-ndk/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86/bin/
default:
 $(MAKE) ARCH=arm CROSS_COMPILE=$(CCPATH)/arm-eabi- -C $(KDIR) M=$(PWD) modules
```

#### Compiling the Module

```bash
cd $LIME_SRC
make
```

## Usage

The following examples demonstrate acquiring memory from a
Linux or Android device over TCP and to disk. On standard
Linux systems, you do not need adb -- simply use insmod
directly. The Android examples below use the Android Debug
Bridge (adb) to load the module and tunnel network traffic
over USB.

### Parameters

LiME supports multiple output formats, including a custom
lime format which integrates with Volatility's lime address
space.

NOTE: On some Android devices there is a bug in insmod where
multiple kernel module parameters must be wrapped in
quotation marks, otherwise only the first parameter will be
parsed. See the TCP and Disk acquisition examples below.

```text
path          Required. Either a filename to write on the
              local system or tcp:<port>
format        Required. One of the following:
              padded: Pads all non-System RAM ranges
              with 0s, starting from physical address 0.
              lime: Each range is prepended with a
              fixed-sized header which contains address
              space information.
              raw: Simply concatenates all System RAM
              ranges. Most memory analysis tools do not
              support this format, as memory position
              information is lost (unless System RAM is
              in one continuous range starting from
              physical address 0)
digest        Optional. Hash the RAM and provide a
              sidecar file with the sum. The sidecar
              filename is the output path with the digest
              algorithm appended (e.g., ram.lime.sha256).
              Supports kernel version 2.6.11 and up.
              When dumping over TCP, the digest file
              requires a second connection.
              Note: enabling digest increases code
              complexity during acquisition and will
              overwrite additional memory. Only use when
              integrity verification is required.
compress      Optional. 1 to compress output with zlib,
              0 to disable (default). Only available when
              CONFIG_ZLIB_DEFLATE is enabled in the
              kernel. Note: enabling compression
              allocates additional kernel memory (~24 KB)
              and increases code complexity during
              acquisition, disturbing more of the target
              system's memory. Only use when the speed or
              size benefit is required.
dio           Optional. 1 to enable Direct IO attempt,
              0 to disable (default)
localhostonly Optional. 1 restricts the tcp to only
              listen on localhost, 0 binds on all
              interfaces (default)
timeout       Optional. If it takes longer than the
              specified timeout (in milliseconds) to
              read/write a page of memory then the range
              is assumed to be bad and is skipped. To
              disable this set timeout to 0. The default
              setting is 1000 (1 second). Only available
              on kernel versions >= 2.6.35.
```

### Acquisition of Memory over TCP

#### Linux (TCP)

On the target system, load the module and listen on a TCP
port:

```bash
insmod ./lime-$(uname -r).ko "path=tcp:4444 format=lime"
```

On the receiving machine, connect with netcat and redirect
the output to a file:

```bash
nc <target-ip> 4444 > ram.lime
```

When acquisition is complete, LiME terminates the TCP
connection.

#### Android (TCP)

Copy the kernel module to the device using adb, set up a
port-forwarding tunnel, and obtain a root shell:

```bash
adb push lime.ko /sdcard/lime.ko
adb forward tcp:4444 tcp:4444
adb shell
su
```

Load the module on the device:

```bash
insmod /sdcard/lime.ko "path=tcp:4444 format=lime"
```

On the host, capture the memory dump through the forwarded
port:

```bash
nc localhost 4444 > ram.lime
```

### Acquisition of Memory to Disk

Disk-based acquisition may be preferred when the investigator
wants to avoid overwriting network buffers. Set the path
parameter to a file path to write the memory image directly
to disk.

#### Linux (Disk)

```bash
insmod ./lime-$(uname -r).ko "path=/tmp/ram.lime format=lime"
```

#### Android (Disk)

On Android, the SD card is a common destination. If the SD
card may contain other evidence, consider imaging it first
before writing the memory dump.

```bash
insmod /sdcard/lime.ko "path=/sdcard/ram.lime format=lime"
```

Once acquisition is complete, transfer the memory dump to the
examination machine using adb or by removing the SD card.

## LiME Memory Range Header Version 1 Specification

```c
typedef struct {
    unsigned int magic;        // Always 0x4C694D45 (LiME)
    unsigned int version;      // Header version number
    unsigned long long s_addr; // Starting address of range
    unsigned long long e_addr; // Ending address of range
    unsigned char reserved[8]; // Currently all zeros
} __attribute__ ((__packed__)) lime_mem_range_header;
```
