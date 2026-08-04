#include <stdio.h>
#include <mram.h>
#include <stdlib.h>
#include "common.h"
#include <stdint.h>
#include <stdlib.h>
#include <mram.h>
#include <perfcounter.h>
#include <defs.h>
#include <alloc.h>
#include <mutex.h>
#include <barrier.h>

#define CACHE_SIZE 1024
#define MSG_BUFFER_SIZE MESSAGE_SIZE*NR_TASKLETS


__mram char msgs[MSG_BUFFER_SIZE];
__host uint32_t hash_digests[CACHE_SIZE];
__host uint32_t nr_instructions=0;
__host uint32_t transferTime=0;
__host uint32_t numberOfTransfer=0;
__host uint32_t SHAtime=0;


int main()
{		
	int initTime=0;
	if(me()==0){ initTime = perfcounter_config(COUNT_CYCLES, false); }
	char  cache[CACHE_SIZE];
	printf("%d\n",msgs[0]);
	
	for(int i=0;i<(MESSAGE_SIZE/CACHE_SIZE);++i)
	{
			if(me()==0)
			{
				numberOfTransfer++;
				if(i==0){ transferTime = perfcounter_get(); }
			}
			
			mram_read(msgs+(me()*MESSAGE_SIZE)+(i*CACHE_SIZE),cache,CACHE_SIZE);
			
			if(me()==0 && i==0)
			{
				transferTime = transferTime -  perfcounter_get();
			}
			
			if(me()==0 && i==0){  SHAtime = perfcounter_get(); }
			SHA256_t(cache,hash_digests+me()*8,CACHE_SIZE,i);
			if(me()==0 && i==0){ SHAtime += perfcounter_get() - SHAtime; }
	}
	
	if(me()==0) { nr_instructions = perfcounter_get() - initTime;}
    return 0;
}
