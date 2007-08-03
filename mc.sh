#!/bin/sh
#
# Overview:
#
# This is a simple bounded model checker using the utilities provided by the
# AIGER library.  It is an illustration on how these utilities can be used
# and also defines the model checking competition input output requirements
# by example.
#
# Installation:
#
# To install this model checker put it together with the utilities listed
# below in 'aigertools' to the same directory and adapt the name of the
# executable of a SAT solver below.  The SAT solver should conform to the
# SAT competition input and output requirements.
#
if [ x"$SATSOLVER" = x ]
then
  satsolver="picosat"
else
  satsolver="$SATSOLVER"
fi

# Todo:
#
# We need to add an k-induction based unbounded model checker to also
# have an example for the ouput never being able to produce a one.  This
# should be included in 'aigbmc'.
#

# No changes are required below this line.
#
aigertools="aigbmc aigtocnf soltostim wrapstim"

msg () {
  [ $verbose = yes ] && echo "[mc.sh] $*" 1>&2
}

cleanup () {
  cd /tmp
  if [ $debug = yes ]
  then
    msg "keeping temporary files in $tmp"
  else
    msg "removing temporary files"
    rm -rf $tmp
  fi
}

die () {
  echo "*** [mc.sh] $*" 1>&2
  exit 1
}

input=""
debug=no
verbose=no
maxk=100

while [ $# -gt 0 ]
do
  case $1 in
    -v) verbose=yes;;
    -d) debug=yes;;
    -h)
cat << EOF
usage: mc.sh [-h][-v][-d][<model>]
-h      print this command line option summary
-v      increase verbose level
-d      switch on debugging
<model> model in AIGER format
EOF
exit 0
;;
    -*) die "invalid command line option '$1'";;
    *)
       [ x"$input" = x ] || die "more than one model"
       [ -f "$1" ] || die "invalid file '$1'"
       input="$1"
       ;;
  esac
  shift
done

if [ $debug = yes ]
then
  tmp=/tmp/mc.sh
  rm -rf $tmp || exit 1
else
  tmp=/tmp/mc.sh-$$
fi

trap 'cleanup' 0 1 2 3 6 9 14 15

msg "setting up temporary directory '$tmp'"
mkdir $tmp || exit 1
mkdir $tmp/bin

model=$tmp/model
if [ x"$input" = x ]
then
  msg "reading model from <stdin>"
  cat $input > $model || exit 1
else
  msg "copying $input"
  cp $input $model || exit 1
fi

basedir="`dirname $0`"
[ x"$basedir" = x ] && die "empty argv[0]"
[ x"$basedir" = x. ] && basedir="`pwd`"
cd $basedir || exit 1

for tool in $satsolver $aigertools
do
  found=no
  for d in `echo "$basedir:$PATH" | sed -e 's,:, ,g'`
  do
    [ -x $d/$tool ] || continue
    found=yes
    break
  done
  [ $found = no ] && \
  die "could not find '$tool' in '$basedir' nor in PATH"
  msg "found '$d/$tool'"
  ln -s $d/$tool $tmp/bin/$tool || exit 1
done

PATH=$tmp/bin:$PATH

[ $verbose = yes ] && verboseoption="-v"

k=0
msg "maximum bound $maxk"
found=no
#while [ $k -le $maxk ]
while true
do
  expansion=$tmp/expansion.aig
  msg "$k expanding"
  aigbmc $verboseoption $k $model $expansion || exit 1
  msg "$k converting"
  cnf=$tmp/cnf
  aigtocnf $expansion $cnf || exit 1
  msg "$k $satsolver"
  solution=$tmp/solution
  $satsolver $cnf 1>$solution
  exitcode="$?"
  #msg "$k exit code $exitcode"
  case "$exitcode" in
    10) found=yes; break;;
    20) ;;
    *) die "'$satsolver' returns exit code 0 in iteration $k";;
  esac
  k=`expr $k + 1`
done

if [ $found = yes ]
then
  echo 1
  msg "translating"
  estim=$tmp/expanded.stim
  soltostim $expansion $solution > $estim
  msg "wrapping"
  wrapstim $model $expansion $k $estim || exit 1
else
  echo x
fi

exit 0
