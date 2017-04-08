#! /bin/bash

make clean
make
mkdir result
for i in {1..10..1}
do
	dmesg -c 
	insmod jprobe_template.ko
	sleep 1
	cyclictest -t1 -p 80 -i 1000000 -l 1 -n && rmmod jprobe_template
	dmesg |grep -1 "acti" |grep -v "pick_next_task_fair" > $i
	mv $i result
done

uname -a > result/target_version 
