CC=gcc
CFLAGS=-Wall -Werror
LDFLAGS=
TARGET=user@10.1.1.161:~

all: kvlib.o readtest testbench_wear testbench testbench_flush_get testbench_flush_set testmincheol testmincheol_gc print gc set get del format testbench_data

kvlib.o: kvlib.c
	$(CC) $(CFLAGS) -c $^ -Wall -o $@ $(LDFLAGS)
	
testbench: testbench.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

testbench_wear: testbench_wear.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

readtest: readtest.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

testbench_flush_set: testbench_flush_set.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

testbench_flush_get: testbench_flush_get.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

testbench_data: testbench_data.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

testmincheol: testmincheol.c kvlib.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

testmincheol_gc: testmincheol_gc.c kvlib.o
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
	scp -r p6_flush_test.sh roadhamer.sh all.sh\
			p6_wear_test.sh \
			testbench_wear readtest \
			launch_flash_simulator.sh insert_mod.sh mount.sh \
			plot_mount.py plot.py read_write.sh \
			testbench testbench_flush_set testbench_flush_get \
			testbench_data testmincheol testmincheol_gc \
			print gc set get del format \
			$(TARGET)
 
clean:
	rm -rf *.o testbench_wear readtest testbench testbench_flush_set testbench_flush_get testbench_data testmincheol testmincheol_gc print gc set get del format
