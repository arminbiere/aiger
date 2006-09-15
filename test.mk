all: testaigtoaig
testaigtoaig: aiger.o testaigtoaig.o makefile
	$(CC) -o $@ testaigtoaig.o aiger.o
testaigtoaig.o: testaigtoaig.c aiger.h makefile
clean: testclean
testclean:
	rm -f log/*.smvfromaig
	rm -f log/*.aig log/*.big log/*.cig
	rm -f log/*.aig.gz log/*.big.gz log/*.cig.gz
	rm -f testaigtoaig
.PHONY: testclean
