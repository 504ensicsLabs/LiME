# LiME - Linux Memory Extractor
# Author:
# Joe Sylve       - joe.sylve@gmail.com, @jtsylve

# MIT License

# Copyright (c) 2011-2021 Joe Sylve - 504ENSICS Labs

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

obj-m := lime.o
lime-objs := tcp.o disk.o main.o hash.o deflate.o

KVER ?= $(shell uname -r)

KDIR ?= /lib/modules/$(KVER)/build

PWD := $(shell pwd)

.PHONY: modules modules_install clean distclean debug

default:
	$(MAKE) -C $(KDIR) M="$(PWD)" modules
	strip --strip-unneeded lime.ko
	mv lime.ko lime-$(KVER).ko

debug:
	KCFLAGS="-DLIME_DEBUG" $(MAKE) CONFIG_DEBUG_SG=y -C $(KDIR) M="$(PWD)" modules
	strip --strip-unneeded lime.ko
	mv lime.ko lime-$(KVER).ko

symbols:
	$(MAKE) -C $(KDIR) M="$(PWD)" modules
	mv lime.ko lime-$(KVER).ko

modules:    main.c disk.c tcp.c hash.c lime.h
	$(MAKE) -C /lib/modules/$(KVER)/build M="$(PWD)" $@
	strip --strip-unneeded lime.ko

modules_install:    modules
	$(MAKE) -C $(KDIR) M="$(PWD)" $@

clean:
	rm -f *.o *.mod.c Module.symvers Module.markers modules.order \.*.o.cmd \.*.ko.cmd \.*.o.d
	rm -rf \.tmp_versions

distclean: mrproper
mrproper:    clean
	rm -f *.ko
