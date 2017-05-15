#!/bin/bash
set -euo pipefail

MOD_NAME=prototype.ko
FLASH_PARTITION_INDEX=0
DEV_NAME=/dev/lkp_kv
DEV_MAJOR=100

# try to remove the module in case it is already loaded
sudo rmmod $MOD_NAME &> /dev/null || true

# try to delete the device file in case it already exists
sudo rm -rf $DEV_NAME &> /dev/null || true

sudo insmod $MOD_NAME MTD_INDEX=$FLASH_PARTITION_INDEX
sudo mknod $DEV_NAME c $DEV_MAJOR 0

