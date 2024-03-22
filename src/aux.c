#ifndef __PARAGRAPHER_AUX_C
#define __PARAGRAPHER_AUX_C

#define max(a,b)           \
({                         \
	__typeof__ (a) _a = (a); \
	__typeof__ (b) _b = (b); \
	_a > _b ? _a : _b;       \
})

#define min(a,b)             \
({                           \
		__typeof__ (a) _a = (a); \
		__typeof__ (b) _b = (b); \
		_a < _b ? _a : _b;       \
})

unsigned long __get_nano_time()
{
	struct timespec ts;
	timespec_get(&ts,TIME_UTC);
	return ts.tv_sec*1e9+ts.tv_nsec;
}

int __get_file_contents(char* file_name, char* buff, int buff_size)
{
	int fd=open(file_name, O_RDONLY);
	if(fd<0)
		return -1;

	int count = read(fd, buff, buff_size);
	if(count<buff_size)
		buff[count]=0;

	for(int c = 0; c < count; c++)
		if(buff[c] == 10)
			buff[c] = '|';

	close(fd);

	return count;
}

long __run_command(char* in_cmd, char* output, unsigned int output_size)
{
	assert(in_cmd != NULL);

	int in_cmd_size = strlen(in_cmd);
	char* cmd = malloc(in_cmd_size + 2 * 64);
	assert(cmd != NULL);
	
	char* res_file = cmd + in_cmd_size + 64;
	sprintf(res_file, "_temp_res_%lu.txt", __get_nano_time());	
	sprintf(cmd, "%s 1>%s 2>&1", in_cmd, res_file);

	int ret = system(cmd);
	long ret2 = 0;
	if(output != NULL && output_size != 0)
		ret2 = __get_file_contents(res_file, output, output_size);
	ret2 = (ret2 << 32) + ret;

	if(access(res_file, F_OK) == 0)
	{
		ret = unlink(res_file);
		assert(ret == 0);
	}
	
	memset(cmd, 0, in_cmd_size + 2 * 64);
	free(cmd);
	cmd = NULL;

	return ret2;
}

void* __create_shm(char* shm_file_name, unsigned long length)
{
	assert(shm_file_name != NULL);
	assert(length > 0);

 	int shm_fd = shm_open(shm_file_name, O_RDWR|O_CREAT, 0644);
 	if(shm_fd == -1)
	{
		__PD && printf("[PARAGRAPHER] __create_shm(), error in shm_open() %d, %s .\033[0;37m \n", errno, strerror(errno));
		return NULL;
	}	

	int ret = ftruncate(shm_fd, length);
	if(ret != 0)
	{
		__PD && printf("[PARAGRAPHER] __create_shm(), error in ftruncate() %d, %s .\033[0;37m \n", errno, strerror(errno));
		ret = shm_unlink(shm_file_name);
		assert(ret == 0);
		return NULL;
	}

	void* mem = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED , shm_fd, 0);
	if(mem == MAP_FAILED)
	{
		__PD && printf("[PARAGRAPHER] __create_shm(), error in mmap() %d, %s .\033[0;37m \n", errno, strerror(errno));
		ret = shm_unlink(shm_file_name);
		assert(ret == 0);
		return NULL;
	}

	close(shm_fd);
	shm_fd = -1;

	return mem;
}

#endif
