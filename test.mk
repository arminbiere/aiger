all: testaigtoaig
testaigtoaig: aiger.o testaigtoaig.o makefile
	$(CC) -o $@ testaigtoaig.o aiger.o
testaigtoaig.o: testaigtoaig.c aiger.h makefile
clean: testclean
testclean:
	rm -f log/*.smvfromaig
	rm -f log/*.aig log/*.aag
	rm -f log/*.aig.gz log/*.aag.gz
	rm -f log/*.log log/*.err
	rm -f testaigtoaig
.PHONY: testclean
