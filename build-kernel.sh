#!/bin/bash

K_SRC=`pwd`

ARCH=`uname -m`
if [ "$ARCH" == "x86_64" ]; then
    export CROSS_COMPILE=aarch64-none-linux-gnu-
    TOOLCHAIN_ARM64=$K_SRC/toolchain/bin
    export PATH=$TOOLCHAIN_ARM64:$PATH
fi
export ARCH=arm64

BOARD=$1

GEN_DEBS="NO"

case "$BOARD" in
  cp4b)
    cfg="rk3588s_cp4b_defconfig"
    dtb="rk3588s-cp4.dtb rk3588s-cp4-minimal.dtb rk3588s-cp4-dsi.dtb rk3588s-cp4-sfc.dtb"
    txt_config_file="config_cp4b.txt"
    txt_extconf_file="extlinux_cp4b.conf"
    ;;
  cm5-evb)
    cfg="rk3588_cpcm5_evb_defconfig"
    dtb="rk3588-cpcm5-evb.dtb"
    txt_config_file="config_cpcm5_evb.txt"
    txt_extconf_file="extlinux_cpcm5_evb.conf"
    ;;
  cm5-evb-v11)
    cfg="rk3588_cpcm5_evb_defconfig"
    dtb="rk3588-cpcm5-evb-v11.dtb"
    txt_config_file="config_cpcm5_evb_v11.txt"
    txt_extconf_file="extlinux_cpcm5_evb_v11.conf"
    ;;
  cm5-minipc)
    cfg="rk3588_cpcm5_minipc_defconfig"
    dtb="rk3588-cpcm5-minipc.dtb"
    txt_config_file="config_cpcm5_minipc.txt"
    txt_extconf_file="extlinux_cpcm5_minipc.conf"
    ;;
  cm5-notebook)
    cfg="rk3588_cpcm5_notebook_defconfig"
    dtb="rk3588-cpcm5-notebook.dtb"
    txt_config_file="config_cpcm5_notebook.txt"
    txt_extconf_file="extlinux_cpcm5_notebook.conf"
    ;;
  cm5-8uart)
    cfg="rk3588_cpcm5_defconfig"
    dtb="rk3588-cpcm5-8uart.dtb"
    txt_config_file="config_cpcm5_8uart.txt"
    txt_extconf_file="extlinux_cpcm5_8uart.conf"
    ;;
  cpnano)
    cfg="rv1106_cpnano_defconfig"
    dtb="rv1106-cpnano.dtb"
    txt_config_file="config_cpnano.txt"
    txt_extconf_file="extlinux_cpnano.conf"
    RV1106="1"
    export ARCH=arm
    ;;
  *)
    echo "Usage: $0 {cpnano|cp4b|cm5-evb|cm5-evb-v11|cm5-minipc|cm5-notebook|cm5-8uart}" >&2
    exit 0
    ;;
esac

for dtb_f in $dtb
do
    rm -rf arch/arm64/boot/dts/rockchip/$dtb_f
done

if [ "$RV1106" == "1" ]; then
    GEN_DEBS="NO"
    ARCH=`uname -m`
    if [ "$ARCH" == "x86_64" ]; then
        export CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf-
        TOOLCHAIN_ARM32=$K_SRC/toolchain32uc/bin
        export PATH=$TOOLCHAIN_ARM32:$PATH
    fi
    if [ "$ARCH" == "aarch64" ]; then
        export CROSS_COMPILE=arm-linux-gnueabi-
    fi
    make ARCH=arm LOCALVERSION= $cfg
    make ARCH=arm LOCALVERSION= -j8
    cp arch/arm/boot/zImage vmlinuz
    for dtb_f in $dtb
    do
        cp arch/arm/boot/dts/$dtb_f .
    done

    rm -rf out_modules
    mkdir -p out_modules

    cd $K_SRC
    make ARCH=arm modules_install INSTALL_MOD_PATH=out_modules
    rm -rf out
    mkdir -p out/extlinux
    cp vmlinuz out/
    #cp demo-cfgs/cmdline.txt out/cmdline.txt
    #cp demo-cfgs/$txt_config_file out/config.txt
    cp demo-cfgs/$txt_extconf_file out/extlinux/extlinux.conf
    cp demo-cfgs/initrd32.img out/initrd32.img
