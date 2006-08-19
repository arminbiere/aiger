CFLAGS=-Wall -g
test: aiger.o test.o
	$(CC) -o $@ test.o aiger.o
test.o: test.c aiger.h
aiger.o: aiger.h aiger.c
clean:
	rm -f aiger.o
