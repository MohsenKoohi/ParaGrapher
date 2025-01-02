#ifndef __PARAGRAPHER_WEBGRAPH_C
#define __PARAGRAPHER_WEBGRAPH_C

#include <libgen.h>

typedef struct
{
	paragrapher_graph_type graph_type;

	char name[PATH_MAX + 8];
	char input_name[PATH_MAX + 8];
	char underlying_name[PATH_MAX + 8];
	unsigned long buffer_size;
	unsigned long max_buffers_count;

	unsigned long vertices_count;
	unsigned long edges_count;

	unsigned long* offsets;

	unsigned long pg_fuse_active;
	char pg_fuse_underlying_graph_mp[PATH_MAX];
	char pg_fuse_graph_mp[PATH_MAX];
	char pg_fuse_offsets_bin_mp[PATH_MAX];
	char pg_fuse_linked_folder[PATH_MAX];
} __wg_graph;

// Changing these values should be reflected also on WebGraphRRServer.java
enum __wg_buffer_status {
	__BS_C_IDLE           = 1,
	__BS_C_REQUESTED      = 2,
	__BS_J_READING        = 3,
	__BS_J_READ_COMPLETED = 4,
	__BS_C_USER_ACCESS    = 5
};

typedef struct
{
	volatile long status;
	volatile long written_edges;
	long start_vertex;
	long start_edge;
	long end_vertex;
	long end_edge;
	long padding[2];
} __wg_buffer_metadata;

// In case of changing __wg_buffer_metadata, it is required to change 
// the Java program and also the __wg_callback_thread

typedef struct
{
	paragrapher_read_request prr;
	paragrapher_csx_callback callback;
	void* callback_args;
	paragrapher_edge_block eb;

	long status;  // values: progressing: 0,  completed: 1, finished unsuccessfully: < 0
	unsigned long buffer_size;
	unsigned long max_buffers_count;
	unsigned long start_time;
	pthread_t thread;

	unsigned long* offsets;
	unsigned char* shm_mem;
	unsigned long shm_mem_size;
	unsigned long bytes_per_edge;

	unsigned long total_edges_requested;
	unsigned long total_edges_read;
	unsigned long total_partitions;
	unsigned long buffers_count;

	unsigned long last_partition;
	unsigned long last_vertex;
	unsigned long last_edge;

	char shm_name[256];
	__wg_buffer_metadata* buffers_metadata;

} __wg_read_request;


