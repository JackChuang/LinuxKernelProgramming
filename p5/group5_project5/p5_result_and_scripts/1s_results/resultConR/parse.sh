#! /bin/bash

for i in {1..10..1}
do
	sed -i 's/.* time: \([0-9]*\)/\1/g' $i
	wc -l $i
	#if less remove
done


