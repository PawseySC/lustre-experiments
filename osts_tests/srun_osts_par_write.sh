#!/usr/bin/env bash
if [ $# -ne 2 ]; then
  echo "write to file in parallel, one ost per process"
  echo "usage: $0 <file path> <stipe size>"
fi
fname=$1
path=$(dirname $fname) 
osts=`./lfs_num_osts.sh $path`
stripe_size=$2
ret=exec ./lfs_gen_stripes.sh $fname $stripe_size $osts
#srun -N $osts ./slurm_par_gen_file.sh $fname $stripe_size 
