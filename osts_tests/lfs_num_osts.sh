#!/usr/bin/env bash
if [ $# -ne 1 ]; then
  echo "print number of OSTs, usage: $0 <path>"
  exit 1
fi
path=$1
osts=`lfs osts $path | tail -n 1 | cut -f 1 -d :`
if [ $? -ne 0 ]; then
  echo "Error invoking 'lfs osts' command"
  exit 1
fi
echo $osts
