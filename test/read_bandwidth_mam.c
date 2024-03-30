/*
	It is similar to read_bandwidth.c, but repeats each run for some rounds, default: 3.
	The output also has 3 values per each config: min, avg., and max bandwidth.
*/

#define _GNU_SOURCE

#include <sys/sysinfo.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <limits.h>
#include <omp.h>

#define __PD 1
#include <../src/aux.c>

unsigned long file_size = 0;
int open_flags = O_RDONLY;

unsigned long read_mmaped(char* file_path,int threads, unsigned long block_size, float* load_imbalance);
unsigned long read_pread (char* file_path,int threads, unsigned long block_size, float* load_imbalance);
unsigned long read_read  (char* file_path,int threads, unsigned long block_size, float* load_imbalance);

int main(int argc, char** args)
{	
	// Initializations
		setlocale(LC_NUMERIC, "");
		setbuf(stdout, NULL);
		setbuf(stderr, NULL);

		unsigned long max_threads = 2 * get_nprocs();
		unsigned long max_block_size = 4 * 1024 * 1024UL;
		unsigned long blocks_per_thread = 4;
		int rounds = 3;
		unsigned long keep_the_file = 0;
		char* flushcache_cmd = NULL;
		char file_path[PATH_MAX] = {0};
		char* path = NULL;
		unsigned long block_sizes[] = {4096UL, 4096UL * 1024 , 0};
		unsigned long threads[] = {1, 2, 4, 0, 0, 0};
		char* methods[] = {"mmap","pread", "read"};

		printf("\n---------------------\n");
		printf("read_bandwidth\n");
		printf("  Usage:\n    -p path/to/folder\n    -t #threads\n    -mbs max block size in KB\n");
		printf("    -bpt #blocks per thread\n    -k keep the created binary file for next usgaes\n    -f flushcache command\n");
		printf("    -r rounds\n");
		printf("    -od use O_DIRECT for pread() and read()\n");

		printf("---------------------\n");
		for(int i=0; i< argc; i++)
		{
			printf("  args[%d]: %s\n",i, args[i]);

			if(!strcmp(args[i], "-p") && argc > i)
				path = args[i+1];

			if(!strcmp(args[i], "-f") && argc > i)
			{
				flushcache_cmd = malloc(strlen(args[i])+ 256);
				assert(flushcache_cmd != NULL);
				sprintf(flushcache_cmd, "taskset 0x`printf FF%%.0s {1..128}` %s 1>/dev/null 2>&1", args[i+1]);
			}

			if(!strcmp(args[i], "-t") && argc > i)
				max_threads = atol(args[i+1]);

			if(!strcmp(args[i], "-r") && argc > i)
				rounds = atoi(args[i+1]);

			if(!strcmp(args[i], "-k"))
				keep_the_file = 1;

			if(!strcmp(args[i], "-od"))
				open_flags |= O_DIRECT;

			if(!strcmp(args[i], "-mbs") && argc > i)
			{
				max_block_size = atol(args[i+1]) * 1024;
				if(max_block_size % 4096 != 0)
					max_block_size -= max_block_size % 4096;
			}

			if(!strcmp(args[i], "-bpt") && argc > i)
				blocks_per_thread = atol(args[i+1]);
		}
		assert(path != NULL && "-p argument is compulsory");

		file_size = max_threads * blocks_per_thread * max_block_size;
		sprintf(file_path, "%s/read_bandwidth.bin", path);

		printf("  max_threads:      \t\t%'lu\n", max_threads);
		printf("  max_block_size:   \t\t%'lu Bytes\n", max_block_size);
		printf("  blocks_per_thread:\t\t%'lu\n", blocks_per_thread);
		printf("  file_size:        \t\t%'lu Bytes\n", file_size);
		printf("  path:             \t\t%s\n", path);
		printf("  rounds:           \t\t%d\n", rounds);
		printf("  keep_the_file:    \t\t%u\n", keep_the_file);
		printf("  file_path:        \t\t%s\n", file_path);
		printf("  flushcache_cmd:   \t\t%s\n", flushcache_cmd);
		printf("  use O_DIRECT:     \t\t%u\n", (open_flags & O_DIRECT) ? 1 : 0);

		if(max_block_size > block_sizes[1])
			block_sizes[2] = max_block_size;
		printf("  block_sizes:      \t\t");
		for(int b = 0; b < sizeof(block_sizes)/sizeof(unsigned long); b++)
			if(block_sizes[b] != 0)
				printf("%lu, ", block_sizes[b]);
		printf("\n");

		if(max_threads >= 32)
		{
			threads[3] = max_threads / 4;
			threads[4] = max_threads / 2;
			threads[5] = max_threads;
		}
		else if(max_threads >= 16)
		{
			threads[3] = max_threads / 2;
			threads[4] = max_threads;
		}
		else
			threads[3] = max_threads;

		printf("  threads:          \t\t");
		for(int t = 0; t < sizeof(threads)/sizeof(unsigned long); t++)
			if(threads[t])
				printf("%lu, ", threads[t]);
		printf("\n");

		printf("---------------------\n");
		
	// Creating the file
		if(access(file_path, F_OK) == 0)
		{
			struct stat st = {0};
			stat(file_path, &st);
			if(st.st_size < file_size)
				unlink(file_path);
		}
		if(access(file_path, F_OK) != 0)
		{
			unsigned long t0 = - __get_nano_time();
			int fd = open(file_path, O_RDWR|O_CREAT, 0600);
		 	assert(fd != -1);

			int ret = ftruncate(fd, file_size);
			assert(ret == 0);

			unsigned long* mem = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED , fd, 0);
			assert(mem != MAP_FAILED);
		
			close(fd);
			fd = -1;

			#pragma omp parallel for 
			for(unsigned long i = 0; i < file_size / sizeof(unsigned long); i++)
				mem[i] = i;

			ret = msync(mem, file_size, MS_SYNC);
			assert(ret == 0);

			ret = munmap(mem, file_size);
			assert(ret == 0);
			mem = NULL;

			t0 += __get_nano_time();
			printf("  File created in %'.2f seconds.\n", t0 / 1e9);
			printf("---------------------\n");
		}
		
	// Readings
		for(int m = 0; m < sizeof(methods)/sizeof(char*); m++)
		{
			printf("%s (MB/s)\n", methods[m]);
			printf("%-10s;","Threads");

			if(m == 0)
				printf("%-10s;          ;          ;  ;%-10s;          ;          ;  ;","","O_DIRECT");
			else
				for(int b = 0; b < sizeof(block_sizes)/sizeof(unsigned long); b++)
				{
					if(block_sizes[b] != 0)
					{
						if(block_sizes[b] < 1024)
							printf( "%8u B;          ;          ;  ;", block_sizes[b]);
						else if(block_sizes[b] < 1024 * 1024)
							printf("%7u KB;          ;          ;  ;", block_sizes[b] / 1024);
						else
							printf("%7u MB;          ;          ;  ;", block_sizes[b] / 1024 / 1024);
					}
				}
			printf("\n");

			for(int t = 0; t < sizeof(threads)/sizeof(unsigned long); t++)
			{
				if(threads[t] == 0)
					continue;

				printf("%10u;", threads[t]);

				for(int b = 0; b < sizeof(block_sizes)/sizeof(unsigned long); b++)
				{
					if(block_sizes[b] != 0)
					{
						unsigned long ts = 0;
						unsigned long tmax = 0;
						unsigned long tmin = -1UL;
						unsigned long sum_total = 0;
						double sum_li = 0;

						for(int r = 0; r < rounds; r++)
						{
							if(flushcache_cmd != NULL)
								system(flushcache_cmd);

							unsigned long t0 = - __get_nano_time();
							float load_imbalance = 0;
							unsigned long sum = 0;

							if(m == 0)
								sum = read_mmaped(file_path, threads[t], block_sizes[b], &load_imbalance);
							else if(m == 1)
								sum = read_pread(file_path, threads[t], block_sizes[b], &load_imbalance);
							else if(m == 2)
								sum = read_read(file_path, threads[t], block_sizes[b], &load_imbalance);

							t0 += __get_nano_time();

							if(sum != (file_size / 8) * (file_size / 8 - 1) / 2)
							{
								sum_total = 0;
								break;
							}

							sum_total += sum;
							sum_li += load_imbalance;
							
							ts += t0;
							if(t0 < tmin)
								tmin = t0;
							if(t0 > tmax)
								tmax = t0;
						}

						if(sum_total != 0)
							printf("%10.1f;%10.1f;%10.1f;%2u;", 
								1e3 * file_size / (tmin), 
								1e3 * file_size / (ts/rounds), 
								1e3 * file_size / (tmax), 
								(unsigned int)(sum_li/rounds)
							);
						else
							printf("%10s;%10s;%10s;%2s;","-","-","-","-");
					}

					if(m == 0 && b == 1)
						break;
				}
				printf("\n");
			}

			printf("\n");
		}
	//

	// Finalizing
		if(flushcache_cmd != NULL)
			system(flushcache_cmd);
		if(!keep_the_file)
			unlink(file_path);
		printf("\n");

	return 0;
}


