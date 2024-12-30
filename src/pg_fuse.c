/*
	ParaGrapher FUSE 
*/

#ifndef _DEFAULT_SOURCE 
	#define _DEFAULT_SOURCE 
#endif
#define _GNU_SOURCE

#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <libgen.h>
#include <stddef.h>
#include <numa.h>

#include "aux.c"

#define __PGF_D   0  // __PG_FUSE_DEBUG
#define __PGF_RUB 1  // __PG_FUSE_REMOVE_UNUSED_BLOCKS 

const unsigned long block_size = 1024UL * 1024 * 32;                       // Must be multiplies of BLKSSZGET (512)
const unsigned long idle_time_before_expiration = 1e9 * 10;                // in nanoseconds

char* file_path;
char* file_basename;
unsigned long file_size = 0;
char** blocks_mem = NULL;
char** blocks_main_mem = NULL;
int* blocks_status = NULL;  // 0: loaded, -1: not loaded, -2: loading, -3: being removed, positive values: users count
unsigned int loaded_blocks_count;
unsigned int blocks_count;
unsigned long* blocks_last_access = NULL;
unsigned long start_time;
unsigned int available_threads;

static struct options
{
	const char *file_path;
	int show_help;
} options;

static const struct fuse_opt option_spec[] = 
{
	{ "--file_path=%s", offsetof(struct options, file_path), 1 },
	{ "-h", offsetof(struct options, show_help), 1 },
	{ "--help", offsetof(struct options, show_help), 1 },
	FUSE_OPT_END
};

static void *pg_fuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void) conn;

	// cfg->kernel_cache = 0;
	// cfg->auto_cache = 0;
	// cfg->direct_io = 1;

	return NULL;
}

static int pg_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	(void) fi;

	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0)
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} 
	else if (strcmp(path+1, file_basename) == 0)
	{
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = file_size;
	} 
	else
		res = -ENOENT;

	return res;
}

static int pg_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
	filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);
	filler(buf, file_basename, NULL, 0, FUSE_FILL_DIR_PLUS);

	return 0;
}

static int pg_fuse_open(const char *path, struct fuse_file_info *fi)
{
	(void) fi;

	if (strcmp(path+1, file_basename) != 0)
		return -ENOENT;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int pg_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	// printf("read: %lu\n", size);
	(void) fi;
	
	if(strcmp(path+1, file_basename) != 0)
		return -ENOENT;

	if(size == 0)
		return 0;

	if(offset >= file_size)
		return 0;

	if(offset + size > file_size)
		size = file_size - offset;

	unsigned long last_offset = offset + size;
	unsigned long total_written_bytes = 0;

	for(unsigned long b = offset/block_size; b <= (last_offset - 1)/block_size; b++)
	{
		// Check if the block has been started loading
		int status;
		while(1)
		{
			status = blocks_status[b];
			if(status >= 0)
			{
				if(__sync_bool_compare_and_swap(blocks_status + b, status, status + 1))
				{
					status = blocks_status[b];
					assert(status > 0);
					break;
				}
				else
					continue;
			}

			if(status == -2  || status == -3)
			{
				sched_yield();
				continue;
			}

			if(status == -1)
			{
				if(__sync_bool_compare_and_swap(blocks_status + b, -1, -2))
				{
					status = blocks_status[b];
					assert(status == -2);
					break;
				}
				else
					continue;
			}
		}

		assert(status > 0 || status == -2);

		if(status == -2)
		{
			// Read the block into memory
			unsigned long t0 = __get_nano_time();
			__PGF_D && printf("%.2f(s): Block %u loading for pid: %u\n", 
				(t0 + start_time)/1e9, b, fuse_get_context()->pid);
			__PGF_D && fflush(stdout);

			blocks_main_mem[b] = numa_alloc_interleaved(4096 + block_size);
			assert(blocks_main_mem[b] != NULL);
			
			unsigned long remainder = (unsigned long)blocks_main_mem[b] % 4096;
			if(remainder != 0)
				blocks_mem[b] = (char*)((unsigned long)blocks_main_mem[b] + 4096 - remainder);
			else
				blocks_mem[b] = blocks_main_mem[b];

			int fd = open(file_path, O_RDONLY | O_DIRECT); 
			assert(fd > 0);

			unsigned long start_byte = block_size * b;
			unsigned long length = min(block_size, file_size - start_byte);

			unsigned long new_offset = lseek(fd, start_byte, SEEK_SET);
			assert(new_offset == start_byte);

			unsigned long read_bytes = 0;
			while(read_bytes < length)
			{
				long ret = read(fd, blocks_mem[b] + read_bytes, length - read_bytes);
				assert(ret != -1);
				read_bytes += ret;
			}
			
			close(fd);
			fd = -1;	

			status = __sync_val_compare_and_swap(blocks_status + b, -2, 1);
			assert(status == -2);

			__PGF_RUB && (blocks_last_access[b] = t0);

			__PGF_D && printf("%.2f(s): Block %u loading for pid: %u finished in %.2f seconds\n", 
				(t0 + start_time)/1e9, b, fuse_get_context()->pid, (__get_nano_time() - t0) /1e9);
			__PGF_D && fflush(stdout);

			// Release unused blocks 
			__PGF_RUB && __atomic_fetch_add(&loaded_blocks_count, 1, __ATOMIC_RELAXED);
			if(__PGF_RUB)
			{
				t0 = __get_nano_time();
				for(unsigned int b = 0; b < blocks_count && (loaded_blocks_count > 2 * available_threads); b++)
				{
					if(blocks_status[b] != 0)
						continue;

					if(t0 < blocks_last_access[b] + idle_time_before_expiration)
						continue;

					// Lock block for releasing
					if(__sync_bool_compare_and_swap(blocks_status + b, 0, -3))
					{
						numa_free(blocks_main_mem[b],4096 + block_size);
						blocks_main_mem[b] = NULL;
						blocks_mem[b] = NULL;

						blocks_last_access[b] = 0;
						__atomic_fetch_sub(&loaded_blocks_count, 1, __ATOMIC_RELAXED);

						int status = __sync_val_compare_and_swap(blocks_status + b, -3, -1);
						assert(status == -3);

						__PGF_D && printf("%.2f(s): Block %u released.\n", (t0 + start_time)/1e9, b);
						__PGF_D && fflush(stdout);
					}
				}
			}

			// All done.
		}

		assert(blocks_mem[b] != NULL);
		
		// Write from block to buf 
			unsigned long b_start = offset % block_size;
			unsigned long b_end = min(b_start + size, block_size);
			unsigned long b_len = b_end - b_start;
			
			memcpy(buf, blocks_mem[b] + b_start, b_len);

			total_written_bytes += b_len;
			offset += b_len;
			buf += b_len;
			size -= b_len;

		// Touching block timestamp
			__PGF_RUB && (blocks_last_access[b] = __get_nano_time());

		// Release the block
			status = __atomic_fetch_sub(blocks_status + b, 1, __ATOMIC_RELAXED);
			assert(status >= 1);
	}
	
	return total_written_bytes;
}

