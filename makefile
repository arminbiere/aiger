CFLAGS=-Wall -g
all: test aigtoaig aignm
test: aiger.o test.o
	$(CC) -o $@ test.o aiger.o
aigtoaig: aiger.o aigtoaig.o
	$(CC) -o $@ aigtoaig.o aiger.o
aignm: aiger.o aignm.o
	$(CC) -o $@ aignm.o aiger.o
test.o: test.c aiger.h
aigtoaig.o: aigtoaig.c aiger.h
aignm.o: aignm.c aiger.h
aiger.o: aiger.h aiger.c
clean:
	rm -f test aigtoaig aignm aiger.o log/*.log
