CFLAGS=-Wall -g
all: test aigtoaig
test: aiger.o test.o
	$(CC) -o $@ test.o aiger.o
aigtoaig: aiger.o aigtoaig.o
	$(CC) -o $@ aigtoaig.o aiger.o
test.o: test.c aiger.h
aigtoaig.o: aigtoaig.c aiger.h
aiger.o: aiger.h aiger.c
clean:
	rm -f aiger.o log/*.log
