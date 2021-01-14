#!/usr/bin/env bash
# Parallel write from multiple processes; must be run from withing SLURM
# size part is computed as: file size / number of tasks
# remainder of file size / number of tasks should be zero but no error is
# currently reported if it's not.
# Script outputs the associated task id; task ids maps to stripe indexes.
#
# Author: Ugo Varetto
#

desc="Parallel write to file, one chunk per process"
if [ -z $SLURM_PROCID ]; then
  echo $desc
  echo "script must be run from within SLURM"
  exit 1
fi
if [ $# -ne 2 ]; then
  echo $desc
  echo "usage: $0 <file name> <file size>"
  exit 1
fi

file_name=$1
filesize=$2
part_size=$(($filesize/$SLURM_NTASKS))
byte_offset=$(($SLURM_PROCID * $part_size))
block_offset=$SLURM_PROCID
#echo "Offset: ${byte_offset}"
ddout=`dd if=/dev/zero of=$file_name bs=$part_size count=1 oflag=nonblock conv=notrunc seek=$block_offset`
if [ $? -ne 0 ]; then
  echo "Error running 'dd' command"
  exit 1
fi
#TODO: extract bandwidth number
echo "${ddout}"

