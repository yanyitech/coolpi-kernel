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

exit 0
