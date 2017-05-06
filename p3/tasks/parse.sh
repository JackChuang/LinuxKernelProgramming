#! /bin/sh
# Copyright (C) 2017 JackChuang<horenc@vt.edu>
# parsing LKP project3 resutls
#
cat /proc/lottery_event > msg.log
cat msg.log |grep DONE > fairness.log
cat msg.log |grep PICK > scalability_pick.log
cat msg.log |grep EN > scalability_en.log
cat msg.log |grep DE > scalability_de.log
cat msg.log |grep CONTEX > contextswitch.log
cat msg.log |grep WinnerPid > winner.log
cat winner.log |sed 's/.*TIME:\([0-9]*\)/\1/g' > winner.cvs
cat scalability_en.log |sed 's/.*TIME:\([0-9]*\)/\1/g' > en.cvs
cat scalability_de.log |sed 's/.*TIME:\([0-9]*\)/\1/g' > de.cvs

# debug
dmesg | tail -n 10 > dmesg.log

cat winner.cvs | awk '{sum+=$1} END {print "Avg time for who_is_winner() = ", sum/NR}' >> result 
echo -n "who_is_winner " >> result
echo -n `wc -l winner.cvs` >> result
echo -n " times" >> result
echo "" >> result

cat en.cvs | awk '{sum+=$1} END {print "Avg time for en() = ", sum/NR}' >> result 
echo -n "en " >> result
echo -n `wc -l en.cvs` >> result
echo -n " times" >> result
echo "" >> result

cat de.cvs | awk '{sum+=$1} END {print "Avg time for de() = ", sum/NR}' >> result 
echo -n "de " >> result
echo -n `wc -l de.cvs` >> result
echo -n " times" >> result
echo " " >> result
