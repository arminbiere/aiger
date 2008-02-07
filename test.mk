all: testaigtoaig testsimpaig
testaigtoaig: aiger.o testaigtoaig.o makefile
	$(CC) $(CFLAGS) -o $@ testaigtoaig.o aiger.o
testsimpaig: simpaig.o testsimpaig.o makefile
	$(CC) $(CFLAGS) -o $@ testsimpaig.o simpaig.o
testaigtoaig.o: testaigtoaig.c aiger.h makefile
testsimpaig.o: testsimpaig.c simpaig.h makefile
clean: testclean
testclean:
	rm -f log/*.smvfromaig
	rm -f log/*.aig log/*.aag
	rm -f log/*.aig.gz log/*.aag.gz
	rm -f log/*.log log/*.err
	rm -f testaigtoaig testsimpaig
.PHONY: testclean
