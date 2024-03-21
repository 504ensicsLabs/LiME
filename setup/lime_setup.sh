#!/bin/bash
# shellcheck disable=SC2154
#
# Script For Building LiME as LKM
#
# Copyright (c) 2024 Panchajanya1999 <kernel@panchajanya.dev>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

# LiME building script
# This script is used to add LiME to the kernel source and build it as a module.
#
# Bail out if script fails
set -e

# Check if git is available
if ! command -v git &> /dev/null; then
    echo "[ERROR] Git is not installed. Please install Git and try again."
    exit 1
fi

GKI_ROOT=$(pwd)
echo "[+] GKI_ROOT: $GKI_ROOT"

if test -d "$GKI_ROOT/common/drivers"; then
    DRIVER_DIR="$GKI_ROOT/common/drivers"
elif test -d "$GKI_ROOT/drivers"; then
    DRIVER_DIR="$GKI_ROOT/drivers"
else
    echo '[ERROR] "drivers/" directory is not found.'
    echo '[+] You should modify this script by yourself.'
    exit 127
fi

# Clone the LiME repository if it doesn't exist
if ! test -d "$DRIVER_DIR/lime"; then
    git clone https://github.com/Panchajanya1999/LiME.git $DRIVER_DIR/lime
fi

# Add the line to Kconfig if it doesn't exist
if ! grep -q 'source "drivers/lime/src/Kconfig"' "$DRIVER_DIR/Kconfig"; then
    echo 'source "drivers/lime/src/Kconfig"' >> $DRIVER_DIR/Kconfig
fi

# Add the line to Makefile if it doesn't exist
if ! grep -q 'obj-\$(CONFIG_LIME)\\t\\t+= lime/' "$DRIVER_DIR/Makefile"; then
    echo -e "\n# LiME\nobj-\$(CONFIG_LIME)\t\t+= lime/" >> $DRIVER_DIR/Makefile
fi

echo "[+] LiME has been added to the kernel source."
