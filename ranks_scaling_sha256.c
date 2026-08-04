#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include "common.h"
#include <dpu.h>
#include <dpu_log.h>

#ifndef DPU_EXE
#define DPU_EXE "./SHA256DPU"
#endif

#ifndef RANKITER
#define RANKITER 1
#endif

#if RANKITER < 40
#define NRRANKS RANKITER
#else
#define NRRANKS 40
#endif

#define DPUS_PER_RANK 64

/* --- must match the DPU side exactly (common.h) --- */
#define BUFFER_SIZE_DPU (MESSAGE_SIZE * NR_TASKLETS)   /* == MSG_BUFFER_SIZE on the DPU */

/* SHA-256 digest = 8 x 32-bit words = 32 bytes, one per tasklet
   (hash_digests[me()*8 .. me()*8+7] on the DPU side).
   NOTE: these are raw uint32_t H[] words as written by SHA256_t(), with
   no endianness conversion - compare/transfer them as-is. */
#define DIGEST_WORDS 8
#define DIGEST_BYTES (DIGEST_WORDS * sizeof(uint32_t))
#define DIGESTS_SIZE_DPU (NR_TASKLETS * DIGEST_BYTES)

/* Global buffers - pointers for dynamic allocation */
unsigned char *buffer_plaintext = NULL;
unsigned char *buffer_dpu_digest_asynctransfer = NULL;
unsigned char *buffer_dpu_digest_asyncexec = NULL;
unsigned char *buffer_sw_digest = NULL;

static inline double get_time_seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return t.tv_sec + 1.0e-9 * t.tv_nsec;
}

/* ============================================================================
 * INPUT GENERATION - fills `buffer` with `size` random bytes directly in
 * RAM (same role as initialize_buffer() in the AES benchmark). No files
 * are read or written; the same buffer is hashed by both the DPUs and the
 * host software reference.
 * ============================================================================ */
void initialize_buffer(unsigned char *buffer, unsigned long long int size) {
    for (unsigned long long int i = 0; i < size; ++i) {
        buffer[i] = (unsigned char) (rand() % 256);
    }
}

