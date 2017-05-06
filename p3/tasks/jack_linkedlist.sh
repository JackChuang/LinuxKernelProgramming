#! /bin/sh
make clean
make
for files in "800" "600" "400" "200" "100" "60" "40" "20" "1000" "2000" "3000" "5000" "7000"
do
    /usr/bin/time -p -a -o time.log sudo ./lottery_system $files
    sleep 1
	rm -r linkedlist_$files
	mkdir linkedlist_$files
    sleep 1
	./parse.sh
    sleep 1
	mv *.log linkedlist_$files
    sleep 1
	mv *.cvs linkedlist_$files
    sleep 1
	mv result linkedlist_$files
    sleep 1
    cp $files linkedlist_$files
    sleep 1
done
