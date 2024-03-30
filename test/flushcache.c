/*
	If there is no sudo access for flushing OS cache (echo 3 >/proc/sys/vm/drop_caches),
	This program can be used to do the same thing by allocating and accessing an array
	of the size of available memory.
*/

#include <assert.h>
#include <numa.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <asm/unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/hw_breakpoint.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/futex.h>
#include <emmintrin.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <locale.h>
#include <omp.h>

int run_command(char* in_command, char* input, unsigned int input_size);
		
int main(int argc, char *argv[])
{

	// Get available memory
	unsigned long mem = 0;
	{
		char input[64];
		run_command("cat /proc/meminfo | grep MemAvailable | cut -f2 -d: | xargs | cut -f1 -d' '", input, 64);
		mem = atol(input) / 1024 / 1024;
		printf("Available mem: %lu GB\n", mem);
	}

	char* arr = NULL;
	mem++;
	do
	{
		mem--;
		arr = numa_alloc_interleaved(mem * 1024 * 1024 * 1024);
	}while(arr == NULL);
	assert(arr != NULL);

	unsigned long sum = 0;
	#pragma omp parallel for
	for(unsigned long m = 0; m < mem * 1024 * 1024 * 1024; m += 4096)
		arr[m] = sum++;

	printf("sum: %lu\n\n",sum);
	return 0;
}

int get_file_contents_no_print(char* file_name, char* buff, int buff_size)
{
	int fd=open(file_name, O_RDONLY);
	if(fd<0)
		return -1;

	int count = read(fd, buff, buff_size);
	if(count<buff_size)
		buff[count]=0;

	close(fd);

	return count;
}

int run_command(char* in_command, char* input, unsigned int input_size)
{
	char* command = malloc(1024 + 64);
	assert(command != NULL);
	char* res_file = command + 1024;
	sprintf(res_file, "_temp_res_%lu.txt",time(0));	
	sprintf(command, "%s >%s", in_command, res_file);
	
	int ret = system(command);
	if(ret == 0)
	{
		if(input != NULL && input_size != 0)
			ret = get_file_contents_no_print(res_file, input, input_size);
	}
	else
		ret = -1;

	int ret2 = unlink(res_file);
	assert(ret2 == 0);
	
	free(command);
	command = NULL;

	return ret;
}
