#!/usr/bin/env bash
# Parallel read, set num processes == number of stripes
# Per-process part size will be computed as (file size) / (num tasks)
# Example ./srun_par_read.sh tmp-data/xxx 4
# Author: Ugo Varetto

if [ $# -ne 2 ]; then
  echo "Parallel read from file, per-process chunk size == (file size) / (num processes)"
  echo "usage: $0 <file path> <num nodes>"
  exit 1
fi
fname=$1
if [ ! -f $fname ]; then
  echo "Error ${fname} does not exist"
  exit 1
fi
num_parts=$SLURM_NTASKS
nodes=$2
srun -N $nodes -n $num_parts ./slurm_par_read_file.sh $fname
