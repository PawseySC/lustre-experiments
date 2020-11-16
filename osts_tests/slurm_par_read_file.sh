#!/usr/bin/env bash
if [ -z $SLURM_PROCID ]; then
  echo "script must be run from within SLURM"
  exit 1
fi
if [ $# -ne 2 ]; then
  echo "usage: $0 <file name> <part size>"
  exit 1
fi

file_name=$1
part_size=$2

dd if=$filename of=/dev/zero bs=$part_size count=1 ibs=$part_size skip=$SLURM_PROCID
