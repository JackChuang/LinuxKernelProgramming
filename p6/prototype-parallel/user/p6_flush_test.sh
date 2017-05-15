#! /bin/sh
#
# jack.sh
# Copyright (C) 2017 user <user@debian>
#
# Distributed under terms of the MIT license.
#
./launch_flash_simulator.sh 
./insert_mod.sh 

# flush
./format
./testbench_flush_set
echo "======="
echo "remount"
echo "======="
#./print
./insert_mod.sh
echo "========"
echo "get keys"
echo "========"
./testbench_flush_get
#./print
