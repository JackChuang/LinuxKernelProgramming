#!/bin/bash

arr=("10,2" "50,2" "100,2" "1000,2")
ori="launch_flash_simulator.sh"


for var in "${arr[@]}"; do
    echo "removing prototype and nandsim..."
    rmmod prototype &> /dev/null 
    rmmod nandsim &> /dev/null 
    echo "current partition setting "$var

    nandsim="sudo modprobe nandsim first_id_byte=0x20 second_id_byte=0xaa third_id_byte=0x00 fourth_id_byte=0x15 parts=$var"

    echo $nandsim

    sed -i '30s/.*/'"$nandsim"'/' $ori # replace nandsim setting

    chmod +x $ori
    echo "===================================================================="
    ./launch_flash_simulator.sh
    ./insert_mod.sh
    #./print
    echo "testing mount time for partition "$var

    for i in `seq 1 10`; do
        ( { time  ./insert_mod.sh > /dev/null; } 2>&1 |grep real ) >> mount$var
    done
done

