#!/bin/bash
# NetHunter kernel for Samsung Galaxy S7 build script by jcadduono

################### BEFORE STARTING ################
#
# download a working toolchain and extract it somewhere and configure this
# file to point to the toolchain's root directory.
#
# once you've set up the config section how you like it, you can simply run
# ./build.sh [VARIANT]
#
###################### CONFIG ######################

# root directory of NetHunter herolte git repo (default is this script's location)
RDIR=$(pwd)

# directory containing cross-compile arm64 toolchain
TOOLCHAIN=/Kernel_Folder/UBERTC-aarch64-linux-android-5.3

# amount of cpu threads to use in kernel make process
THREADS=3

############## SCARY NO-TOUCHY STUFF ###############

export ARCH=arm64
export CROSS_COMPILE=$TOOLCHAIN/bin/aarch64-linux-android-

DEFCONFIG=SuperKernel-herolte_defconfig

[ -f "$RDIR/arch/$ARCH/configs/${DEFCONFIG}" ] || {
	echo "Config $DEFCONFIG not found in $ARCH configs!"
	exit 1
}

KDIR=$RDIR/arch/$ARCH/boot

CLEAN_BUILD()
{
	echo "Cleaning build..."
	cd $RDIR
	git clean -xdf
}

BUILD_KERNEL()
{
	echo "Creating kernel config..."
	cd $RDIR
	make -C $RDIR $DEFCONFIG
	echo "Starting build..."
	make -C $RDIR -j"$THREADS"
}

CLEAN_BUILD && BUILD_KERNEL && echo "Finished building!"

