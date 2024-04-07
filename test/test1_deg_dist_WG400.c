/*
This program reads a WebGraph asynchronously (PARAGRAPHER_CSX_WG_400_AP) 
and produces the degree distribution of the transposed graph and
The callback funcion process the read block of edges and release them.
The main thread waits for callbacks to be finished and then produces the outputs.
*/

#include "paragrapher.h"

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

unsigned int* in_degrees = NULL;
unsigned long vertices_count = 0UL;
unsigned long completed_callbacks_count = 0UL;
unsigned long processed_edges = 0UL;

void callback(paragrapher_read_request* req, paragrapher_edge_block* eb, void* in_offsets, void* in_edges, void* buffer_id, void* args)
{
	// unsigned long bi= (unsigned long)buffer_id;
	// printf("Callback for bi: %lu,  eb: %lu.%lu - %lu.%lu\n", bi, eb->start_vertex, eb->start_edge, eb->end_vertex, eb->end_edge);

	unsigned long* offsets = (unsigned long*)in_offsets;
	unsigned int* edges = (unsigned int*)in_edges;
	unsigned long buffer_ec = offsets[eb->end_vertex] + eb->end_edge - offsets[eb->start_vertex] - eb->start_edge;
	// printf("buffer_ec: %lu\n", buffer_ec);

	unsigned long ei_offset = offsets[eb->start_vertex] + eb->start_edge;

	for(unsigned int v = eb->start_vertex; v <= eb->end_vertex; v++)
	{
		// Identifying the start and the end edge of v that are in the current partition
			unsigned int start_edge_index = 0;
			if(v == eb->start_vertex)
				start_edge_index = eb->start_edge;

			unsigned int end_edge_index = 0;
			if(v == eb->end_vertex)
				end_edge_index = eb->end_edge;
			else
				end_edge_index = offsets[v + 1] - offsets[v];

		// Processing the edges
		for(unsigned int e = start_edge_index; e < end_edge_index; e++)
		{
			unsigned long edge_index = offsets[v] + e - ei_offset;
			unsigned int dest = edges[edge_index];
			
			// Incrementing the in-degree of the dest vertex
			__atomic_add_fetch(in_degrees + dest, 1U, __ATOMIC_RELAXED);

			__atomic_add_fetch(&processed_edges, 1UL, __ATOMIC_RELAXED);
		}
	}

	paragrapher_csx_release_read_buffers(req, eb, buffer_id);

	// if(completed_callbacks_count > 10)
	// 	sleep(5);

	__atomic_add_fetch(&completed_callbacks_count, 1UL, __ATOMIC_RELAXED);
	return;
}

