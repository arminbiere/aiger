#!/bin/sh
debug=no
die () {
  echo "*** configure.sh: $*" 1>&2
  exit 1
}
usage () {
  echo "usage: [CC=compile] [CFLAGS=cflags] configure.sh [-h][-hg]"
  exit 0
}
wrn () {
  echo "###########" 1>& 2
  echo "########### WARNING : $*" 1>& 2
  echo "###########" 1>& 2
}
msg () {
  echo "[configure.sh] $*" 1>& 2
}
while [ $# -gt 0 ]
do
  case $1 in
    -h|--help) usage;;
    -g) debug=yes;;
    *) die "invalid command line option '$1' (try '-h')";;
  esac
  shift
done
if [ x"$CC" = x ] 
then 
  msg "using gcc as default compiler"
  CC=gcc
else
  msg "using $CC as compiler"
fi
if [ x"$CFLAGS" = x ]
then
  msg "using default compilation flags"
  case x"$CC" in
    xgcc*)
      CFLAGS="-Wall"
      if [ $debug = yes ]
      then
	CFLAGS="-g"
      else
	CFLAGS="-O3 -DNDEBUG"
      fi
      ;;
    *)
      if [ $debug = yes ]
      then
	CFLAGS="-g"
      else
	CFLAGS="-O -DNDEBUG"
      fi
      ;;
  esac
else
  msg "using custom compilation flags"
fi

PICOSAT=no
if [ -d ../picosat ]
then
  if [ -f ../picosat/picosat.h ]
  then
    if [ -f ../picosat/picosat.o ]
    then
      if [ -f ../picosat/VERSION ] 
      then
	PICOSATVERSION="`cat ../picosat/VERSION`"
	if [ $PICOSATVERSION -lt 953 ]
	then
	  wrn "out-dated version $PICOSATVERSION in '../picosat/' (need at least 953 for 'aigbmc')"
	else
	  msg "found PicoSAT version $PICOSATVERSION in '../picosat'"
	  AIGBMCTARGET="aigbmc"
	  msg "using '../picosat/picosat.h', '../picosat/picosat.o' for 'aigbmc'"
	  PICOSAT=yes
	fi
      else
        wrn "can not find '../picosat/VERSION' (missing for 'aigbmc')"
      fi
    else
    wrn \
      "can not find '../picosat/picosat.o' object file (no 'aigbmc' target)"
    fi
  else
    wrn "can not find '../picosat/picosat.h' header (no 'aigbmc' target)"
  fi
else
  wrn "can not find '../picosat' directory (no 'aigbmc' target)"
fi

LINGELING=no
if [ -d ../lingeling ]
then
  if [ -f ../lingeling/lingeling.h ]
  then
    if [ -f ../picosat/liblgl.a ]
    then
      msg "using '../lingeling/liblgl.a' for 'aigbmc'"
      LINGELING=yes
    else
      wrn "can not find '../lingeling/liblgl.a' library"
    fi
  else
    wrn "can not find '../lingeling/lingeling.h' header"
  fi
else
  wrn "can not find '../lingeling' directory"
fi


if [ $PICOSAT = yes -o $LINGELING = yes ]
then
  AIGBMCTARGET="aigbmc"
else
  wrn "no proper '../lingeling' nor '../picosat' (will not build 'aigbmc')"
fi

msg "compiling with: $CC $CFLAGS"
rm -f makefile
sed \
  -e "s/@CC@/$CC/" \
  -e "s/@CFLAGS@/$CFLAGS/" \
  -e "s/@AIGBMCTARGET@/$AIGBMCTARGET/" \
  makefile.in > makefile
