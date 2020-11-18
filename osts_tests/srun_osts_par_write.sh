#!/usr/bin/env bash
if [ $# -ne 2 ]; then
  echo "Write to file in parallel, one stripe per OST, one proces per stripe"
  echo "usage: $0 <file path> <stipe size>"
fi
fname=$1
path=$(dirname $fname) 
osts=`./lfs_num_osts.sh $path`
stripe_size=$2
./lfs_gen_stripes.sh $fname $stripe_size $osts
if [ $? -ne 0 ]; then
  echo "Error generating stripes"
  exit 1
fi
#srun -N $osts ./slurm_par_gen_file.sh $fname $stripe_size 
