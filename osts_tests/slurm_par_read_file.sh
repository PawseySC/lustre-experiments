#!/usr/bin/env bash
# Parallel file read, must be run from within SLURM.
# The per-process part size is computed as (file size) / (num tasks)
# Author: Ugo Varetto
desc="Parallel file read, one chunk per process"
if [ -z $SLURM_PROCID ]; then
  echo $desc
  echo "script must be run from within SLURM"
  exit 1
fi
if [ $# -ne 1 ]; then
  echo $desc
  echo "usage: $0 <file name>"
  exit 1
fi

file_name=$1
filesize=`stat -c %s ${file_name}`
if [ $? -ne 0 ]; then
  echo "Error retrieving ${file_name} size"
  exit 1
fi
part_size=$(($filesize/$SLURM_NTASKS))

#read from file at location $SLURM_PROCID x $part_size
ddout=`dd if=$file_name of=/dev/zero bs=$part_size count=1 skip=$SLURM_PROCID`
if [ $? -ne 0 ]; then
  echo "Error running 'dd' command"
  exit 1
fi
#TODO: extract bandwidth number
echo "${ddout}"
