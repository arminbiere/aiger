#!/bin/sh
aigtoaig -a $* | \
awk '
/^aag /{n=$3;next}
{
  if (NR > n + 1) print;
  else if (NR == n+1) {
  } else inputs[NR-1]=$1;
}'

