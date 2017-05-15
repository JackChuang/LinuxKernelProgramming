#!/usr/bin/env python
import matplotlib.pyplot as plt
import csv
import sys
'''
 ./plot_mount.py input
'''

file_arr = ['10,10', '50,50','100,100', '1000,1000']
mount = 'mount'
X = [1, 2, 3, 4]
x = []
y = []
for num in file_arr:
    temp = []
    f = mount+num
    inf = open(f, 'r')
    lines = inf.readlines()
    for line in lines:
        temp.append(float(line.split()[1][2:-1]))
    x.append(num)
    y.append(sum(temp)/len(temp))

plt.bar(X, y,width=0.2,label='vanilla',edgecolor='black')
plt.legend()
plt.xticks(X, x)
plt.title('mount time comparison')
plt.xlabel('partition size')
plt.ylabel('time(s)')
plt.show()
