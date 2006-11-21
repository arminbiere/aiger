include test.mk

CFLAGS=-Wall -g3

TARGETS=aigbmc aiginfo aignm aigsim aigstrip aigtoaig aigtoblif aigtocnf \
aigtosmv andtoaig bliftoaig smvtoaig soltostim aigdd wrapstim \
aiger.o simpaig.o

all: $(TARGETS)

aigbmc: aiger.o aigbmc.o simpaig.o makefile
	$(CC) $(CFLAGS) -o $@ aigbmc.o aiger.o simpaig.o
aigdd: aiger.o aigdd.o makefile
	$(CC) $(CFLAGS) -o $@ aigdd.o aiger.o
aiginfo: aiger.o aiginfo.o makefile
	$(CC) $(CFLAGS) -o $@ aiginfo.o aiger.o
aignm: aiger.o aignm.o makefile
	$(CC) $(CFLAGS) -o $@ aignm.o aiger.o
aigsim: aiger.o aigsim.o makefile
	$(CC) $(CFLAGS) -o $@ aigsim.o aiger.o
aigstrip: aiger.o aigstrip.o makefile
	$(CC) $(CFLAGS) -o $@ aigstrip.o aiger.o
aigtoaig: aiger.o aigtoaig.o makefile
	$(CC) $(CFLAGS) -o $@ aigtoaig.o aiger.o
aigtoblif: aiger.o aigtoblif.o makefile
	$(CC) $(CFLAGS) -o $@ aigtoblif.o aiger.o
aigtocnf: aiger.o aigtocnf.o makefile
	$(CC) $(CFLAGS) -o $@ aigtocnf.o aiger.o
aigtosmv: aiger.o aigtosmv.o makefile
	$(CC) $(CFLAGS) -o $@ aigtosmv.o aiger.o
andtoaig: aiger.o andtoaig.o makefile
	$(CC) $(CFLAGS) -o $@ andtoaig.o aiger.o
bliftoaig: aiger.o bliftoaig.o makefile
	$(CC) $(CFLAGS) -o $@ bliftoaig.o aiger.o
smvtoaig: aiger.o smvtoaig.o makefile
	$(CC) $(CFLAGS) -o $@ smvtoaig.o aiger.o
soltostim: aiger.o soltostim.o makefile
	$(CC) $(CFLAGS) -o $@ soltostim.o aiger.o
wrapstim: aiger.o wrapstim.o makefile
	$(CC) $(CFLAGS) -o $@ wrapstim.o aiger.o

aigbmc.o: aiger.h simpaig.h aigbmc.c makefile
aigdd.o: aiger.h aigdd.c makefile
aiger.o: aiger.h aiger.c makefile
aiginfo.o: aiginfo.c aiger.h makefile
aignm.o: aignm.c aiger.h makefile
aigsim.o: aigsim.c aiger.h makefile
aigstrip.o: aigstrip.c aiger.h makefile
aigtoaig.o: aigtoaig.c aiger.h makefile
aigtoblif.o: aigtoblif.c aiger.h makefile
aigtocnf.o: aigtocnf.c aiger.h makefile
aigtosmv.o: aigtosmv.c aiger.h makefile
andtoaig.o: andtoaig.c aiger.h makefile
bliftoaig.o: bliftoaig.c aiger.h makefile
simpaig.o: simpaig.h simpaig.c makefile
smvtoaig.o: smvtoaig.c aiger.h makefile
soltostim.o: soltostim.c aiger.h makefile
wrapstim.o: wrapstim.c aiger.h makefile

clean:
	rm -f *.o 
	rm -f $(TARGETS)

.PHONY: all clean
