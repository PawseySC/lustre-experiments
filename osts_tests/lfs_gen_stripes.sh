#!/usr/bin/env bash
if [ $# -ne 3 ]; then
  echo "usage: $0 <file name> <stripe size> <num stripes>"
  exit 1
fi
fname=$1
if [ -f $fname ]; then
  rm $fname
fi
stripe_size=$2
num_stripes=$3
lfs setstripe -c $num_stripes -S $stripe_size $fname 
