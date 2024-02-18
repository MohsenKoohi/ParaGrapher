/*
This program reads a WebGraph asynchronously (POPLAR_CSX_WG_400_AP) 
and writes it in uncompressed binary format 
*/

#include "poplar.h"

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

char output_file[1024] = {0};
unsigned long vertices_count = 0UL;
unsigned long completed_callbacks_count = 0UL;
unsigned long processed_edges = 0UL;

void callback(poplar_read_request* req, poplar_edge_block* eb, void* in_offsets, void* in_edges, void* buffer_id, void* args)
{
	// unsigned long bi= (unsigned long)buffer_id;
	// printf("Callback for bi: %lu,  eb: %lu.%lu - %lu.%lu\n", bi, eb->start_vertex, eb->start_edge, eb->end_vertex, eb->end_edge);

	unsigned long* offsets = (unsigned long*)in_offsets;
	unsigned char* buf = (unsigned char*)in_edges;
	unsigned long ec = offsets[eb->end_vertex] + eb->end_edge - offsets[eb->start_vertex] - eb->start_edge;
	
	int fd = open(output_file, O_RDWR); 
	assert(fd > 0);	
	
	unsigned long ei_offset = 
		sizeof(unsigned long) * (2 + vertices_count + 1) + 
		sizeof(unsigned int) * (offsets[eb->start_vertex] + eb->start_edge)
	;
	unsigned long new_offset = lseek(fd, ei_offset, SEEK_SET);
	assert(new_offset == ei_offset);

	unsigned long written_bytes = 0;
	unsigned long total_bytes = ec * sizeof(unsigned int);
	while(written_bytes != total_bytes)
	{
		ssize_t wret = write(fd, buf + written_bytes, total_bytes - written_bytes);
		assert(wret != -1);
		written_bytes += wret;
	}

	close(fd);
	fd = -1;

	__atomic_add_fetch(&processed_edges, ec, __ATOMIC_RELAXED);

	poplar_csx_release_read_buffers(req, eb, buffer_id);

	__atomic_add_fetch(&completed_callbacks_count, 1UL, __ATOMIC_RELAXED);

	return;
}

