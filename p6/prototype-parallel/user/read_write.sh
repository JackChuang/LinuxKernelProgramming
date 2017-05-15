#!/bin/bash

rm *.csv # clean old data

rw_times=(100 500 1000 2000)

for var in "${rw_times[@]}"; do
    for i in `seq 1 5`; do 
        ./testbench_data $var;
    done
done   