paragrapher_graph* __wg_open_graph(char* name, paragrapher_graph_type type, void** args, int argc)
{
	assert(name != NULL);
	assert(type == PARAGRAPHER_CSX_WG_400_AP || type == PARAGRAPHER_CSX_WG_800_AP || type == PARAGRAPHER_CSX_WG_404_AP);
	assert(strlen(name) < PATH_MAX);
	char underlying_name[PATH_MAX]={0};
	char input_name[PATH_MAX]={0};
	sprintf(input_name, "%.*s", PATH_MAX, name);

	//Check if .properties and .graph file exist
	{
		char temp[PATH_MAX + 1024];
		sprintf(temp,"%.*s.properties", PATH_MAX, name);
		if(access(temp, F_OK) != 0)
		{
			__PD && printf("[ParaGrapher] paragrapher_open_graph(), file \"%s\" doesn't exist\n", temp);
			return NULL;
		}

		if(type == PARAGRAPHER_CSX_WG_400_AP || type == PARAGRAPHER_CSX_WG_800_AP)
		{
			sprintf(temp,"%.*s.graph", PATH_MAX, name);
			if(access(temp, F_OK) != 0)
			{
				__PD && printf("[ParaGrapher] paragrapher_open_graph(), file \"%s\" doesn't exist\n", temp);
				return NULL;
			}
			sprintf(underlying_name, "%s", name);
		}
		else if(type == PARAGRAPHER_CSX_WG_404_AP)
		{
			sprintf(temp,"%.*s.labels", PATH_MAX, name);
			if(access(temp, F_OK) != 0)
			{
				__PD && printf("[ParaGrapher] paragrapher_open_graph(), file \"%s\" doesn't exist\n", temp);
				return NULL;
			}

			sprintf(temp,"%.*s.labeloffsets", PATH_MAX, name);
			if(access(temp, F_OK) != 0)
			{
				__PD && printf("[ParaGrapher] paragrapher_open_graph(), file \"%s\" doesn't exist\n", temp);
				return NULL;
			}

			char cmd[1024 + PATH_MAX];
			sprintf(cmd, "echo -n `dirname %s.properties`/`cat %s.properties | grep underlyinggraph | cut -f2 -d= |xargs`", name, name);
			int ret = (int)__run_command(cmd, underlying_name, PATH_MAX + 1023);
			assert(ret == 0);
			__PD && printf("[ParaGrapher] paragrapher_open_graph(), underlying graph name: %s\n",underlying_name);

			sprintf(temp,"%.*s.graph", PATH_MAX, underlying_name);
			if(access(temp, F_OK) != 0)
			{
				__PD && printf("[ParaGrapher] paragrapher_open_graph(), file \"%s\" doesn't exist\n", temp);
				return NULL;
			}

			sprintf(temp,"%.*s.properties", PATH_MAX, underlying_name);
			if(access(temp, F_OK) != 0)
			{
				__PD && printf("[ParaGrapher] paragrapher_open_graph(), file \"%s\" doesn't exist\n", temp);
				return NULL;
			}
		}
		else
		{
			assert(0 && "Do not reach here.");
			exit(-1);
		}
	}

	__wg_graph* graph = calloc(1, sizeof(__wg_graph));
	assert(graph != NULL);

	// Iterating over args
		for(int a = 0; a < argc; a++)
		{
			// USE_PG_FUSE
			if(!strcmp((char*)args[a], "USE_PG_FUSE"))
			{
				char temp [PATH_MAX];
				sprintf(temp, "%s.offsets", underlying_name);
				if(access(temp, F_OK) == 0)
				{
					sprintf(temp, "%s_offsets.bin", underlying_name);
					if(access(temp, F_OK) == 0)
						graph->pg_fuse_active = 1;
				}

				if (graph->pg_fuse_active)
					printf("[ParaGrapher] Loading with pg_fuse\n");
				else
					printf("[ParaGrapher] Cannot load using pg_fuse as auxiliary files have not created yet.\n");
			}
		}

	// Mount files if pg_fuse is active 
	if(graph->pg_fuse_active)
	{
		char* temp = malloc(4096 * 4 + PATH_MAX * 4 + get_nprocs()/4);
		assert(temp != NULL);
		char res[4096];
		char n1[2 * PATH_MAX + 2];
		char n2[2 * PATH_MAX + 2];

		char* PLF = strdup(getenv("PARAGRAPHER_LIB_FOLDER"));

		char* ts = calloc(1024 + get_nprocs() / 4, 1);
		assert(ts != NULL);
		for(int i = 0; i < get_nprocs() / 4  + 1; i++)
			sprintf(ts + strlen(ts), "F");

		// Basename and dirname of underlying graph
			sprintf(temp, "%s.graph", underlying_name);
			char* u_basename = strdup(basename(temp));
			u_basename[strlen(u_basename) - 6] = '\0';
			char* u_dirname = realpath(dirname(temp), NULL);
			
		// Creating temp folders
			sprintf(graph->pg_fuse_underlying_graph_mp, "/tmp/pg_fuse-%s-graph-%lu", u_basename, __get_nano_time());
			int ret = mkdir(graph->pg_fuse_underlying_graph_mp, 0700);
			assert(ret == 0);

			sprintf(graph->pg_fuse_offsets_bin_mp, "/tmp/pg_fuse-%s-offsets-bin-%lu", u_basename, __get_nano_time());
			ret = mkdir(graph->pg_fuse_offsets_bin_mp, 0700);
			assert(ret == 0);

			sprintf(graph->pg_fuse_linked_folder, "/tmp/pg_fuse-%s-all-%lu", u_basename, __get_nano_time());
			ret = mkdir(graph->pg_fuse_linked_folder, 0700);
			assert(ret == 0);

			if(type == PARAGRAPHER_CSX_WG_404_AP)
			{
				sprintf(graph->pg_fuse_graph_mp, "/tmp/pg_fuse-%s-labels-%lu", u_basename, __get_nano_time());
				int ret = mkdir(graph->pg_fuse_graph_mp, 0700);
				assert(ret == 0);
			}

			printf("[ParaGrapher] Mounting underlying graph on %s .\n", graph->pg_fuse_underlying_graph_mp);
			printf("[ParaGrapher] Mounting offsets.bin file on %s .\n", graph->pg_fuse_offsets_bin_mp);
			printf("[ParaGrapher] Linking graph files on %s .\n", graph->pg_fuse_linked_folder);
		
		// Mounting and linking the .graph file
			sprintf(temp, "taskset 0x%s %s/pg_fuse.o %s --file_path=%s.graph -o auto_unmount", 
				ts, PLF, graph->pg_fuse_underlying_graph_mp, underlying_name);
			ret = __run_command(temp, res, 4096);
			if(ret != 0)
			{
				printf("[ParaGrapher] Could not mount .graph file on %s, Output: %s\n", graph->pg_fuse_underlying_graph_mp, res);
				return NULL;
			}

			sprintf(n1, "%s/%s.graph",graph->pg_fuse_underlying_graph_mp, u_basename);
			sprintf(n2, "%s/%s.graph",graph->pg_fuse_linked_folder, u_basename);
			ret = symlink(n1, n2);
			if(ret != 0)
			{
				printf("[ParaGrapher] Could not link %s on %s .\n",n1, n2);
				return NULL;
			}

		// Linking the .properties and .offsets files
			sprintf(n1, "%s/%s.properties",u_dirname, u_basename);
			sprintf(n2, "%s/%s.properties",graph->pg_fuse_linked_folder, u_basename);
			ret = symlink(n1, n2);
			if(ret != 0)
			{
				printf("[ParaGrapher] Could not link %s on %s .\n",n1, n2);
				return NULL;
			}

			sprintf(n1, "%s/%s.offsets", u_dirname, u_basename);
			sprintf(n2, "%s/%s.offsets", graph->pg_fuse_linked_folder, u_basename);
			ret = symlink(n1, n2);
			if(ret != 0)
			{
				printf("[ParaGrapher] Could not link %s on %s .\n",n1, n2);
				return NULL;
			}

		// Mounting and linking the .offsets_bin file				
			sprintf(temp, "taskset 0x%s %s/pg_fuse.o %s --file_path=%s_offsets.bin -o auto_unmount", 
				ts, PLF, graph->pg_fuse_offsets_bin_mp, underlying_name);
			ret = __run_command(temp, res, 4096);
			if(ret != 0)
			{
				printf("[ParaGrapher] Could not mount offsets.bin on %s, Output: %s\n", graph->pg_fuse_offsets_bin_mp, res);
				return NULL;
			}

			sprintf(n1, "%s/%s_offsets.bin", graph->pg_fuse_offsets_bin_mp, u_basename);
			sprintf(n2, "%s/%s_offsets.bin", graph->pg_fuse_linked_folder, u_basename);
			ret = symlink(n1, n2);
			if(ret != 0)
			{
				printf("[ParaGrapher] Could not link %s on %s .\n",n1, n2);
				return NULL;
			}

		// Updating the underlying_name
			sprintf(underlying_name, "%s/%s", graph->pg_fuse_linked_folder, u_basename);

		if(type != PARAGRAPHER_CSX_WG_404_AP)
		{
			sprintf(name, "%s", underlying_name);
		}
		else
		{
			// Mounting the .labels file
				sprintf(temp, "taskset 0x%s %s/pg_fuse.o %s --file_path=%s.labels -o auto_unmount", 
					ts, PLF, graph->pg_fuse_graph_mp, name);
				ret = __run_command(temp, res, 4096);
				if(ret != 0)
				{
					printf("[ParaGrapher] Could not mount on %s, Output: %s\n", graph->pg_fuse_graph_mp, res);
					return NULL;
				}
				printf("[ParaGrapher] Mounting labelled graph on %s .\n", graph->pg_fuse_graph_mp);
				
				sprintf(temp, "%s.labels", name);
				char* l_basename = strdup(basename(temp));
				l_basename[strlen(l_basename) - 7] = '\0';
			
			// Linking the .labels file
				sprintf(n1, "%s/%s.labels", graph->pg_fuse_graph_mp, l_basename);
				sprintf(n2, "%s/%s.labels", graph->pg_fuse_linked_folder, l_basename);
				ret = symlink(n1, n2);
				if(ret != 0)
				{
					printf("[ParaGrapher] Could not link %s on %s .\n",n1, n2);
					return NULL;
				}

			// Linking the .properties file
				sprintf(n1, "%s/%s.properties", u_dirname, l_basename);
				sprintf(n2, "%s/%s.properties", graph->pg_fuse_linked_folder, l_basename);
				ret = symlink(n1, n2);
				if(ret != 0)
				{
					printf("[ParaGrapher] Could not link %s on %s .\n",n1, n2);
					return NULL;
				}

			// Linking the .labeloffsets file
				sprintf(n1, "%s/%s.labeloffsets", u_dirname, l_basename);
				sprintf(n2, "%s/%s.labeloffsets", graph->pg_fuse_linked_folder, l_basename);
				ret = symlink(n1, n2);
				if(ret != 0)
				{
					printf("[ParaGrapher] Could not link %s on %s .\n",n1, n2);
					return NULL;
				}

			sprintf(name, "%s/%s", graph->pg_fuse_linked_folder, l_basename);
			free(l_basename);			
		}
		
		free(temp);
		free(u_basename);
		free(u_dirname);
		free(PLF);
		free(ts);
	}
	
	graph->graph_type = type;
	sprintf(graph->input_name, "%.*s", PATH_MAX, input_name);
	sprintf(graph->name, "%.*s", PATH_MAX, name);
	sprintf(graph->underlying_name, "%.*s", PATH_MAX, underlying_name);

	// printf("Graph name: %s\n", name);
	// printf("Graph underlying name: %s\n", underlying_name);

	graph->buffer_size = 1024UL * 1024 * 64;
	{
		char res[1024];
		int ret = (int)__run_command("nproc", res, 1023);
		assert(ret == 0);
		graph->max_buffers_count = atoi(res);

		ret = (int)__run_command("lscpu | grep \"Thread(s) per core\" | head -n1 | cut -f2 -d:|xargs", res, 1023);
		assert(ret == 0);
		unsigned int tpc = atoi(res);
		if(tpc == 1)
			graph->max_buffers_count *= 2; 
	}
	graph->offsets = NULL;

	{
		char cmd[1024 + PATH_MAX],res[1024];
		sprintf(cmd, "cat %s.properties | grep -e \"\\bnodes=\" | cut -f2 -d=", graph->underlying_name);
		int ret = (int)__run_command(cmd, res, 1023);
		assert(ret == 0);
		graph->vertices_count = atol(res);

		sprintf(cmd, "cat %s.properties | grep -e \"\\barcs=\" | cut -f2 -d=", graph->underlying_name);
		ret = (int)__run_command(cmd,res,1023);
		assert(ret == 0);
		graph->edges_count = atol(res);
	}

	return (paragrapher_graph*) graph;
}

