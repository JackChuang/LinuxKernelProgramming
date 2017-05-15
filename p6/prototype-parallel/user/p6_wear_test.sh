#! /bin/sh
#
# jack.sh
# Copyright (C) 2017 user <user@debian>
#
# Distributed under terms of the MIT license.
#

./launch_flash_simulator.sh 
./insert_mod.sh 
./format
./insert_mod.sh 
echo "======================================="
echo "wear test (use larger blk size like 50)"
echo "======================================="
./testbench_wear
./print
