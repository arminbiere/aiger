CFLAGS=-Wall -g
all: aigtoaig aignm andtoaig smvtoaig aigstrip test
aigtoaig: aiger.o aigtoaig.o makefile
	$(CC) -o $@ aigtoaig.o aiger.o
aignm: aiger.o aignm.o makefile
	$(CC) -o $@ aignm.o aiger.o
aigstrip: aiger.o aigstrip.o makefile
	$(CC) -o $@ aigstrip.o aiger.o
andtoaig: aiger.o andtoaig.o makefile
	$(CC) -o $@ andtoaig.o aiger.o
smvtoaig: aiger.o smvtoaig.o makefile
	$(CC) -o $@ smvtoaig.o aiger.o
test: aiger.o test.o makefile
	$(CC) -o $@ test.o aiger.o
aiger.o: aiger.h aiger.c makefile
aignm.o: aignm.c aiger.h makefile
aigstrip.o: aigstrip.c aiger.h makefile
aigtoaig.o: aigtoaig.c aiger.h makefile
andtoaig.o: andtoaig.c aiger.h makefile
smvtoaig.o: smvtoaig.c aiger.h makefile
test.o: test.c aiger.h makefile
clean:
	rm -f *.o 
	rm -f log/*.aig log/*.big log/*.cig
	rm -f log/*.aig.gz log/*.big.gz log/*.cig.gz
	rm -f aigtoaig andtoaig aignm aigstrip smvtoaig test