int __wg_release_graph(paragrapher_graph* in_graph, void** args, int argc)
{
	__wg_graph* graph = (__wg_graph*)in_graph;

	__sync_synchronize();

	// Releasing offsets array
		if(graph->offsets != NULL)
		{
			munmap(graph->offsets, 8UL * (1 + graph->vertices_count));
			graph->offsets = NULL;
		}

	if(graph->pg_fuse_active)
	{
		char temp[4096 + 3 * PATH_MAX];
		char res[1024];
		sprintf(temp, "fusermount -u %s", graph->pg_fuse_underlying_graph_mp);
		int ret = __run_command(temp, res, 1024);
		assert(ret == 0);

		sprintf(temp, "fusermount -u %s", graph->pg_fuse_offsets_bin_mp);
		ret = __run_command(temp, res, 1024);
		assert(ret == 0);

		if(graph->graph_type == PARAGRAPHER_CSX_WG_404_AP)
		{
			sprintf(temp, "fusermount -u %s", graph->pg_fuse_graph_mp);
			ret = __run_command(temp, res, 1024);
			assert(ret == 0);
		}

		sprintf(temp, "rm -r %s %s %s", 
			graph->pg_fuse_linked_folder, graph->pg_fuse_underlying_graph_mp, graph->pg_fuse_graph_mp);
		ret = __run_command(temp, res, 1024);
		assert(ret == 0);
	}

	// Releasing graph
		memset(graph, 0, sizeof(__wg_graph));
		free(graph);
		graph = NULL;
		in_graph = NULL;

	return 0;
}

