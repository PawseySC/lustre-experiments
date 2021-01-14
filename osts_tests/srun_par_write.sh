#!/usr/bin/env bash
# Parallel write to striped file; generates a striped file and writes.
# In case the file exists it exits.
# Stripe size is computed as (file size) / (number of tasks)
# TODO: consider recomputing the file size as (number of stripes) x (stripe size)
# Example: ./srun_par_write.sh tmp-data/xxx $((111*10*1024*1024)) 4
#
# Author: Ugo Varetto
#
if [ $# -ne 3 ]; then
  echo "Parallel write, one stripe per process"
  echo "usage: $0 <file path> <file size>"
fi
fname=$1
filesize=$2
nodes=$SLURM_JOB_NUM_NODES
stripe_size=$(($filesize/$SLURM_NTASKS))
stripes=$SLURM_NTASKS
echo "Stripe size: ${stripe_size}"
echo "Num stripes: ${stripes}"
./lfs_gen_stripes.sh $fname $stripe_size $stripes
if [ $? -ne 0 ]; then
  echo "Error generating stripes"
  exit 1
fi
srun -N $nodes -n $stripes ./slurm_par_write_file.sh $fname $filesize 
