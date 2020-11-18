#!/usr/bin/env bash
if [ $# -ne 2 ]; then
  echo "Read from file in parallel, number of processes == number of stripes"
  echo "usage: $0 <file path>"
  exit 1
fi
fname=$1
if [ ! -f $fname ]; then
  echo "Error ${fname} does not exist"
  exit 1
fi
numprocs=lfs getstripe --stripe-count $fname
stripe_size=lfs getstripe --stripe-size $fname
#srun -N $num_stripes ./slurm_par_read_file.sh $fname $stripe_size 
