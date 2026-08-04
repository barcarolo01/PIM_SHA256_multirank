#ifndef __COMMON_H__
#define __COMMON_H__

#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define MESSAGE_SIZE (1<<16)   // 32 KB per tasklet
#define NR_TASKLETS 16        

//SHA-256 functions
#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define Ch(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define maj(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define SIGMA1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define sigma0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define sigma1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))


static const uint32_t K[8 * 8] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

void strcopia(char* dst,char* src, const int initLength, const int n_bytes)
{
    int i;
    for(i=0;i<n_bytes;++i){ dst[initLength+i] = src[i]; }
}

void SHA256_t(const char* msgBlock, uint32_t* dst, const uint32_t paddedLength, int initH)
{
    int i,t;
    uint32_t a,b,c,d,e,f,g,h,T1,T2;
    uint32_t tmp[4];
    uint32_t H[8];
    uint32_t W[64];

	if(initH==0)
	{
		//INIT (Default values)
		H[0] = 0x6a09e667;
		H[1] = 0xbb67ae85;
		H[2] = 0x3c6ef372;
		H[3] = 0xa54ff53a;
		H[4] = 0x510e527f;
		H[5] = 0x9b05688c;
		H[6] = 0x1f83d9ab;
		H[7] = 0x5be0cd19;
	}
	else
	{
		for(int k=0;k<8;++k){ H[k] = dst[k]; }
	}
	
	
    for(i=0;i<paddedLength/64;++i)
    {
        a=H[0];
        b=H[1];
        c=H[2];
        d=H[3];
        e=H[4];
        f=H[5];
        g=H[6];
        h=H[7];
		
	
        //Computing the extended words W[i]
        for(t=0;t<64;++t)
        {
            if(t<16)
            {
                tmp[0] = (uint32_t)(msgBlock[64*i+4*t+3]) & 0x000000FF;
                tmp[1] = (uint32_t)(msgBlock[64*i+4*t+2] << 8) & 0x0000FF00;
                tmp[2] = (uint32_t)(msgBlock[64*i+4*t+1] << 16) & 0x00FF0000;
                tmp[3] = (uint32_t)(msgBlock[64*i+4*t+0] << 24) & 0xFF000000;
                W[t] = tmp[0] | tmp[1] | tmp[2] | tmp[3];
            }
            else
            {
                W[t] = (sigma1(W[t-2]) + W[t-7] + sigma0(W[t-15]) + W[t-16]); //% twoPow32;
            }
        }

		
        for(t=0;t<64;++t)
        {
            T1 = h + SIGMA1(e) + Ch(e,f,g) + K[t] + W[t];
            T2 = SIGMA0(a) + maj(a,b,c);
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }

        // Values updated just one for each block of 512 bit
        H[0] = a + H[0];
        H[1] = b + H[1];
        H[2] = c + H[2];
        H[3] = d + H[3];
        H[4] = e + H[4];
        H[5] = f + H[5];
        H[6] = g + H[6];
        H[7] = h + H[7];
    }

	for(int k=0;k<8;++k){ dst[k] = H[k]; }
}

uint32_t padding(char* str,uint64_t length)
{
    int i,k,paddingBytes; //paddingBytes takes into account also the first byte "0x80"
    char tmp[MESSAGE_SIZE];
    paddingBytes = 64 - (length % 64);

    if(paddingBytes<=8){ paddingBytes = 64 - paddingBytes + 8; }
    tmp[0] = (char)(0x80) & (char)(0xFF);
    for(i=1;i<paddingBytes-8;i++){ tmp[i] = 0x00; }
    for(k=0;k<8;k++){ tmp[i+k] = ((length*8) >> (7-k)*8) &  0xFF; }

    strcopia(str,tmp,length,paddingBytes);

    return length + paddingBytes;
}

#endif