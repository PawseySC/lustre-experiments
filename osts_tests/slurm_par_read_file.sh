#!/usr/bin/env bash
# Reading from file in parallel,
# must be run from within SLURM
desc="Parallel read from file, one chunk per process"
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

dd if=$filename of=/dev/zero bs=$part_size count=1 ibs=$part_size skip=$SLURM_PROCID
if [ $? -ne 0 ]; then
  echo "Error running 'dd' command"
  exit 1
fi