static const struct fuse_operations ops = {
	.init = pg_fuse_init,
	.getattr	= pg_fuse_getattr,
	.readdir	= pg_fuse_readdir,
	.open		  = pg_fuse_open,
	.read		  = pg_fuse_read,
};

int main(int argc, char *argv[])
{
	// Parsing aguments and showing help	
		struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
		options.file_path = NULL;
		if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
			return -1;

		if (options.show_help)
		{
			printf(
				"\nParaGrapher FUSE (pg_fuse)\n\n"
				"Arguments:\n"
				"  --file_path=<s>    Path to the file\n"
				"Usage:\n"
				"  ./pg_fuse path/to/mountpoint --file_path=path/to/source/file [FUSE options]\n"
				"\n"
			);
			
			assert(fuse_opt_add_arg(&args, "--help") == 0);
			args.argv[0][0] = '\0';


			int ret = fuse_main(args.argc, args.argv, &ops, NULL);
			fuse_opt_free_args(&args);
			return ret;
		}

	// Check if file_path exists
		int file_path_correct = 0;
		if(options.file_path == NULL)
		{
			printf("Error: No file_path has been given. The file path should be given using --file_path argument.\n\n");
			return -2;
		}
		else if(access(options.file_path, F_OK) != 0)
		{
			printf("Error: The given file_path on \"%s\" is not accesible.\n\n", options.file_path);
			return -3;
		}
	
	// Initializing the data
		file_path = realpath(strdup(options.file_path), NULL);
		file_basename = strdup(basename(file_path));

		int fd = open(file_path,O_RDONLY); 
		assert(fd > 0);

		struct stat st;
		int ret = fstat(fd, &st);
		assert(ret == 0);
		close(fd);
		fd = -1;
		file_size = st.st_size;

		printf("\nParaGrapher FUSE (pg_fuse) started.\n");
		printf("File \"%s\" with size %lu is mounted on \"%s/%s\"\n", 
			file_path, file_size,	realpath(argv[1], NULL), file_basename);
		
		available_threads = get_available_cpus_count();
		printf("Available threads: %u\n", available_threads);		

		blocks_count = 1 + file_size / block_size;
		blocks_mem = calloc(blocks_count, sizeof(char*));
		assert(blocks_mem != NULL);
		blocks_main_mem = calloc(blocks_count, sizeof(char*));
		assert(blocks_main_mem != NULL);
		blocks_status = calloc(blocks_count, sizeof(int));
		assert(blocks_status != NULL);
		loaded_blocks_count = 0;

		if(__PGF_RUB)
		{
			blocks_last_access = calloc(blocks_count, sizeof(unsigned long));
			assert(blocks_last_access != NULL);
		}

		for(unsigned int b = 0; b < blocks_count; b++)
		{
			blocks_status[b] = -1;
			blocks_mem[b] = NULL;
			blocks_main_mem[b] = NULL;
			__PGF_RUB && (blocks_last_access[b] = 0);
		}
		start_time = - __get_nano_time();
		printf("Block size: %lu, Blocks count: %lu\n", block_size, blocks_count);		
		printf("\n\n");
		fflush(stdout);
	
	// Starting fuse
	ret = fuse_main(args.argc, args.argv, &ops, NULL);

	fuse_opt_free_args(&args);

	return ret;
}