int __wg_get_set_options(paragrapher_graph* in_graph, paragrapher_request_type request_type, void** args, int argc)
{
	__wg_graph* graph = (__wg_graph*)in_graph;

	assert(args != NULL);
	assert(argc >=1 );
	assert(args[0] != NULL);
	
	unsigned long* args0_ulp = (unsigned long*)args[0];
	char* args0_chp = (char*)args[0];
		
	switch(request_type)
	{
		case PARAGRAPHER_REQUEST_GET_GRAPH_PATH:
			sprintf(args0_chp, "%.*s", PATH_MAX, graph->input_name);
			break;

		case PARAGRAPHER_REQUEST_GET_VERTICES_COUNT:
			*args0_ulp = graph->vertices_count;
			break;

		case PARAGRAPHER_REQUEST_GET_EDGES_COUNT:
			*args0_ulp = graph->edges_count;
			break;

		case PARAGRAPHER_REQUEST_LIB_USES_OWN_BUFFERS:
			*args0_ulp = 1;
			break;

		case PARAGRAPHER_REQUEST_LIB_USES_USER_ARRAYS:
			*args0_ulp = 0;
			break;

		case PARAGRAPHER_REQUEST_SET_BUFFER_SIZE:
			{
				// The limits can be removed by updating the Java-side buffer
				unsigned long bsl = 0;
				if(graph->graph_type == PARAGRAPHER_CSX_WG_400_AP)
					bsl = 1UL<< (31 - 2);
				else
					bsl = 1UL<< (31 - 3);

				if(*args0_ulp < bsl)
				{
					graph->buffer_size = *args0_ulp;
					assert(graph->buffer_size >= 1024);
				}
				else
					__PD && printf("[ParaGrapher] SET_BUFFER_SIZE, limit to %'lu\n",bsl);
			}
			break;

		case PARAGRAPHER_REQUEST_GET_BUFFER_SIZE:
			*args0_ulp = graph->buffer_size;
			break;

		case PARAGRAPHER_REQUEST_SET_MAX_BUFFERS_COUNT:
			graph->max_buffers_count = *args0_ulp;
			assert(graph->max_buffers_count > 0);
			break;

		case PARAGRAPHER_REQUEST_GET_MAX_BUFFERS_COUNT:
			*args0_ulp = graph->max_buffers_count;
			break;

		case PARAGRAPHER_REQUEST_READ_STATUS:
		{
			assert(argc >= 2);
			__wg_read_request* req=(__wg_read_request*)args[0];
			assert(req != NULL);
			assert(req->prr.graph != NULL);
			assert(
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_400_AP || 
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_800_AP || 
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_404_AP
			);
			unsigned long* args1_ulp = (unsigned long*)args[1];
			__atomic_store_n(args1_ulp, req->status, __ATOMIC_RELAXED);
			break;
		}

		case PARAGRAPHER_REQUEST_READ_TOTAL_CALLBACKS:
		{
			assert(argc >= 2);
			__wg_read_request* req=(__wg_read_request*)args[0];
			assert(req != NULL);
			assert(req->prr.graph != NULL);
			assert(
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_400_AP || 
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_800_AP || 
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_404_AP
			);
			unsigned long* args1_ulp = (unsigned long*)args[1];
			__atomic_store_n(args1_ulp, req->total_partitions, __ATOMIC_RELAXED);
			break;
		}

		case PARAGRAPHER_REQUEST_READ_EDGES:
		{
				assert(argc >= 2);
			__wg_read_request* req=(__wg_read_request*)args[0];
			assert(req != NULL);
			assert(req->prr.graph != NULL);
			assert(
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_400_AP || 
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_800_AP || 
				req->prr.graph->graph_type == PARAGRAPHER_CSX_WG_404_AP
			);
			unsigned long* args1_ulp = (unsigned long*)args[1];
			__atomic_store_n(args1_ulp, req->total_edges_read, __ATOMIC_RELAXED);
			break;
		}
		
		default:
			return -1;
	}

	return 0;
}

int __wg_check_create_webgraph_offsets_file(__wg_graph* graph)
{
	char cmd[1024 + PATH_MAX], offsets_file[1024 + PATH_MAX],res[PATH_MAX];

	sprintf(offsets_file,"%.*s.offsets", PATH_MAX, graph->underlying_name);
	if(access(offsets_file, F_OK) == 0)
		return 0;

	// Check if the folder is writable
	{
		sprintf(res, "%s.graph", graph->underlying_name);
		char* folder = dirname(res);
		if(access(folder, W_OK) != 0)
		{
			printf("[ParaGrapher] Cannot create the .offsets file. Folder \"%s\" is not writable [%s].\n", folder, strerror(errno));
			exit(-1);
		}
		res[0] = 0;
	}

	if(graph->graph_type == PARAGRAPHER_CSX_WG_400_AP || graph->graph_type == PARAGRAPHER_CSX_WG_404_AP)
		sprintf(cmd, "java -Xmx500G -cp %s/jlibs/*: it.unimi.dsi.webgraph.BVGraph -O %s", getenv("PARAGRAPHER_LIB_FOLDER"), graph->underlying_name);
	else if(graph->graph_type == PARAGRAPHER_CSX_WG_800_AP)
		sprintf(cmd, "java -Xmx500G -cp %s/jlibs/*: it.unimi.dsi.big.webgraph.BVGraph -O %s", getenv("PARAGRAPHER_LIB_FOLDER"), graph->underlying_name);
	else
	{
		assert(0 && "Do not reach here.");
		exit(-1);
	}

	int ret = (int)__run_command(cmd, res, 1023);

	if(access(offsets_file, F_OK) != 0)
	{
		__PD && printf("[ParaGrapher] get_subgraph(), __wg_check_create_webgraph_offsets_file(), ret=%d,\n\"%s\"\n",ret, res);
		return -1;
	}

	printf("[ParaGrapher] WebGraph offsets file created successfully: %s .\n", offsets_file);
	return 0;
}

