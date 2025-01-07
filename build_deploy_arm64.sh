if [ $# -lt 1 ]; then 
    echo "Usage: $0 <kernel-configuration> [<sdcard-boot-device> <sdcard-root-device>]"; 
    exit 1; 
fi

KERNEL_CONFIG=$1

num_processors=$(nproc)
#echo "Number of processors: $num_processors" 

echo "Configuring kernel build for $KERNEL_CONFIG"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- $KERNEL_CONFIG
if [ $? -ne 0 ]; then
    echo "Failed to configure kernel"
    exit 1
fi

KERNEL_NAME="kernel8"
# version string from CONFIG_LOCALVERSION
CONFIG_LOCALVERSION=$(sed -n 's/^CONFIG_LOCALVERSION=\(.*\)$/\1/p' .config)
if [ -n $CONFIG_LOCALVERSION ]; then
    CONFIG_LOCALVERSION=$(echo $CONFIG_LOCALVERSION | xargs)
    KERNEL_NAME="kernel${CONFIG_LOCALVERSION}"
fi
echo "Kernel name: $KERNEL_NAME"

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

if [ $# -eq 1 ]; then 
    exit 0; 
fi

if [ $# -ne 3 ]; then 
    echo "Usage: $0 <kernel-configuration> <sdcard-boot-device> <sdcard-root-device>"; 
    exit 1; 
fi

SDCARD_BOOT_DEVICE=$2
SDCARD_ROOT_DEVICE=$3

sudo mount /dev/$SDCARD_BOOT_DEVICE mnt/boot
if [ $? -ne 0 ]; then
    echo "Failed to mount boot partition"
    exit 1
fi
sudo mount /dev/$SDCARD_ROOT_DEVICE mnt/root
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
sudo cp -u -v arch/arm64/boot/Image mnt/boot/$KERNEL_NAME.img
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

