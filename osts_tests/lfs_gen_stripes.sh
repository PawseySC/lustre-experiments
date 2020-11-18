#!/usr/bin/env bash
# Generate striped file 
if [ $# -ne 3 ]; then
  echo "Generate striped file on Luste filesystem, if file exists it will exit with an error message"
  echo "usage: $0 <file name> <stripe size> <num stripes>"
  exit 1
fi
fname=$1
if [ -f $fname ]; then
  echo "File ${fname} exists, remove file or select different file name"
  exit 1
fi
stripe_size=$2
num_stripes=$3
lfs setstripe -c $num_stripes -S $stripe_size $fname
if [ $? -ne 0 ]; then
  echo "Error creating striped file ${fname}, num stripes=${num_stripes}, stripe size=${stripe_size}"
  exit 1
fi
