#!/bin/sh
version=`cat VERSION`
name=aiger-$version
archive=/tmp/${name}.tar.gz
dir=/tmp/$name
rm -rf $dir
mkdir $dir
make -C doc/beyond1 2>/dev/null >/dev/null
cp -a configure.sh makefile.in $dir
cp -a VERSION README FORMAT LICENSE NEWS $dir
cp -a doc/beyond1/beyond1.pdf $dir
cp -a \
aigand.c aigbmc.c aigdd.c aiger.c aiger.h aigfuzz.c aigflip.c aigfuzz.h \
aigfuzzlayers.c aiginfo.c aigjoin.c aigmiter.c aigmove.c aignm.c aigor.c \
aigreset.c aigsim.c aigsplit.c aigstrip.c aigtoaig.c aigtoblif.c \
aigtobtor.c aigtocnf.c aigtodot.c aigtosmv.c aigunroll.c andtoaig.c \
bliftoaig.c simpaig.c simpaig.h smvtoaig.c soltostim.c wrapstim.c \
aigunconstraint.c aigdep.c \
$dir
cp -a mc.sh aigvis $dir
cp -ar examples $dir/
cd /tmp
rm -f $archive
tar zcf $archive $name
rm -rf $dir
ls -l $archive
