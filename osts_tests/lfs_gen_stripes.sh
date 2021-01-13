#!/usr/bin/env bash
# Generate striped file
# Author: Ugo Varetto 
if [ $# -ne 3 ]; then
  echo "Generate striped file on Luste filesystem"
  echo "usage: $0 <file name> <stripe size> <num stripes>"
  exit 1
fi
fname=$1
stripe_size=$2
num_stripes=$3
lfs setstripe -c $num_stripes -S $stripe_size $fname -i 0
if [ $? -ne 0 ]; then
  echo "Error creating striped file ${fname}, num stripes=${num_stripes}, stripe size=${stripe_size}"
  exit 1
fi
