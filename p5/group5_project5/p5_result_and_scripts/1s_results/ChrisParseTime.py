#!/usr/bin/env python3
import argparse
import re
import glob
import sys
import tokenize
import csv


########## Begin
c_line=0
ff = range(1, 10+1)

#var1=None

timeDiff_1 = []
timeDiff_2 = []
timeDiff_3 = []

filecnt=0

for curr in ff:
    print('try '+ str(curr))
    try: 
        f = open(str(curr),'r')
        c_line = 0
    	#with open(curr,'r') as f :
        for line in f:
            if c_line == 0:
                var1 = int(line) #t[0]
                c_line += 1
            elif c_line == 1:
                #var2 = int(line)
                c_line += 1
            elif c_line == 2:
                var3 = int(line)
                c_line += 1
            elif c_line == 3:
                var4 = int(line)
                c_line += 1
            elif c_line == 4:
                var5 = int(line)
                c_line += 1
            else:
                print('Too many lines ')
            #end if
        #Do Math
        filecnt += 1
        diff1 = var5 - var1	# sleep latency
        diff2 = var4 - var3	# int latency
        diff3 = var5 - var4	# sche latency
        
        print('run '+ str(filecnt) +'\t\t1 '+str(diff1)+'\t2 '+str(diff2)+'\t3 '+str(diff3))
        
        timeDiff_1.append(diff1)
        timeDiff_2.append(diff2)
        timeDiff_3.append(diff3)
    except: 
        pass
	#Add diff1 to end of array timeDiff_1
	
#end for ff


#Do Avgerage avg(timeDiff_1)

curAvg1=0
curAvg2=0
curAvg3=0
for c in timeDiff_1:
	curAvg1 += c
#end for timeDiff_1
Aa1 = curAvg1/filecnt

for c in timeDiff_2:
	curAvg2 += c
#end for timeDiff_2
Aa2 = curAvg2/filecnt

for c in timeDiff_3:
	curAvg3 += c
#end for timeDiff_3
Aa3 = curAvg3/filecnt

#Time for printing to Person
print('Average \t1. '+str(Aa1)+'\t2. '+str(Aa2)+'\t3. '+str(Aa3))


#Now time to save to file!
filename ='jack_total.csv'
with open(filename,'a') as csvv:
    writer = csv.writer(csvv)
    writer.writerow(str(Aa1))
    writer.writerow(str(Aa2))
    writer.writerow(str(Aa3))

