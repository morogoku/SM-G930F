#!/bin/bash
# simple bash script for generating dtb image

# directory containing cross-compile arm64 toolchain
TOOLCHAIN=/Kernel_Folder/UBERTC-aarch64-linux-android-5.3

# device dependant variables
PAGE_SIZE=2048
DTB_PADDING=0

export ARCH=arm64
export CROSS_COMPILE=$TOOLCHAIN/bin/aarch64-linux-android-

RDIR=$(pwd)
OUTDIR=$RDIR/arch/$ARCH/boot
DTSDIR=$RDIR/arch/$ARCH/boot/dts
DTBDIR=$OUTDIR/dtb
DTCTOOL=$RDIR/scripts/dtc/dtc
INCDIR=$RDIR/include

[ -f "$DTCTOOL" ] || {
	echo "You need to run ./build.sh first!"
	exit 1
}

[ "$DEVICE" ] || DEVICE=herolte

case $DEVICE in
herolte)
	DTSFILES="exynos8890-herolte_eur_open_00 exynos8890-herolte_eur_open_01
			exynos8890-herolte_eur_open_02 exynos8890-herolte_eur_open_03
			exynos8890-herolte_eur_open_04 exynos8890-herolte_eur_open_08
			exynos8890-herolte_eur_open_09"
	;;
hero2lte)
	DTSFILES="exynos8890-hero2lte_eur_open_00 exynos8890-hero2lte_eur_open_01
			exynos8890-hero2lte_eur_open_03 exynos8890-hero2lte_eur_open_04
			exynos8890-hero2lte_eur_open_08"
	;;
*)
	echo "Unknown device: $DEVICE"
	exit 1
	;;
esac

mkdir -p $OUTDIR $DTBDIR

cd $DTBDIR || {
	echo "Unable to cd to $DTBDIR!"
	exit 1
}

rm -f ./*

echo "Processing dts files..."

for dts in $DTSFILES; do
	echo "=> Processing: ${dts}.dts"
	${CROSS_COMPILE}cpp -nostdinc -undef -x assembler-with-cpp -I "$INCDIR" "$DTSDIR/${dts}.dts" > "${dts}.dts"
	echo "=> Generating: ${dts}.dtb"
	$DTCTOOL -p $DTB_PADDING -i "$DTSDIR" -O dtb -o "${dts}.dtb" "${dts}.dts"
done

echo "Generating dtb.img..."
$RDIR/scripts/dtbTool/dtbTool -o "$OUTDIR/dtb.img" -d "$DTBDIR/" -s $PAGE_SIZE

echo "Done."
