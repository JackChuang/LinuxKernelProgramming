#! /bin/sh
make clean
make
for files in "800" "600" "400" "200" "100" "60" "40" "20" "1000" "2000" "3000" "5000" "7000"
do
    /usr/bin/time -p -a -o time.log sudo ./lottery_system $files
    sleep 1
	rm -r rb_$files
	mkdir rb_$files
    sleep 1
	./parse.sh
    sleep 1
	mv *.log rb_$files
	mv *.cvs rb_$files
	mv result rb_$files
    sleep 1
    cp $files rb_$files
    sleep 1
done
