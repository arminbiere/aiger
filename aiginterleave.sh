#!/bin/sh
aigtoaig -a $1 | \
awk '
/^aag /{print;n=$3;next}
{
  if (NR > n + 1) print;
  else {
    inputs[NR-1]=$1;
    if (NR == n+1) {
      for (i = 0; i < n; i++) {
	print inputs[i];
	print inputs[n/2 + 1];
      }
    }
  }
}' |aigtoaig $2