double get_idle_percentage(unsigned long nt, unsigned long* threads_nt, unsigned int threads_count)
{ 
	unsigned long idle = 0; 
	for(unsigned int t=0; t<threads_count; t++)
		idle += nt - threads_nt[t]; 
	idle /= threads_count; 
	return 100.0 * idle / nt;
}

unsigned long read_mmaped(char* file_path,int threads, unsigned long block_size, float* load_imbalance)
{
	int fd;
	if(block_size == 4096) 
		fd = open(file_path, O_RDONLY);
	else 
		fd = open(file_path, O_RDONLY | O_DIRECT);
	if(fd == -1)
		return 0;

	unsigned long* mem = mmap(NULL, file_size, PROT_READ, MAP_SHARED , fd, 0);
	assert(mem != MAP_FAILED);
	close(fd);
	fd = -1;

	unsigned long sum = 0;

	if(threads == 1)
	{
		for(unsigned long i = 0; i < file_size / sizeof(unsigned long); i++)
			sum += mem[i];
		
		*load_imbalance = 0.0;
	}
	else
	{
		unsigned long* tt = calloc(sizeof(unsigned long), threads);
		assert(tt != NULL);

		unsigned long t0 = - __get_nano_time();
	
		#pragma omp parallel reduction(+: sum) num_threads(threads)
		{
			unsigned tid = omp_get_thread_num();
			tt[tid] = - __get_nano_time();

			#pragma omp for nowait 
			for(unsigned long i = 0; i < file_size / sizeof(unsigned long); i++)
				sum += mem[i];
		
			tt[tid] += __get_nano_time();
		}

		t0 += __get_nano_time();
		*load_imbalance = get_idle_percentage(t0, tt, threads);

		free(tt);
		tt = NULL;
	}

	int ret = munmap(mem, file_size);
	assert(ret == 0);
	mem = NULL;

	return sum;
}


