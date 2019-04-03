# LiME - Linux Memory Extractor
# Copyright (c) 2011-2014 Joe Sylve - 504ENSICS Labs
#
#
# Author:
# Joe Sylve       - joe.sylve@gmail.com, @jtsylve
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or (at
# your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA


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
