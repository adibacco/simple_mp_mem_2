/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

/*
 * This sample application is a simple multi-process application which
 * demostrates sharing of queues and memory pools between processes, and
 * using those queues/pools for communication between the processes.
 *
 * Application is designed to run with two processes, a primary and a
 * secondary, and each accepts commands on the commandline, the most
 * important of which is "send", which just sends a string to the other
 * process.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_memcpy.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_branch_prediction.h>
#include <rte_ring.h>
#include <rte_log.h>
#include <sys/time.h>
#include <rte_power.h>



#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define SIZE_1GB		(1024*1024*1024)
#define SIZE_1MB		(1024*1024)

volatile int quit = 0;

uint8_t	data[64];
uint64_t tsc_rate;


static int
init_power_library(void)
{
	enum power_management_env env;
	unsigned int lcore_id;
	int ret = 0;

	RTE_LCORE_FOREACH(lcore_id) {
		/* init power management library */
		ret = rte_power_init(lcore_id);
		if (ret) {
			RTE_LOG(ERR, POWER,
				"Library initialization failed on core %u\n",
				lcore_id);
			return ret;
		}
		/* we're not supporting the VM channel mode */
		env = rte_power_get_env();
		if (env != PM_ENV_ACPI_CPUFREQ &&
				env != PM_ENV_PSTATE_CPUFREQ) {
			RTE_LOG(ERR, POWER,
				"Only ACPI and PSTATE mode are supported\n");
			return -1;
		}
	}
	return ret;
}



double evaluateBandwidth(uint64_t alloc_size, int alloc_socketid, int iterations)
{
	const struct rte_memzone* shared_mem = rte_memzone_reserve_aligned("shared_mem", alloc_size, alloc_socketid,
		RTE_MEMZONE_256MB | RTE_MEMZONE_SIZE_HINT_ONLY | RTE_MEMZONE_IOVA_CONTIG, RTE_CACHE_LINE_SIZE);


	__m256i ymm0;

	ymm0 = _mm256_loadu_si256((const __m256i *)(const void *)data);
	uint64_t chunk_size = 256/8;
	uint64_t blocks = alloc_size/chunk_size;

	uint64_t addr;
	uint64_t start, end;

	double bandwidth = 0;

	double cpu_time_us = 0;

	start = rte_rdtsc_precise();


	for (int j = 0; j < iterations; j++) {

		addr = (uint64_t) (shared_mem->addr);

		int i = 0;
		while (i < blocks) 
		{
			
			_mm256_storeu_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;		
			_mm256_storeu_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;
			_mm256_storeu_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;		
			_mm256_storeu_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;
					

		/*	
			_mm256_stream_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;				
			_mm256_stream_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;
		*/				
			
		}

	}

	cpu_time_us = ((double)(rte_rdtsc_precise() - start) / tsc_rate)*1e6; 
	bandwidth = ((alloc_size*iterations)/cpu_time_us);

	rte_memzone_free(shared_mem);

	printf("%ld, %.3lf, %.2lf\n", alloc_size/1024, bandwidth, cpu_time_us);
	return bandwidth;

}



int main(int argc, char **argv)
{

	int ret;

	printf("pid %d\n", getpid());

	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot init EAL\n");



	
	int c;
	int alloc_socketid = 0;
	int iterations = 1000;
	uint64_t alloc_size = 1;
	opterr = 0;

	while ((c = getopt (argc, argv, "i:m:s:")) != -1)
		switch (c)
		{
			case 'm':
				alloc_socketid = atoi(optarg);
				break;
			case 'i':
				iterations = atoi(optarg);
				break;
			case 's':
				alloc_size = atoi(optarg)*1024;
				break;
			default:
				break;
		}

	printf("alloc_socketid %d alloc_size %ld kB\n", alloc_socketid, alloc_size);

	tsc_rate = rte_get_tsc_hz();
	int socket_id = rte_socket_id();
	unsigned lcore_id = rte_lcore_id();

	init_power_library();


	printf("Main thread on socketid %d core %d\n",  rte_socket_id(), rte_get_main_lcore());


	memset(data, 0xaa, 64);


	if (rte_eal_process_type() == RTE_PROC_PRIMARY){

		rte_power_freq_enable_turbo(lcore_id);
	
		printf("TURBO: %d\n", rte_power_turbo_status(lcore_id));

		rte_power_freq_max(lcore_id);		

		for (int a = 128; a < 1048576; a += 256) {
        	evaluateBandwidth(a*1024, alloc_socketid, 262144/a);
		}

	} 
	else  // SECONDARY
	{
		const struct rte_memzone* shared_mem = rte_memzone_lookup("shared_mem");
		int chunk_size = 256/8;
		int blocks = alloc_size/chunk_size;
		uint64_t addr;
		struct timeval st, et;

		__m256i ymm0;

		for (int j = 0; j < 100000000; j++) {

			gettimeofday(&st,NULL);
			int i = 0;
			addr = (uint64_t) (shared_mem->addr);
			while (i < blocks) {

				
				ymm0 = _mm256_loadu_si256((__m256i *)(void *)(addr));	i++; addr += chunk_size;		
				ymm0 = _mm256_loadu_si256((__m256i *)(void *)(addr));	i++; addr += chunk_size;
						

			/*	
				_mm256_stream_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;				
				_mm256_stream_si256((__m256i *)(void *)(addr), ymm0++);	i++; addr += chunk_size;
			*/				
				
			}

			gettimeofday(&et,NULL);

			
			uint64_t elapsed =  ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
			printf("rte R Bandwidth: %ld MB/s %ld\n", alloc_size/elapsed, elapsed);

		}		
		// Empty for now
	}

	while (1) {
		rte_delay_ms(100);	
	}
	rte_eal_mp_wait_lcore();

	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
