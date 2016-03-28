#!/bin/bash
# NetHunter kernel for Samsung Galaxy S7 build script by jcadduono
# This build script is for TouchWiz with Kali Nethunter support only
#
###################### CONFIG ######################

# root directory of NetHunter herolte git repo (default is this script's location)
RDIR=$(pwd)

# output directory of Image and dtb.img
OUT_DIR=$RDIR

############## SCARY NO-TOUCHY STUFF ###############

ARCH=arm64
KDIR=$RDIR/arch/$ARCH/boot

MOVE_IMAGES()
{
	echo "Moving kernel Image and dtb.img to $VARIANT_DIR/..."
	mkdir -p $VARIANT_DIR
	rm -f $VARIANT_DIR/Image $VARIANT_DIR/dtb.img
	mv $KDIR/Image $KDIR/dtb.img $VARIANT_DIR/
}

mkdir -p $OUT_DIR

for DEVICE in herolte hero2lte
do
	VARIANT_DIR=$OUT_DIR/$DEVICE
	DEVICE=$DEVICE $RDIR/build.sh && DEVICE=$DEVICE $RDIR/dtbgen.sh $V && MOVE_IMAGES
done

echo "Finished building kernels!"

