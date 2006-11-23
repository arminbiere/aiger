#!/bin/sh

# A SAT solver conforming to the SAT copetition output format.
#
solver="picosat"

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

while [ $# -gt 0 ]
do
  case $1 in
    -v) verbose=yes;;
    -d) debug=yes;;
    -h)
cat << EOF
usage: mc.sh [-h][-v][-d][<model>]
EOF
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

basedir="`dirname $0`"
[ x"$basedir" = x ] && die "empty argv[0]"
[ x"$basedir" = x. ] && basedir="`pwd`"
cd $basedir || exit 1

for tool in $solver aigbmc aigtocnf soltostim wrapstim
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

model=$tmp/model.aig
if [ x"$input" = x ]
then
  msg "reading model from <stdin>"
  cat $input > $model || exit 1
else
  msg "copying $input to $model"
  cp $input $model || exit 1
fi