void* __wg_csx_get_offsets(paragrapher_graph* in_graph, void* offsets, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc)
{
	__wg_graph* graph = (__wg_graph*)in_graph;

	if(start_vertex > graph->vertices_count)
		return NULL;

	if(end_vertex == -1UL)
		end_vertex = graph->vertices_count;
	else
		if(end_vertex > graph->vertices_count)
			return NULL;

	if(graph->offsets != NULL)
		return &graph->offsets[start_vertex];

	// Create *_offsets.bin if it does not exists
	char bin_offsets_file[1024 + PATH_MAX];
	sprintf(bin_offsets_file,"%.*s_offsets.bin", PATH_MAX, graph->underlying_name);

	if(access(bin_offsets_file, F_OK) != 0)
	{
		__PD && printf("[ParaGrapher] paragrapher_csx_get_offsets(), Creating offsets.bin file in \"%s\" \n", bin_offsets_file);

		int ret = __wg_check_create_webgraph_offsets_file(graph);
		if(ret != 0)
			return NULL;

		char res[PATH_MAX];
		// Check if the folder is writable
		{
			sprintf(res, "%s.graph", graph->underlying_name);
			char* folder = dirname(res);
			if(access(folder, W_OK) != 0)
			{
				printf("[ParaGrapher] Cannot create the offsets.bin file. Folder \"%s\" is not writable [%s].\n", folder, strerror(errno));
				exit(-1);
			}
			res[0] = 0;
		}

		char* cmd = calloc(4096 + PATH_MAX * 2 + get_nprocs() / 4, 1);
		assert(cmd != NULL);
		char* PLF=getenv("PARAGRAPHER_LIB_FOLDER");

		char* ts = calloc(1024 + get_nprocs() / 4, 1);
		assert(ts != NULL);
		for(int i = 0; i < get_nprocs() / 4  + 1; i++)
			sprintf(ts + strlen(ts), "F");
		
		if(graph->graph_type == PARAGRAPHER_CSX_WG_400_AP || graph->graph_type == PARAGRAPHER_CSX_WG_404_AP)
			sprintf(cmd, "taskset 0x%s java -Xmx500G -ea -cp %s:%s/jlibs/* WG400AP create_bin_offsets %s %s", ts, PLF, PLF, graph->underlying_name, bin_offsets_file);
		else if(graph->graph_type == PARAGRAPHER_CSX_WG_800_AP)
			sprintf(cmd, "taskset 0x%s java -Xmx500G -ea -cp %s:%s/jlibs/* WG800AP create_bin_offsets %s %s", ts, PLF, PLF, graph->underlying_name, bin_offsets_file);
		else
		{
			assert(0 && "Do not reach here.");
			exit(-1);
		}

		ret = (int)__run_command(cmd, res, 2047);
		if(ret != 0)
		{
			printf("[ParaGrapher] __wg_csx_get_offsets(), ret=%d, res=\n\"%s\"\n",ret, res);
			return NULL;
		}

		chmod(bin_offsets_file, S_IRUSR|S_IRGRP|S_IROTH);

		printf("[ParaGrapher] offsets.bin file created successfully: %s .\n", bin_offsets_file);

		free(cmd);
		cmd = NULL;

		free(ts);
		ts = NULL;
	}

	// Opening 
	int bin_offsets_fd = open(bin_offsets_file, O_RDONLY); 
	if(bin_offsets_fd == -1)
	{
		printf("[ParaGrapher], __wg_csx_get_offsets(), Cannot open bin offsets file, errno: %d, %s\n",errno, strerror(errno));
		close(bin_offsets_fd);
		return NULL;
	}

	// Checking size of offsets.bin file
	{
		struct stat st;
		int ret = fstat(bin_offsets_fd, &st);
		assert(ret == 0);
		if(st.st_size != 8UL * (1 + graph->vertices_count))
		{
			printf("[ParaGrapher], __wg_csx_get_offsets(), offsets.bin file size does not match.\n");
			close(bin_offsets_fd);
			return NULL;
		}
	}

	// Mapping 
	graph->offsets = mmap(NULL, 8UL * (1 + graph->vertices_count), PROT_READ, MAP_PRIVATE, bin_offsets_fd, 0);
	close(bin_offsets_fd);
	if(graph->offsets == MAP_FAILED)
	{
		printf("[ParaGrapher], __wg_csx_get_offsets(), Cannot mmap, errno: %d, %s",errno, strerror(errno));
		return NULL;
	}

	return &graph->offsets[start_vertex];
}

void __wg_csx_release_offsets_weights_arrays(paragrapher_graph* in_graph, void* array)
{
	__sync_synchronize();

	return;
}

