#!/bin/bash

K_SRC=`pwd`

ARCH=`uname -m`
if [ "$ARCH" == "x86_64" ]; then
    export CROSS_COMPILE=aarch64-none-linux-gnu-
    TOOLCHAIN_ARM64=$K_SRC/toolchain/bin
    export PATH=$TOOLCHAIN_ARM64:$PATH
fi
export ARCH=arm64

rm -rf arch/arm64/boot/dts/rockchip/rk3588s-cp4.dtb
make ARCH=arm64 LOCALVERSION= rk3588s_cp4b_defconfig
make ARCH=arm64 LOCALVERSION= -j8
make ARCH=arm64 LOCALVERSION= modules -j8
cp arch/arm64/boot/Image.gz vmlinuz
cp arch/arm64/boot/dts/rockchip/rk3588s-cp4.dtb .

rm -rf out_modules
mkdir -p out_modules

cd $K_SRC
make ARCH=arm64 modules_install INSTALL_MOD_PATH=out_modules

cd $K_SRC/out_modules/lib/modules/5.10.66
unlink source
unlink build
ln -sf /usr/src/5.10.66/ build
ln -sf /usr/src/5.10.66/ source
cd $K_SRC/out_modules/lib/
tar -czf ../../modules.tar.gz *

cd $K_SRC
rm -rf out
mkdir -p out
cp vmlinuz out/
cp rk3588s-cp4.dtb out/
cp modules.tar.gz out/

rm -rf $K_SRC/boot_linux -R
		mkdir -p $K_SRC/boot_linux
		mkdir -p $K_SRC/boot_linux/extlinux
		touch $K_SRC/boot_linux/extlinux/extlinux.conf
		echo "label rockchip-kernel-kernel_$KERNEL_VERSION" >>$K_SRC/boot_linux/extlinux/extlinux.conf
		echo "  kernel /extlinux/Image" >>$K_SRC/boot_linux/extlinux/extlinux.conf
		echo "  fdt /extlinux/rk3588s-coolpi4b-v10-linux.dtb" >>$K_SRC/boot_linux/extlinux/extlinux.conf
		echo "  append earlycon=uart8250,mmio32,0xfeb50000 root=PARTUUID=614e0000-0000-4b53-8000-1d28000054a9 rw rootwait rootfstype=ext4 console=ttyS0,1500000n81" >>$K_SRC/boot_linux/extlinux/extlinux.conf
		cp $K_SRC/arch/arm64/boot/Image $K_SRC/boot_linux/extlinux 
		cp $K_SRC/arch/arm64/boot/dts/rockchip/rk3588s-cp4.dtb $K_SRC/boot_linux/extlinux/rk3588s-coolpi4b-v10-linux.dtb
		genext2fs -B 4096 -b 16384 -d $K_SRC/boot_linux -i 8192 -U $K_SRC/boot_linux.img

exit 0
