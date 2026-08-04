#!/bin/bash
clear
rm -f rank_scaling_sha256.csv
rnkiter=1
while [ $rnkiter -le 40 ]
do
dpu-upmem-dpurte-clang -DNR_TASKLETS=16 -DSTACK_SIZE_DEFAULT=2400 -O3 SHA256DPU.c -o SHA256DPU
gcc --std=c11 -DASYNCTRANS -DASYNCEXEC -DSHAHOST -DRANKITER=$rnkiter -O3 ranks_scaling_sha256.c -o ranks_scaling_sha256 `dpu-pkg-config --cflags --libs dpu`
./ranks_scaling_sha256
rnkiter=$(( $rnkiter +1 ))
done

rm -f SHA256DPU
rm -f ranks_scaling_sha256