void* __wg_java_program_wrapper(void* in)
{
	__wg_read_request* req = (__wg_read_request*) in;
	__wg_graph* graph = (__wg_graph*)req->prr.graph;
					
	char* cmd = calloc(4096 + get_nprocs() / 4 + PATH_MAX, 1);
	assert(cmd != NULL);
	char* PLF=getenv("PARAGRAPHER_LIB_FOLDER");

	char* ts = calloc(1024 + get_nprocs() / 4, 1);
	assert(ts != NULL);
	for(int i = 0; i < get_nprocs() / 4 + 1; i++)
		sprintf(ts + strlen(ts), "F");
		
	if(graph->graph_type == PARAGRAPHER_CSX_WG_400_AP)
		sprintf(cmd, "taskset 0x%s java -Xmx500G -ea -cp %s:%s/jlibs/* WG400AP read_edges %s %s", 
		ts, PLF, PLF, graph->name, req->shm_name);
	else if(graph->graph_type == PARAGRAPHER_CSX_WG_404_AP)
		sprintf(cmd, "taskset 0x%s java -Xmx500G -ea -cp %s:%s/jlibs/* WG404AP read_edges %s %s", 
		ts, PLF, PLF, graph->name, req->shm_name);
	else if(graph->graph_type == PARAGRAPHER_CSX_WG_800_AP)
		sprintf(cmd, "taskset 0x%s java -Xmx500G -ea -cp %s:%s/jlibs/* WG800AP read_edges %s %s", 
		ts, PLF, PLF, graph->name, req->shm_name);
	else
	{
		assert(0 && "Do not reach here.");
		exit(-1);
	}
	
	int ret = system(cmd);
	assert(ret == 0);

	free(cmd);
	cmd = NULL;
	free(ts);
	ts = NULL;

	return NULL;
}

void* __wg_callback_thread(void* in)
{
	void** args = (void**)in;
	__wg_read_request* req = args[0];
	unsigned long buffer_index = (unsigned long)args[1];
	free(args);
	args = NULL;
	in = NULL;

	unsigned int* edges = (unsigned int*)(
		(unsigned char*)&req->buffers_metadata[req->buffers_count] + 
		buffer_index * req->buffer_size * req->bytes_per_edge
	);
	paragrapher_edge_block* eb = (paragrapher_edge_block*)((unsigned long*)(req->buffers_metadata + buffer_index) + 2);
	
	req->callback((paragrapher_read_request*)req, eb, req->offsets, edges, (void*)buffer_index, req->callback_args);
	
	return NULL;
}