unsigned long read_pread(char* file_path,int threads, unsigned long block_size, float* load_imbalance)
{
	int fd = open(file_path, open_flags);
	if(fd == -1)
		return 0;

	unsigned long sum = 0;

	if(threads == 1)
	{ 
		unsigned long* mmem = malloc(block_size + 4096);
		assert(mmem != NULL);
		unsigned long* mem = mmem;
		if((unsigned long)mem % 4096 != 0)
			mem = (unsigned long*)((unsigned long)mem + 4096 - ((unsigned long)mem % 4096));

		unsigned long trd = 0;
		while(trd < file_size)
		{
			unsigned long read_bytes = pread(fd, mem, block_size, trd);
			assert(read_bytes != -1);
			if(read_bytes % 8 != 0)
				read_bytes -= read_bytes % 8;

			for(unsigned long i = 0; i < read_bytes / sizeof(unsigned long); i++)
				sum += mem[i];

			trd += read_bytes;
		}

		*load_imbalance = 0.0;

		free(mmem);
		mmem = NULL;
		mem = NULL;
	}
	else
	{
		unsigned long* tt = calloc(sizeof(unsigned long), threads);
		assert(tt != NULL);

		unsigned long t0 = - __get_nano_time();
	
		#pragma omp parallel reduction(+: sum) num_threads(threads)
		{
			unsigned tid = omp_get_thread_num();
			tt[tid] = - __get_nano_time();

			unsigned long* mmem = malloc(block_size + 4096);
			assert(mmem != NULL);
			unsigned long* mem = mmem;
			if((unsigned long)mem % 4096 != 0)
				mem = (unsigned long*)((unsigned long)mem + 4096 - ((unsigned long)mem % 4096));

			#pragma omp for nowait 
			for(unsigned long b = 0; b < file_size / block_size; b++)
			{
				unsigned long offset = block_size * b;

				unsigned long trd = 0;
				while(trd < block_size)
				{
					unsigned long read_bytes = pread(fd, mem, block_size - trd, trd + offset);
					assert(read_bytes != -1);
					if(read_bytes % 8 != 0)
						read_bytes -= read_bytes % 8;

					for(unsigned long i = 0; i < read_bytes / sizeof(unsigned long); i++)
						sum += mem[i];

					trd += read_bytes;
				}
			}
		
			tt[tid] += __get_nano_time();

			free(mmem);
			mmem = NULL;
			mem = NULL;
		}

		t0 += __get_nano_time();
		*load_imbalance = get_idle_percentage(t0, tt, threads);


		free(tt);
		tt = NULL;
	}

	return sum;
}