else
    make ARCH=arm64 LOCALVERSION= $cfg
    make ARCH=arm64 LOCALVERSION= -j8
    make ARCH=arm64 LOCALVERSION= modules -j8
    cp arch/arm64/boot/Image.gz vmlinuz
    cp arch/arm64/boot/Image Image
    for dtb_f in $dtb
    do
        cp arch/arm64/boot/dts/rockchip/$dtb_f .
    done

    rm -rf out_modules
    mkdir -p out_modules

    cd $K_SRC
    make ARCH=arm64 modules_install INSTALL_MOD_PATH=out_modules
    rm -rf out
    mkdir -p out/extlinux
    cp vmlinuz out/
    cp Image out/
    cp demo-cfgs/cmdline.txt out/cmdline.txt
    cp demo-cfgs/$txt_config_file out/config.txt
    cp demo-cfgs/$txt_extconf_file out/extlinux/extlinux.conf
    cp demo-cfgs/initrd.img out/initrd.img
fi

cd $K_SRC/out_modules/lib/modules/5.10.110
unlink source
unlink build
ln -sf /usr/src/linux-headers-5.10.110/ build
ln -sf /usr/src/linux-headers-5.10.110/ source
cd $K_SRC/out_modules/lib/
tar -czf ../../modules.tar.gz *

cd $K_SRC
for dtb_f in $dtb
do
    cp $dtb_f out/
done
cp modules.tar.gz out/

case "$GEN_DEBS" in
    [yY][eE][sS]|[yY])
        echo "Create coolpi kernel deb packages..."
	;;
    *)
	exit 0
	;;
esac

KDEB_DIR=$K_SRC/.tmp
rm -rf $KDEB_DIR
mkdir -m 0755 -p $KDEB_DIR/DEBIAN
cd $KDEB_DIR
mkdir -p boot/firmware && cp -a $K_SRC/out/* boot/firmware/
mkdir -p usr/lib && cp -a $K_SRC/out_modules/lib/modules usr/lib/
cp $K_SRC/demo-cfgs/initrd.img boot/firmware/
cat << EOF > $KDEB_DIR/DEBIAN/control
Package: linux-image-5.10.110
Source: linux-5.10.110
Version: 100
Architecture: arm64
Maintainer: coolpi <coolpi@coolpi>
Section: kernel
Priority: optional
Homepage: https://www.kernel.org/
Description: Linux kernel, version 100
 This package contains the Linux kernel, modules and corresponding other
 files, version: 5.10.110.
EOF
cd $KDEB_DIR && dpkg-deb "--root-owner-group" --build . ..

cd $K_SRC
KHDEB_DIR=$K_SRC/.tmp_h
HDR_DIR=$KHDEB_DIR/usr/src/linux-headers-5.10.110
rm -rf $KHDEB_DIR
mkdir -m 0755 -p $KHDEB_DIR/DEBIAN
mkdir -p $HDR_DIR
cat << EOF > $KHDEB_DIR/DEBIAN/control
Package: linux-headers-5.10.110
Source: linux-5.10.110
Version: 100
Architecture: arm64
Maintainer: coolpi <coolpi@coolpi>
Section: kernel
Priority: optional
Homepage: https://www.kernel.org/
Description: Linux kernel headers, version 100
 This package contains the Linux kernel, modules and corresponding other
 files, version: 5.10.110.
EOF
(
    cd $K_SRC
    find . arch/arm64 -maxdepth 1 -name Makefile\*
    find include scripts -type f -o -type l
    find arch/arm64 -name Kbuild.platforms -o -name Platform
    find $(find arch/arm64 -name include -o -name scripts -type d) -type f
) > .hdrsrcfiles
tar -c -f - -C $K_SRC -T $K_SRC/.hdrsrcfiles | tar -xf - -C $HDR_DIR
cp $K_SRC/.config $HDR_DIR
cp $K_SRC/Module.symvers $HDR_DIR
rm $K_SRC/.hdrsrcfiles
cd $KHDEB_DIR && dpkg-deb "--root-owner-group" --build . ..

exit 0
