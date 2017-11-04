# LiME ~ Linux Memory Extractor
A Loadable Kernel Module (LKM) which allows for volatile memory acquisition from Linux and Linux-based devices, such as Android. This makes LiME unique as it is the first tool that allows for full memory captures on Android devices. It also minimizes its interaction between user and kernel space processes during acquisition, which allows it to produce memory captures that are more forensically sound than those of other tools designed for Linux memory acquisition.

## Table of Contents
 * [Features](#features)
 * [Usage](#usage)
  * [Examples](#example)
 * [Presentation](#present)
 
## Features <a name="features"/>
* Full Android memory acquisition
* Acquisition over network interface
* Minimal process footprint
* Hash of dumped memory

## Usage <a name="usage"/>
Detailed documentation on LiME's usage and internals can be found in the "doc" directory of the project.

LiME utilizes the insmod command to load the module, passing required arguments for its execution.
```
insmod ./lime.ko "path=<outfile | tcp:<port>> format=<raw|padded|lime> [digest=<digest>] [dio=<0|1>]"

path (required):   outfile ~ name of file to write to on local system (SD Card)
                   tcp:port ~ network port to communicate over
        
format (required): raw ~ concatenates all System RAM ranges
                   padded ~ pads all non-System RAM ranges with 0s
                   lime ~ each range prepended with fixed-size header containing address space info

digest (optional): Supports kernel version 2.6.11 and up. See below for
                   available digest options.
        
dio (optional):    1 ~ attempt to enable Direct IO
                   0 ~ default, do not attempt Direct IO
        
localhostonly (optional):  1 ~ restricts the tcp to only listen on localhost,
                           0 ~ binds on all interfaces (default)

timeout (optional): 1000 ~ max amount of milliseconds tolerated to read a page (default).
                           If a page exceeds the timeout all the memory region are skipped.
                       0 ~ disable the timeout so the slow region will be acquired.

                           This feature is only available on kernel versions >= 2.6.35. 

```

## Examples <a name="example"/>
In this example we use adb to load LiME and then start it with acquisition performed over the network
```
$ adb push lime.ko /sdcard/lime.ko
$ adb forward tcp:4444 tcp:4444
$ adb shell
$ su
# insmod /sdcard/lime.ko "path=tcp:4444 format=lime"
```

Now on the host machine, we can establish the connection and acquire memory using netcat
```
$ nc localhost 4444 > ram.lime
```

Acquiring to sdcard
```
# insmod /sdcard/lime.ko "path=/sdcard/ram.lime format=lime"
```

## Available Digests
Really LiME will support any digest algorithm that the kernel library can. For a quick reference here is a list of supported digests.
### All kernel versions
```
crc32c
md4, md5, 
sha1, sha224, sha256, sha384, sha512
wp512, wp384, wp256
```
### 4.10 and up
```
sha3-224, sha3-256, sha3-384, sha3-512
```
### 3.0 and up
```
rmd128, rmd160, rmd256, rmd320
```


## Presentation <a name="present"/>
LiME was first presented at Shmoocon 2012 by Joe Sylve.  
Youtube~ <a href="https://www.youtube.com/watch?v=oWkOyphlmM8">Android Mind Reading: Memory Acquisition and Analysis with DMD and Volatility</a>