unsigned long read_read(char* file_path,int threads, unsigned long block_size, float* load_imbalance)
{
	{
		int fd = open(file_path, open_flags);
		if(fd == -1)
			return 0;
		close(fd);
	}

	unsigned long sum = 0;

	if(threads == 1)
	{
		int fd = open(file_path, open_flags);
		assert(fd > 0);

		unsigned long* mmem = malloc(block_size + 4096);
		assert(mmem != NULL);
		unsigned long* mem = mmem;
		if((unsigned long)mem % 4096 != 0)
			mem = (unsigned long*)((unsigned long)mem + 4096 - ((unsigned long)mem % 4096));

		unsigned long trd = 0;
		while(trd < file_size)
		{
			unsigned long read_bytes = read(fd, mem, block_size);
			assert(read_bytes == block_size);
			
			for(unsigned long i = 0; i < read_bytes / sizeof(unsigned long); i++)
				sum += mem[i];

			trd += read_bytes;
		}

		*load_imbalance = 0.0;

		free(mmem);
		mmem = NULL;
		mem = NULL;
		close(fd);
		fd = -1;
	}
	else
	{
		unsigned long* tt = calloc(sizeof(unsigned long), threads);
		assert(tt != NULL);

		unsigned long t0 = - __get_nano_time();
	
		#pragma omp parallel reduction(+: sum) num_threads(threads)
		{
			unsigned tid = omp_get_thread_num();
			tt[tid] = - __get_nano_time();

			unsigned long* mmem = malloc(block_size + 4096);
			assert(mmem != NULL);
			unsigned long* mem = mmem;
			if((unsigned long)mem % 4096 != 0)
				mem = (unsigned long*)((unsigned long)mem + 4096 - ((unsigned long)mem % 4096));

			#pragma omp for nowait 
			for(unsigned long b = 0; b < file_size / block_size; b++)
			{
				int fd = open(file_path, open_flags );
				assert(fd > 0);

				unsigned long offset = block_size * b;
				unsigned long new_offset = lseek(fd, offset, SEEK_SET);
				assert(offset == new_offset);

				unsigned long read_bytes = read(fd, mem, block_size);
				assert(read_bytes == block_size);

				for(unsigned long i = 0; i < read_bytes / sizeof(unsigned long); i++)
					sum += mem[i];

				close(fd);
				fd = -1;
			}
		
			tt[tid] += __get_nano_time();

			free(mmem);
			mmem = NULL;
			mem = NULL;
		}

		t0 += __get_nano_time();
		*load_imbalance = get_idle_percentage(t0, tt, threads);

		free(tt);
		tt = NULL;
	}

	return sum;
}
