#!/usr/bin/env bash
# BSD 3-Clause License
#
# Copyright (c) 2020, Commonwealth Scientific and Industrial Research 
# Organisation (CSIRO) and The Pawsey Supercomputing Centre
#
# Author: Ugo Varetto
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
ARGC=$#
if [ $ARGC -ne 4 ]
then
  echo "Usage: $0 <location> <file size in MiB> <stripe count>"
prefix=${1%/}
file_size=$2
MB=$((2**20))
stripe_count=$3
stripe_size=$((file_size*MB/stripe_count))
dir_name="$prefix/striped_${file_size}MB_$stripe_count"
num_files=4
if [ ! -d $dir_name ]
then
  mkdir $dir_name
fi

for f in $(seq 1 $num_files)
do
  fpath="$dir_name/${file_size}MB_$f"
  if [ -f $fpath ]
  then
   rm $fpath
  fi
  lfs setstripe $fpath -S $stripe_size -c $stripe_count
  dd if=/dev/zero of=$fpath bs="${file_size}MB" count=1 > /dev/null 2>&1 &
done 
wait

