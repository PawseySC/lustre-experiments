#!/usr/bin/env bash
# Write to file in parallel from multiple processes,
# must be run from withing SLURM
desc="Write to file in parallel, one chunk per process"
if [ -z $SLURM_PROCID ]; then
  echo $desc
  echo "script must be run from within SLURM"
  exit 1
fi
if [ $# -ne 2 ]; then
  echo $desc
  echo "usage: $0 <file name> <part size>"
  exit 1
fi

file_name=$1
part_size=$2

dd if=/dev/zero of=$file_name bs=$part_size count=1 obs=$part_size seek=$SLURM_PROCID
if [ $? -ne 0 ]; then
  echo "Error running 'dd' command"
fi
