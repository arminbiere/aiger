#!/bin/sh
version=`cat VERSION`
name=aiger-$version
archive=/tmp/${name}.tar.xz
dir=/tmp/$name
if [ -d $dir ]
then 
  rm -f $dir/*
else
  mkdir $dir
fi
cp -a VERSION $dir
cp -a aiger.c aiger.h VERSION $dir
cat >$dir/README<<EOF
This is the minimal library only source release of AIGER.
EOF
cat >$dir/makefile.in<<EOF
all:
	@COMPILE@ -c aiger.c
clean:
	rm -f makefile aiger.o
EOF
cat >$dir/configure<<EOF
#!/bin/sh
COMPILE="gcc -Wall -O3 -DNDEBUG"
while [ \$# -gt 0 ]
do
  case \$1 in
    -h) echo "usage: ./configure [-g]"; exit 0;;
    -g) COMPILE="gcc -Wall -g";;
    *) echo "configure: error: \$*" 1>&2; exit 1;;
  esac
  shift
done
sed -e "s,@COMPILE@,\$COMPILE," makefile.in > makefile
EOF
chmod 755 $dir/configure
cd /tmp
rm -f $archive
tar cfJ $archive $name
rm -rf $dir
ls -l $archive