int main(void) {
    FILE *fp = fopen("rank_scaling_sha256.csv", "r");
    int file_exists = 0;
    if (fp != NULL) {
        file_exists = 1;
        fclose(fp);
    }

    fp = fopen("rank_scaling_sha256.csv", "a");
    if (fp == NULL) {
        return 1;
    }

    if (!file_exists) {
        fprintf(fp, "nRanks,dataSize,timeUPMEM_asyncTrans,timeUPMEM_asyncExec,timeSHAHost\n");
    }
    srand(time(0));

    struct dpu_set_t rank_set, rank, dpu_set;
    uint32_t each_rank, each_dpu;
    int num_dpus = 0, num_ranks = 0, total_dpus = 0;
    uint32_t dpus_per_rank[256] = {0};
    unsigned long long int total_buffer_size = 0;    /* plaintext, one rank's worth  */
    unsigned long long int total_digest_size = 0;    /* digests,   one rank's worth  */

    double time_start_asyncTrans = 0, time_end_asyncTrans = 0;
    double time_start_asyncExec  = 0, time_end_asyncExec  = 0;
    double time_SHAHost_start = 0, time_SHAHost_end = 0;

    int dpu_buffer_offset = 0;
    int dpu_digest_offset = 0;
    int error_count = 0;

    #ifdef ASYNCTRANS
    DPU_ASSERT(dpu_alloc_ranks(NRRANKS, NULL, &rank_set));
    DPU_ASSERT(dpu_get_nr_dpus(rank_set, &num_dpus));

    DPU_RANK_FOREACH(rank_set, rank, each_rank)
    {
        int rank_dpus = 0;
        DPU_ASSERT(dpu_get_nr_dpus(rank, &rank_dpus));
        dpus_per_rank[each_rank] = rank_dpus;
        num_ranks++;
        total_dpus += rank_dpus;
    }

    DPU_ASSERT(dpu_load(rank_set, DPU_EXE, NULL));
    total_buffer_size = (unsigned long long int)DPUS_PER_RANK * BUFFER_SIZE_DPU;
    total_digest_size = (unsigned long long int)DPUS_PER_RANK * DIGESTS_SIZE_DPU;

    printf("====== RUN ASYNCHRONOUS TRANSFER ======\n");
    printf("NUMBER OF RANKS ALLOCATED: %d (%d DPUs)\n", NRRANKS, total_dpus);
    printf("TOTAL DATA TO HASH: %.1f MB\n", ((total_buffer_size*NRRANKS)/(1024.0*1024.0)));

    buffer_plaintext = malloc(total_buffer_size);
    buffer_dpu_digest_asynctransfer = malloc(total_digest_size);
    buffer_dpu_digest_asyncexec = malloc(total_digest_size);
    buffer_sw_digest = malloc(total_digest_size);

    if (!buffer_plaintext || !buffer_dpu_digest_asynctransfer ||
        !buffer_dpu_digest_asyncexec || !buffer_sw_digest) {
        printf("[\033[1;31mERROR\033[0m] Memory allocation failed\n");
        printf("errno = %d (%s)\n", errno, strerror(errno));
        return 1;
    }

    /* Fill plaintext directly in RAM with random data */
    initialize_buffer(buffer_plaintext, total_buffer_size);

    time_start_asyncTrans = get_time_seconds();
    DPU_RANK_FOREACH(rank_set, rank, each_rank)
    {
        dpu_buffer_offset = 0;
        DPU_FOREACH(rank, dpu_set, each_dpu)
        {
            if (dpu_buffer_offset + BUFFER_SIZE_DPU > total_buffer_size)
            {
                printf("[\033[1;31mERROR\033[0m] Buffer overflow\n");
                return 1;
            }
            DPU_ASSERT(dpu_prepare_xfer(dpu_set, buffer_plaintext + dpu_buffer_offset));
            dpu_buffer_offset += BUFFER_SIZE_DPU;
        }
        DPU_ASSERT(dpu_push_xfer(rank, DPU_XFER_TO_DPU, "msgs", 0, BUFFER_SIZE_DPU, DPU_XFER_ASYNC));
    }

    DPU_ASSERT(dpu_launch(rank_set, DPU_SYNCHRONOUS));

    DPU_RANK_FOREACH(rank_set, rank, each_rank)
    {
        dpu_digest_offset = 0;
        DPU_FOREACH(rank, dpu_set, each_dpu)
        {
            DPU_ASSERT(dpu_prepare_xfer(dpu_set, buffer_dpu_digest_asynctransfer + dpu_digest_offset));
            dpu_digest_offset += DIGESTS_SIZE_DPU;
        }
        DPU_ASSERT(dpu_push_xfer(rank, DPU_XFER_FROM_DPU, "hash_digests", 0, DIGESTS_SIZE_DPU, DPU_XFER_ASYNC));
    }
    dpu_sync(rank_set);
    time_end_asyncTrans = get_time_seconds();
    dpu_free(rank_set);
    #endif /* ASYNCTRANS */

    #ifdef ASYNCEXEC
    total_dpus = 0;
    num_ranks = 0;
    dpu_buffer_offset = 0;
    dpu_digest_offset = 0;

    DPU_ASSERT(dpu_alloc_ranks(NRRANKS, NULL, &rank_set));
    DPU_ASSERT(dpu_get_nr_dpus(rank_set, &num_dpus));

    DPU_RANK_FOREACH(rank_set, rank, each_rank)
    {
        int rank_dpus = 0;
        DPU_ASSERT(dpu_get_nr_dpus(rank, &rank_dpus));
        dpus_per_rank[each_rank] = rank_dpus;
        num_ranks++;
        total_dpus += rank_dpus;
    }

    printf("====== RUN ASYNCHRONOUS EXECUTION ======\n");
    printf("NUMBER OF RANKS ALLOCATED: %d (%d DPUs)\n", NRRANKS, total_dpus);
    printf("TOTAL DATA TO HASH: %.1f MB\n", ((total_buffer_size*NRRANKS)/(1024.0*1024.0)));

    DPU_ASSERT(dpu_load(rank_set, DPU_EXE, NULL));

    time_start_asyncExec = get_time_seconds();
    DPU_RANK_FOREACH(rank_set, rank, each_rank)
    {
        dpu_buffer_offset = 0;
        DPU_FOREACH(rank, dpu_set, each_dpu)
        {
            if (dpu_buffer_offset + BUFFER_SIZE_DPU > total_buffer_size) {
                printf("[\033[1;31mERROR\033[0m] Buffer overflow\n");
                return 1;
            }
            DPU_ASSERT(dpu_prepare_xfer(dpu_set, buffer_plaintext + dpu_buffer_offset));
            dpu_buffer_offset += BUFFER_SIZE_DPU;
        }
        DPU_ASSERT(dpu_push_xfer(rank, DPU_XFER_TO_DPU, "msgs", 0, BUFFER_SIZE_DPU, DPU_XFER_DEFAULT));
        DPU_ASSERT(dpu_launch(rank, DPU_ASYNCHRONOUS));
    }

    dpu_sync(rank_set);

    DPU_RANK_FOREACH(rank_set, rank, each_rank)
    {
        dpu_digest_offset = 0;
        DPU_FOREACH(rank, dpu_set, each_dpu)
        {
            if (dpu_digest_offset + DIGESTS_SIZE_DPU > total_digest_size) {
                printf("[\033[1;31mERROR\033[0m] Digest buffer overflow\n");
                return 1;
            }
            DPU_ASSERT(dpu_prepare_xfer(dpu_set, buffer_dpu_digest_asyncexec + dpu_digest_offset));
            dpu_digest_offset += DIGESTS_SIZE_DPU;
        }
        DPU_ASSERT(dpu_push_xfer(rank, DPU_XFER_FROM_DPU, "hash_digests", 0, DIGESTS_SIZE_DPU, DPU_XFER_ASYNC));
    }
    dpu_sync(rank_set);
    time_end_asyncExec = get_time_seconds();
    dpu_free(rank_set);
    #endif /* ASYNCEXEC */

    /* Software SHA-256 reference: SAME SHA256_t() used by the DPU kernel
       (from common.h), called once per tasklet-sized MESSAGE_SIZE slice
       with initH=0, which reproduces exactly the chained result the DPU
       computes over its 1024-byte chunks (block boundaries always fall on
       64-byte lines, so the chaining state is identical either way).
       Output is written directly as raw uint32_t H[] words - no
       byte-order conversion - to match the DPU's "hash_digests" layout. */
    #ifdef SHAHOST
    time_SHAHost_start = get_time_seconds();
    for (int m = 0; m < NRRANKS; m++)
    {
        for (int d = 0; d < DPUS_PER_RANK; d++)
        {
            unsigned char *dpu_base = buffer_plaintext + (unsigned long long int)d * BUFFER_SIZE_DPU;
            for (int t = 0; t < NR_TASKLETS; t++)
            {
                uint32_t *digest_dst = (uint32_t *)(buffer_sw_digest +
                                        (unsigned long long int)d * DIGESTS_SIZE_DPU + t * DIGEST_BYTES);
                SHA256_t((const char *)(dpu_base + (unsigned long long int)t * MESSAGE_SIZE),
                         digest_dst, MESSAGE_SIZE, 0);
            }
        }
    }
    time_SHAHost_end = get_time_seconds();
    #endif

    #if defined(SHAHOST) && defined(ASYNCTRANS) && defined(ASYNCEXEC)
    int first_error_byte = -1;
    for (unsigned long long int i = 0; i < total_digest_size; ++i)
    {
        if (buffer_dpu_digest_asynctransfer[i] != buffer_sw_digest[i] ||
            buffer_sw_digest[i] != buffer_dpu_digest_asyncexec[i])
        {
            if (error_count == 0) first_error_byte = i;
            error_count++;
        }
    }
    if (error_count == 0)
        printf("[\033[1;32mOK\033[0m] All digests match\n");
    else
        printf("[\033[1;31mERROR\033[0m] %d bytes mismatch (first error at byte %d)\n", error_count, first_error_byte);
    #endif

    int MB = ((total_buffer_size*NRRANKS) / (1024 * 1024));

    fprintf(fp, "%d,%d,%.2f,%.2f,%.2f\n",
            NRRANKS,
            MB,
            1000*(time_end_asyncTrans-time_start_asyncTrans),
            1000*(time_end_asyncExec-time_start_asyncExec),
            1000*(time_SHAHost_end-time_SHAHost_start)
    );

    free(buffer_plaintext);
    free(buffer_dpu_digest_asynctransfer);
    free(buffer_dpu_digest_asyncexec);
    free(buffer_sw_digest);
    fclose(fp);

    printf("\n\n");
    return error_count;
}