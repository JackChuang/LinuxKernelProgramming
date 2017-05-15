#! /bin/sh
#
# jack.sh
# Copyright (C) 2017 user <user@debian>
#
# Distributed under terms of the MIT license.
#


#./launch_flash_simulator.sh
#./insert_mod.sh
#./format


./launch_flash_simulator.sh 
./insert_mod.sh 
#mincheol
./testbench
./readtest 
./testmincheol

# perf data
#for i in 1 1 1 1 1 1 11 1 1 1 1 1 1 
#do
#./read_write.sh 
#done
#gc 
#./testmincheol_gc

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
#./set key1 trash
#./set key1 new1
#./get key1
#./print
#sleep 3
#sleep 1