int main(int argc, char** args)
{	
	printf("\n---------------------\ntest2_WG400\n");
	for(int i=0; i< argc; i++)
		printf("  args[%d]: %s\n",i, args[i]);

	sprintf(output_file, "obj/test2.bin");
	printf("  output_file: %s\n", output_file);

	setlocale(LC_NUMERIC, "");
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	int ret = poplar_init();
	assert(ret == 0);

	poplar_graph* graph = poplar_open_graph(args[1], POPLAR_CSX_WG_400_AP, NULL, 0);
	assert(graph != NULL);

	unsigned long edges_count = 0;
	{
		void* op_args []= {&vertices_count, &edges_count};

		ret = poplar_get_set_options(graph, POPLAR_REQUEST_GET_VERTICES_COUNT, op_args, 1);
		assert (ret == 0);
		ret = poplar_get_set_options(graph, POPLAR_REQUEST_GET_EDGES_COUNT, op_args + 1, 1);
		assert (ret == 0);
		printf("  Vertices: %'lu\n", vertices_count);
		printf("  Edges:    %'lu\n\n", edges_count);

		char name[1024];
		op_args[0] = name;
		ret = poplar_get_set_options(graph, POPLAR_REQUEST_GET_GRAPH_PATH, op_args, 1);
		assert (ret == 0);
		printf("  GET_GRAPH_PATH:        %'s\n", name);
		
		// Check if the related library uses its own buffers or the user arrays
		unsigned long val = 0;
		op_args[0] = &val;
		ret = poplar_get_set_options(graph, POPLAR_REQUEST_LIB_USES_OWN_BUFFERS, op_args, 1);
		assert (ret == 0);
		printf("  LIB_USES_OWN_BUFFERS:  %'lu\n", val);
		ret = poplar_get_set_options(graph, POPLAR_REQUEST_LIB_USES_USER_ARRAYS, op_args, 1);
		assert (ret == 0);
		printf("  LIB_USES_USER_ARRAYS:  %'lu\n", val);
		
		// Check buffer size and set it
		ret = poplar_get_set_options(graph, POPLAR_REQUEST_GET_BUFFER_SIZE, op_args, 1);
		assert (ret == 0);
		printf("  GET_BUFFER_SIZE:       %'lu\n", val);
		int set_bc = 0;
		if(val > edges_count && edges_count > 1024)
		{	
			val = 1UL << (unsigned int)(log(edges_count)/log(2) - 3);
			set_bc = 8;
			ret = poplar_get_set_options(graph, POPLAR_REQUEST_SET_BUFFER_SIZE, op_args, 1);
			assert (ret == 0);
			ret = poplar_get_set_options(graph, POPLAR_REQUEST_GET_BUFFER_SIZE, op_args, 1);
			assert (ret == 0);
			printf("  GET_BUFFER_SIZE:       %'lu\n", val);
		}

		// Check max number of buffers and set it
		ret = poplar_get_set_options(graph, POPLAR_REQUEST_GET_MAX_BUFFERS_COUNT, op_args, 1);
		assert (ret == 0);
		printf("  GET_MAX_BUFFERS_COUNT: %'lu\n", val);
		if(set_bc)
		{
			val = set_bc;
			ret = poplar_get_set_options(graph, POPLAR_REQUEST_SET_MAX_BUFFERS_COUNT, op_args, 1);
			assert (ret == 0);
			ret = poplar_get_set_options(graph, POPLAR_REQUEST_GET_MAX_BUFFERS_COUNT, op_args, 1);
			assert (ret == 0);
			printf("  GET_MAX_BUFFERS_COUNT: %'lu\n", val);		
		}
	}

	// Getting the offsets
		unsigned long* offsets = (unsigned long*)poplar_csx_get_offsets(graph, NULL, 0, -1UL, NULL, 0);
		assert(offsets != NULL);

		printf("\n  First Degrees: ");
		for(unsigned int v= 0; v < 30; v++)
			printf("%u, ", (unsigned int)(offsets[v + 1] - offsets[v]));
		printf("\n");

	// Writing to the file
	{
		int fd = open(output_file, O_RDWR|O_CREAT|O_TRUNC, 0644); 
		assert(fd > 0);

		int ret = ftruncate(fd, sizeof(unsigned long) * (2 + vertices_count + 1) + edges_count * sizeof(unsigned int));
		assert(ret == 0);

		unsigned long temp[2] = {vertices_count, edges_count};
		ssize_t wret = write(fd, temp, 16);
		assert(wret == 16);

		char* offsets_buf = (char*)offsets;
		unsigned long written_bytes = 0;
		unsigned long total_bytes = (vertices_count + 1) * sizeof(unsigned long);
		while(written_bytes != total_bytes)
		{
			wret = write(fd, offsets_buf + written_bytes, total_bytes - written_bytes);
			assert(wret != -1);
			written_bytes += wret;
		}

		close(fd);
		fd = -1;
	}

	// Releasing the offsets array
		poplar_csx_release_offsets_weights_arrays(graph, offsets);
		offsets = NULL;
	

	// Reading the graph
	{
		printf("\n  Reading graph ...\n");

		poplar_edge_block eb;
		eb.start_vertex = 0;
		eb.start_edge=0;
		eb.end_vertex = -1UL;
		eb.end_edge= -1UL;


		poplar_read_request* req= poplar_csx_get_subgraph(graph, &eb, NULL, NULL, callback, NULL, NULL, 0);
		assert(req != NULL);

		struct timespec ts = {0, 200 * 1000 * 1000};
		long status = 0;
		unsigned long read_edges = 0;
		unsigned long callbacks_count = 0;
		void* op0_args []= {req, &status};
		void* op1_args []= {req, &read_edges};
		void* op2_args []= {req, &callbacks_count};

		do
		{
			nanosleep(&ts, NULL);
			
			ret = poplar_get_set_options(graph, POPLAR_REQUEST_READ_STATUS, op0_args, 2);
			assert (ret == 0);
			ret = poplar_get_set_options(graph, POPLAR_REQUEST_READ_EDGES, op1_args, 2);
			assert (ret == 0);
			if(callbacks_count == 0)
			{
				ret = poplar_get_set_options(graph, POPLAR_REQUEST_READ_TOTAL_CALLBACKS, op2_args, 2);
				assert (ret == 0);
			}

			printf("  Reading ..., status: %'ld, read_edges: %'lu, completed callbacks: %'u/%'u .\n", status, read_edges, completed_callbacks_count, callbacks_count);
		}
		while(status == 0);

		printf("  Reading graph finished, status: %'ld, read_edges: %'lu, completed callbacks: %'u/%'u .\n", status, read_edges, completed_callbacks_count, callbacks_count);
	

		// Waiting for all buffers to be processed
		while(completed_callbacks_count < callbacks_count)
		{
			nanosleep(&ts, NULL);
			printf("  Waiting for callbacks ..., completed callbacks: %'u/%'u .\n", completed_callbacks_count, callbacks_count);
		}

		// Releasing the req
		poplar_csx_release_read_request(req);
		req = NULL;
	}

	printf("  Processed edges: %'lu\n", processed_edges);
	assert(processed_edges == edges_count);

	// Releasing the graph
		ret = poplar_release_graph(graph, NULL, 0);
		assert(ret == 0);
		graph = NULL;

	printf("\n  Decompressing and saving in binary format finished successfully.\n");
	
	printf("---------------------\n");
	
	return 0;
}
