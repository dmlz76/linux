if [ $# -ne 3 ]; then 
    echo "Usage: $0 <kernel-configuration> <sdcard-boot-device> <sdcard-root-device>"; 
    exit 1; 
fi

num_processors=$(nproc)
echo "Number of processors: $num_processors" 

echo "Configuring kernel build for $1"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- $1
if [ $? -ne 0 ]; then
    echo "Failed to configure kernel"
    exit 1
fi

echo "Building kernel"
make -j $num_processors ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules dtbs
if [ $? -ne 0 ]; then
    echo "Failed to build kernel"
    exit 1
fi

if [ ! -d mnt ]; then
    mkdir mnt
fi
if [ ! -d mnt/boot ]; then
    mkdir mnt/boot
fi
if [ ! -d mnt/root ]; then
    mkdir mnt/root
fi

sudo mount /dev/$2 mnt/boot
if [ $? -ne 0 ]; then
    echo "Failed to mount boot partition"
    exit 1
fi
sudo mount /dev/$3 mnt/root
if [ $? -ne 0 ]; then
    echo "Failed to mount root partition"
    sudo umount mnt/boot
    exit 1
fi

function cleanup {
    sudo umount mnt/boot
    sudo umount mnt/root
}

echo "Installing kernel"
sudo env PATH=$PATH make -j $num_processors ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH=mnt/root modules_install
if [ $? -ne 0 ]; then
    echo "Failed to install kernel modules"
    cleanup
    exit 1
fi

echo "Copying kernel"
sudo cp -u -v arch/arm64/boot/Image mnt/boot/kernel8-16k-dmlz.img
if [ $? -ne 0 ]; then
    echo "Failed to copy kernel"
    cleanup
    exit 1
fi

echo "Copying Broadcom dtbs"
sudo cp -u -v arch/arm64/boot/dts/broadcom/*.dtb mnt/boot/
if [ $? -ne 0 ]; then
    echo "Failed to copy Broadcom dtbs"
    cleanup
    exit 1
fi

echo "Copying verlays"
sudo cp -u -v arch/arm64/boot/dts/overlays/*.dtb* mnt/boot/overlays/
if [ $? -ne 0 ]; then
    echo "Failed to copy overlays dtbs"
    cleanup
    exit 1
fi

echo "Copying overlays README"
sudo cp -u -v arch/arm64/boot/dts/overlays/README mnt/boot/overlays/
if [ $? -ne 0 ]; then
    echo "Failed to copy overlays README"
    cleanup
    exit 1
fi

cleanup

