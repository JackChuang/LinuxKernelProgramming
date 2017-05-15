CC=gcc
CFLAGS=-Wall -Werror
LDFLAGS=
TARGET=root@10.1.1.169:~

all: kvlib.o testbench testbench_data readtest testmincheol print gc set get del format

kvlib.o: kvlib.c
	$(CC) $(CFLAGS) -c $^ -Wall -o $@ $(LDFLAGS)
	
testbench: testbench.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

testbench_data: testbench_data.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)


testmincheol: testmincheol.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

readtest: readtest.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

print: print.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

gc: gc.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

set: set.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

get: get.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

del: del.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

format: format.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)


install: all
	scp -r launch_flash_simulator.sh insert_mod.sh readtest testbench testbench_data testmincheol print gc set get del format $(TARGET)

clean:
	rm -rf *.o testbench testmincheol print gc set get del readtest format testbench_data
