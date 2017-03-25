#! /bin/sh
#
# jack.sh
# Copyright (C) 2017 user <user@debian>
#
# Distributed under terms of the MIT license.
#


#ssh jackchuang@10.1.1.142 make -C /home/jackchuang/Courses/LKP/p4/VM/linux-4.0.9/example
#sshfs jackchuang@10.1.1.142:/home/jackchuang/Courses/LKP/p4/VM/linux-4.0.9/example /home/user/temp
#scp jackchuang@10.1.1.142:/home/jackchuang/Courses/LKP/p4/VM/linux-4.0.9/example/example.ko .
make clean
make

killall p4
gcc p4.c -o p4
./p4 &

sudo umount -f /mnt 
sleep 1
sudo rmmod -f example
sleep 1
sudo insmod example.ko
sleep 3
sudo mount -t lwnfs null /mnt
#sudo mount -t lwnfs none /tmp
echo "\n\n"
echo "$ ls /mnt"
ls /mnt
echo "\n\n\ndone !!"
