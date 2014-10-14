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

## Usage <a name="usage"/>
LiME utilizes the insmod command to load the module, passing required arguments for its execution.

```
insmod ./lime.ko "path=<outfile | tcp:<port>> format=<raw|padded|lime> [dio=<0|1>]"

path:   outfile ~ name of file to write to on local system (SD Card)
        tcp:port ~ network port to communicate over
        
format: raw ~ concatenates all System RAM ranges
        padded ~ pads all non-System RAM ranges with 0s
        lime ~ each range prepended with fixed-size header containing address space info
        
dio:    1 ~ default, attempt to enable Direct IO
        0 ~ do not attempt Direct IO
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

## Presentation <a name="present"/>
LiME was first presented at Shmoocon 2012 by Joe Sylve.  
Youtube~ <a href="https://www.youtube.com/watch?v=oWkOyphlmM8">Android Mind Reading: Memory Acquisition and Analysis with DMD and Volatility</a>
