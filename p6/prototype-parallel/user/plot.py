#!/usr/bin/env python
import matplotlib.pyplot as plt
import csv
import sys
'''
 ./plot.py input.csv
'''
with open(sys.argv[1], 'r') as inf:
    L = []
    lines = csv.reader(inf)
    for row in lines:
        L.append(row)

dic  = {}

for item in L:
    if(int(item[0][1:-1]) in dic):
        dic[int(item[0][1:-1])].append(float(item[2]))
    else:
        dic[int(item[0][1:-1])] = [float(item[2])]        
k_list = []
v_list = []   

for k,v in dic.iteritems():
   k_list.append(k)
   v_list.append(sum(v)/len(v))

X = [1,2,3,4]

plt.bar(X,sorted(v_list),width=0.2,label='vanilla',edgecolor='black')
plt.legend()
plt.xticks(X, sorted(k_list))
plt.title('read comparison')
plt.xlabel('read times')
plt.ylabel('time(ms)')
plt.show()
