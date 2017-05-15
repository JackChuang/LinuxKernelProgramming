#! /bin/sh
#
# jack.sh
# Copyright (C) 2017 user <user@debian>
#
# Distributed under terms of the MIT license.
#
./launch_flash_simulator.sh 
./insert_mod.sh 

echo "roadhammer test"
echo "roadhammer test"
echo "roadhammer test"
./format
#./print
./testbench
./readtest 
./testmincheol
#./print