int main(int argc, char** args)
{	
	printf("\n---------------------\ntest1_WG400\n");
	for(int i=0; i< argc; i++)
		printf("  args[%d]: %s\n",i, args[i]);

	setlocale(LC_NUMERIC, "");
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	int ret = paragrapher_init();
	assert(ret == 0);

	paragrapher_graph* graph = paragrapher_open_graph(args[1], PARAGRAPHER_CSX_WG_400_AP, NULL, 0);
	assert(graph != NULL);

	unsigned long edges_count = 0;
	{
		void* op_args []= {&vertices_count, &edges_count};

		ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_GET_VERTICES_COUNT, op_args, 1);
		assert (ret == 0);
		ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_GET_EDGES_COUNT, op_args + 1, 1);
		assert (ret == 0);
		printf("  Vertices: %'lu\n", vertices_count);
		printf("  Edges:    %'lu\n\n", edges_count);

		char name[1024];
		op_args[0] = name;
		ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_GET_GRAPH_PATH, op_args, 1);
		assert (ret == 0);
		printf("  GET_GRAPH_PATH:        %'s\n", name);
		
		// Check if the related library uses its own buffers or the user arrays
		unsigned long val = 0;
		op_args[0] = &val;
		ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_LIB_USES_OWN_BUFFERS, op_args, 1);
		assert (ret == 0);
		printf("  LIB_USES_OWN_BUFFERS:  %'lu\n", val);
		ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_LIB_USES_USER_ARRAYS, op_args, 1);
		assert (ret == 0);
		printf("  LIB_USES_USER_ARRAYS:  %'lu\n", val);
		
		// Check buffer size and set it
		ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_GET_BUFFER_SIZE, op_args, 1);
		assert (ret == 0);
		printf("  GET_BUFFER_SIZE:       %'lu\n", val);
		int set_bc = 0;
		if(val > edges_count && edges_count > 1024)
		{	
			val = 1UL << (unsigned int)(log(edges_count)/log(2) - 3);
			set_bc = 8;
			ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_SET_BUFFER_SIZE, op_args, 1);
			assert (ret == 0);
			ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_GET_BUFFER_SIZE, op_args, 1);
			assert (ret == 0);
			printf("  GET_BUFFER_SIZE:       %'lu\n", val);
		}

		// Check max number of buffers and set it
		ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_GET_MAX_BUFFERS_COUNT, op_args, 1);
		assert (ret == 0);
		printf("  GET_MAX_BUFFERS_COUNT: %'lu\n", val);
		if(set_bc)
		{
			val = set_bc;
			ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_SET_MAX_BUFFERS_COUNT, op_args, 1);
			assert (ret == 0);
			ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_GET_MAX_BUFFERS_COUNT, op_args, 1);
			assert (ret == 0);
			printf("  GET_MAX_BUFFERS_COUNT: %'lu\n", val);		
		}
	}

	// Allocating memory
		in_degrees = calloc(sizeof(unsigned int), vertices_count);
		assert(in_degrees != NULL);


	// Getting the offsets
	{
		unsigned long* offsets = (unsigned long*)paragrapher_csx_get_offsets(graph, NULL, 0, -1UL, NULL, 0);
		assert(offsets != NULL);

		printf("\n  First Degrees: ");
		for(unsigned int v= 0; v < 30; v++)
			printf("%u, ", (unsigned int)(offsets[v + 1] - offsets[v]));
		printf("\n");

		paragrapher_csx_release_offsets_weights_arrays(graph, offsets);
		offsets = NULL;
	}

	// Reading the graph
	{
		printf("\n  Reading graph ...\n");

		paragrapher_edge_block eb;
		eb.start_vertex = 0;
		eb.start_edge=0;
		eb.end_vertex = -1UL;
		eb.end_edge= -1UL;


		paragrapher_read_request* req= paragrapher_csx_get_subgraph(graph, &eb, NULL, NULL, callback, NULL,NULL, 0);
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
			
			ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_READ_STATUS, op0_args, 2);
			assert (ret == 0);
			ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_READ_EDGES, op1_args, 2);
			assert (ret == 0);
			if(callbacks_count == 0)
			{
				ret = paragrapher_get_set_options(graph, PARAGRAPHER_REQUEST_READ_TOTAL_CALLBACKS, op2_args, 2);
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
		paragrapher_csx_release_read_request(req);
		req = NULL;
	}

	// Releasing the graph
		ret = paragrapher_release_graph(graph, NULL, 0);
		assert(ret == 0);
		graph = NULL;

	// Identifying the max in_degree, calculating in-degree distribution
	{
		printf("  Processed edges: %'lu\n", processed_edges);
		assert(processed_edges == edges_count);

		// Max in-degree
			unsigned int max_in_degree = 0;
			for(unsigned int v = 0; v < vertices_count; v++)
				if(in_degrees[v] > max_in_degree)
					max_in_degree = in_degrees[v];
			printf("  Max in-degree: %'u\n", max_in_degree);

		// In-degree Distribution
			unsigned int* dist = calloc(sizeof(unsigned int), 1 + max_in_degree);
			assert(dist != NULL);
			for(unsigned int v = 0; v < vertices_count; v++)
				dist[in_degrees[v]]++;
			
			printf("  Output file: obj/test1_in_degree_distribution.txt\n");
			FILE* f = fopen("obj/test1_in_degree_distribution.txt","w+");
			assert(f != NULL);
			for(unsigned int d = 0; d <= max_in_degree; d++)
				if(dist[d])
					fprintf(f, "%u; %u;\n", d, dist[d]);
			fclose(f);
			f=NULL;
			free(dist);
			dist = NULL;
			free(in_degrees);
			in_degrees = NULL;
	}

	printf("---------------------\n");
	
	return 0;
}