void* __wg_thread(void* in)
{
	// Vars
		__wg_read_request* req = (__wg_read_request*) in;
		__wg_graph* graph = (__wg_graph*)req->prr.graph;
		paragrapher_edge_block* eb = &req->eb;

	// Check if WebGraph's .offsets file exists
		int ret = __wg_check_create_webgraph_offsets_file(graph);
		if(ret != 0)
		{
			__PD && printf("[ParaGrapher] get_subgraph(), __wg_thread(), could not create WebGraph offsets file.\n");
			__atomic_store_n(&req->status, -1L, __ATOMIC_RELAXED);
			return NULL;
		}

	// Identifying #buffers
		unsigned long* offsets =	__wg_csx_get_offsets((paragrapher_graph*)graph, NULL, 0, -1UL, NULL, 0);
		assert(offsets != NULL);
		req->offsets = offsets;
		if(eb->start_edge >= offsets[eb->start_vertex + 1] - offsets[eb->start_vertex])
		{
			eb->start_vertex++;
			eb->start_edge = 0;
		}

		if(eb->end_vertex != graph->vertices_count)
		{
			if(eb->end_edge >= offsets[eb->end_vertex + 1] - offsets[eb->end_vertex])
			{
				eb->end_vertex++;
				eb->end_edge = 0;
			}
		}
		else
		{
			eb->end_edge = 0;
		}

		if(offsets[eb->start_vertex] + eb->start_edge > eb->end_edge + offsets[eb->end_vertex])
		{
			__atomic_store_n(&req->status, 1L, __ATOMIC_RELAXED);			
			return NULL;
		}

		req->total_edges_requested = eb->end_edge + offsets[eb->end_vertex] - offsets[eb->start_vertex] - eb->start_edge;
		assert(req->total_edges_requested <= graph->edges_count);

		if(eb->start_vertex == graph->vertices_count || req->total_edges_requested == 0)
		{
			__atomic_store_n(&req->status, 1L, __ATOMIC_RELAXED);			
			return NULL;
		}

		req->total_partitions = req->total_edges_requested / req->buffer_size;
		if(req->total_edges_requested % req->buffer_size != 0)
			req->total_partitions++;

		req->buffers_count = min(req->total_partitions, req->max_buffers_count);
		__PD && printf("[ParaGrapher] get_subgraph(), edges: %'lu, partitions: %lu, buffers: %lu\n", 
			req->total_edges_requested, req->total_partitions, req->buffers_count);
		assert(req->buffers_count != 0);

	// Creating shared memory buffers
		assert(sizeof(__wg_buffer_metadata) == 64);
		assert(req->bytes_per_edge != 0);

		unsigned long shm_size = 0;
		{
			unsigned long ds_size = 0;
			ds_size += 2 * 64; 	// 2 cachelines, one for the C program and one for the Java program
			ds_size += req->buffers_count * sizeof(__wg_buffer_metadata);  // one cacheline to store metadata of each buffer 
			shm_size = ds_size + req->bytes_per_edge * req->buffer_size *  req->buffers_count;
		}

		sprintf(req->shm_name, "paragrapher_wg_%lu", __get_nano_time());
		__PD && printf("[ParaGrapher] get_subgraph(), shm_name: %s, shm_size:%'lu \n", req->shm_name, shm_size);
		void* shm_mem = __create_shm(req->shm_name, shm_size);
		if(shm_mem == NULL)
		{
			__PD && printf("[ParaGrapher] get_subgraph(), __create_shm() error.\n"); 
			__atomic_store_n(&req->status, -1L, __ATOMIC_RELAXED);
			return NULL;
		}
		req->shm_mem = shm_mem;
		req->shm_mem_size = shm_size;
		
	// Setting shm vars and initializing them
		// First cacheline contains C side info
		long* ds_mem = (long*)shm_mem;
		long* C_timestamp = ds_mem;
		long* C_completed = ds_mem + 1;

		*C_timestamp = 1;  // ds_mem[0]
		*C_completed = 0;  // ds_mem[1]
		ds_mem[2] = req->buffers_count;
		ds_mem[3] = req->buffer_size;
		ds_mem[4] = __PD;
		ds_mem[5] = req->bytes_per_edge;

		// Second cacheline for info of J program
		long* J_timestamp = ds_mem + 8;
		*J_timestamp = 0;

		// Buffers metadata
		req->buffers_metadata = (__wg_buffer_metadata*)((char*)shm_mem + 2 * 64);
		for(int b = 0; b < req->buffers_count; b++)
			req->buffers_metadata[b].status = __BS_C_IDLE;
		
	// Running Java program
		pthread_t j_tid = 0;
		{
			int ret = pthread_create(&j_tid, NULL,  __wg_java_program_wrapper, req);
			assert(ret == 0);
		}

	// Requesting buffers and waiting for response from Java program
		unsigned long prev_J_timestamp = 0;
		unsigned long last_vertex = eb->start_vertex;
		unsigned long last_edge = eb->start_edge;
		unsigned long requesting_completed = 0;
		unsigned long completed_partitions = 0;
		unsigned long requested_partitions = 0;
		struct timespec ts = {0, 1000 * 1000}; //0.1,000,000 seconds

		req->total_edges_read = 0;

		while(completed_partitions < req->total_partitions)
		{
			// The sleep time can be a bandwidth bottleneck
			// For t threads, reading e Milion edges, we can optimally read t * 4 * e MB per return from sleep
			// So, for a sleep duration of s, we have a max for bandwidth: t*4*e/s
			nanosleep(&ts, NULL);

			// Check if there is any buffer read completely by the Java program 
			unsigned long t = *J_timestamp;
			if(t != prev_J_timestamp)
			{
				prev_J_timestamp = t;

				// Check buffers whose reading have been completed by the Java program
				// and call the user's callback for them
				for(int b = 0; b < req->buffers_count; b++)
					if(req->buffers_metadata[b].status == __BS_J_READ_COMPLETED)
					{
						req->buffers_metadata[b].status = __BS_C_USER_ACCESS;
						assert(req->buffers_metadata[b].written_edges != 0);
						req->total_edges_read += req->buffers_metadata[b].written_edges;
						
						__PD && printf("[ParaGrapher] __wg_thread(), CT: %ld, JT: %ld, read finished, cp:%lu, b:%u, start: %lu.%lu, end: %lu.%lu, #edges: %'lu\n",
							*C_timestamp,
							prev_J_timestamp,
							completed_partitions,
							b, 
							req->buffers_metadata[b].start_vertex, 
							req->buffers_metadata[b].start_edge, 
							req->buffers_metadata[b].end_vertex, 
							req->buffers_metadata[b].end_edge,
							req->buffers_metadata[b].written_edges
						);

						void** args = calloc(sizeof(void*), 2);
						assert(args != NULL);
						args[0] = req;
						args[1] = (void*)((unsigned long)b);

						pthread_t tid;
						int ret = pthread_create(&tid, NULL,  __wg_callback_thread, args);
						assert(ret == 0);

						completed_partitions++;
					}
			}
			
			// Check if idle buffers can be used to read the remained partitions
			int changes = 0;
			if(!requesting_completed)
				for(int b = 0; b < req->buffers_count; b++)
					if(req->buffers_metadata[b].status == __BS_C_IDLE && !requesting_completed)
					{
						unsigned long dest_vertex = last_vertex;					
						while(
							dest_vertex < eb->end_vertex && 
							offsets[dest_vertex + 1] - offsets[last_vertex] - last_edge < req->buffer_size
						)
							dest_vertex++;

						unsigned long dest_edge = 0;
						if(dest_vertex != graph->vertices_count) 
							dest_edge = req->buffer_size - (offsets[dest_vertex] - (offsets[last_vertex] + last_edge));
						if(dest_vertex == eb->end_vertex && dest_edge > eb->end_edge)
							dest_edge = eb->end_edge;

						req->buffers_metadata[b].start_vertex = last_vertex;
						req->buffers_metadata[b].start_edge = last_edge;
						req->buffers_metadata[b].end_vertex = dest_vertex;
						req->buffers_metadata[b].end_edge = dest_edge;
						__sync_synchronize();
						
						req->buffers_metadata[b].status = __BS_C_REQUESTED;
						
						last_vertex = dest_vertex;
						last_edge = dest_edge;

						if(last_vertex == eb->end_vertex && last_edge == eb->end_edge)
							requesting_completed = 1;

						__PD && printf("[ParaGrapher] __wg_thread(), CT: %ld, JT: %ld, requesting  p:%lu on b:%u, start: %lu.%lu, end: %lu.%lu, #edges: %'lu\n", 
							*C_timestamp,
							prev_J_timestamp,
							requested_partitions,
							b, 
							req->buffers_metadata[b].start_vertex, 
							req->buffers_metadata[b].start_edge, 
							req->buffers_metadata[b].end_vertex, 
							req->buffers_metadata[b].end_edge, 
							offsets[req->buffers_metadata[b].end_vertex] + req->buffers_metadata[b].end_edge - 
							offsets[req->buffers_metadata[b].start_vertex] - req->buffers_metadata[b].start_edge
						);

						requested_partitions++;
						changes++;
					}

			if(changes)
			{
				__sync_synchronize();
				
				long temp = 2 + *C_timestamp;
				if(temp <= 0)
					*C_timestamp = 1;
				else
					*C_timestamp = temp;
			}		
		}
		assert(req->total_edges_requested == req->total_edges_read);

		__PD && printf("[ParaGrapher] __wg_thread(), C: reading finished.\n");

	// Informing Java program to be finished
		*C_completed = 1;
		ret = pthread_join(j_tid, NULL);
		assert(ret == 0);

	// Setting status to completed
		__atomic_store_n(&req->status, 1L, __ATOMIC_RELAXED);

	return NULL;	
}

paragrapher_read_request* __wg_csx_get_subgraph(paragrapher_graph* in_graph, paragrapher_edge_block* eb, void* offsets, void* edges, paragrapher_csx_callback callback, void* callback_args, void** args, int argc)
{
	if(callback == NULL)
	{
		__PD && printf("[ParaGrapher] get_subgraph(), callback function should be passed.\n");
		return NULL;
	}

	__wg_graph* graph = (__wg_graph*)in_graph;

	if(eb->start_vertex >= graph->vertices_count)
		return NULL;

	__wg_read_request* req = calloc(1, sizeof(__wg_read_request));
	assert(req != NULL);
	req->prr.graph = in_graph;
	req->callback = callback;
	req->callback_args = callback_args;
	req->eb.start_vertex = eb->start_vertex;
	req->eb.start_edge = eb->start_edge;
	req->eb.end_vertex = eb->end_vertex;
	req->eb.end_edge = eb->end_edge;
	if(req->eb.end_vertex >= graph->vertices_count)
	{
		req->eb.end_vertex = graph->vertices_count;
		req->eb.end_edge = 0;
	}
	req->status = 0;
	req->buffer_size = graph->buffer_size;
	req->max_buffers_count = graph->max_buffers_count;
	req->start_time = __get_nano_time();

	if(graph->graph_type == PARAGRAPHER_CSX_WG_400_AP)
		req->bytes_per_edge = 4;
	else if(graph->graph_type == PARAGRAPHER_CSX_WG_404_AP || graph->graph_type == PARAGRAPHER_CSX_WG_800_AP)
		req->bytes_per_edge = 8;
	else
	{
		assert(0 && "Do not reach here.");
		exit(-1);
	}
	
	{
		int ret = pthread_create(&req->thread, NULL,  __wg_thread, req);
		assert(ret == 0);
	}

	return (paragrapher_read_request*) req;
}

void __wg_csx_release_read_buffers(paragrapher_read_request* request, paragrapher_edge_block* eb, void* buffer_id)
{
	assert(eb != NULL);

	__sync_synchronize();
	
	__wg_read_request* req = (__wg_read_request*)request;
	unsigned long bi = (unsigned long)buffer_id;
	assert(bi < req->buffers_count);

	req->buffers_metadata[bi].status = __BS_C_IDLE;

	return;
}

void __wg_csx_release_read_request(paragrapher_read_request* request)
{
	__wg_read_request* req = (__wg_read_request*)request;

	// Releasing shm
		assert(req->shm_mem != NULL);
		int ret = munmap(req->shm_mem, req->shm_mem_size);
		assert(ret == 0);
		req->shm_mem = NULL;

		ret = shm_unlink(req->shm_name);
		assert(ret == 0);

		req->buffers_metadata = NULL;
	

	// Releasing req
		memset(req, 0, sizeof(__wg_read_request));
		free(req);
		req = NULL;
		request = NULL;

	return;
}

paragrapher_reader* PARAGRAPHER_CSX_WG_400_AP_init()
{
	paragrapher_reader* lib= calloc(1,sizeof(paragrapher_reader));
	assert(lib != NULL);

	lib->reader_type = PARAGRAPHER_CSX_WG_400_AP;
	lib->open_graph = __wg_open_graph;
	lib->release_graph = __wg_release_graph;
	lib->get_set_options = __wg_get_set_options;
	lib->csx_get_offsets = __wg_csx_get_offsets;
	lib->csx_get_vertex_weights = NULL;
	lib->csx_release_offsets_weights_arrays = __wg_csx_release_offsets_weights_arrays;
	lib->csx_get_subgraph = __wg_csx_get_subgraph;
	lib->csx_release_read_request = __wg_csx_release_read_request;
	lib->csx_release_read_buffers = __wg_csx_release_read_buffers;

	return lib;
}

paragrapher_reader* PARAGRAPHER_CSX_WG_800_AP_init()
{
	paragrapher_reader* lib= calloc(1,sizeof(paragrapher_reader));
	assert(lib != NULL);

	lib->reader_type = PARAGRAPHER_CSX_WG_800_AP;
	lib->open_graph = __wg_open_graph;
	lib->release_graph = __wg_release_graph;
	lib->get_set_options = __wg_get_set_options;
	lib->csx_get_offsets = __wg_csx_get_offsets;
	lib->csx_get_vertex_weights = NULL;
	lib->csx_release_offsets_weights_arrays = __wg_csx_release_offsets_weights_arrays;
	lib->csx_get_subgraph = __wg_csx_get_subgraph;
	lib->csx_release_read_request = __wg_csx_release_read_request;
	lib->csx_release_read_buffers = __wg_csx_release_read_buffers;

	return lib;
}

paragrapher_reader* PARAGRAPHER_CSX_WG_404_AP_init()
{
	paragrapher_reader* lib= calloc(1,sizeof(paragrapher_reader));
	assert(lib != NULL);

	lib->reader_type = PARAGRAPHER_CSX_WG_404_AP;
	lib->open_graph = __wg_open_graph;
	lib->release_graph = __wg_release_graph;
	lib->get_set_options = __wg_get_set_options;
	lib->csx_get_offsets = __wg_csx_get_offsets;
	lib->csx_get_vertex_weights = NULL;
	lib->csx_release_offsets_weights_arrays = __wg_csx_release_offsets_weights_arrays;
	lib->csx_get_subgraph = __wg_csx_get_subgraph;
	lib->csx_release_read_request = __wg_csx_release_read_request;
	lib->csx_release_read_buffers = __wg_csx_release_read_buffers;

	return lib;
}
#endif
