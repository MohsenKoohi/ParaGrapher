/*
JT-CC for WG400AP
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


unsigned long __get_nano_time()
{
	struct timespec ts;
	timespec_get(&ts,TIME_UTC);
	return ts.tv_sec*1e9+ts.tv_nsec;
}

unsigned int* cc = NULL;
unsigned long vertices_count = 0UL;
unsigned long completed_callbacks_count = 0UL;

void callback(paragrapher_read_request* req, paragrapher_edge_block* eb, void* in_offsets, void* in_edges, void* buffer_id, void* args)
{

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

			// Jayanti-Tarjan Weekly Connected Components
			// http://arxiv.org/abs/1612.01514
			{
				unsigned int x = v;
				unsigned int y = dest;

				while(1)
				{
					while(x != cc[x])
						x = cc[x];

					while(y != cc[y])
						y = cc[y];

					if(x == y)
						break;

					if(x < y)
					{
						if(__sync_bool_compare_and_swap(&cc[y], y, x))
							break;
					}
					else
					{
						if(__sync_bool_compare_and_swap(&cc[x], x, y))
							break;
					}
				}
			}

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
	unsigned long t0 = - __get_nano_time();
	printf("\n---------------------\ntest8\n");
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

	// Allocating memory for in_degrees array and for CC
		cc = calloc(sizeof(unsigned int), vertices_count);
		assert(cc != NULL);
		for(unsigned int v = 0; v < vertices_count; v++)
			cc[v] = v;

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

			// printf("  Reading ..., status: %'ld, read_edges: %'lu, completed callbacks: %'u/%'u .\n", status, read_edges, completed_callbacks_count, callbacks_count);
		}
		while(status == 0);

		printf("  Reading graph finished, status: %'ld, read_edges: %'lu, completed callbacks: %'u/%'u .\n", status, read_edges, completed_callbacks_count, callbacks_count);
	

		// Waiting for all buffers to be processed
		while(completed_callbacks_count < callbacks_count)
		{
			nanosleep(&ts, NULL);
			// printf("  Waiting for callbacks ..., completed callbacks: %'u/%'u .\n", completed_callbacks_count, callbacks_count);
		}

		// Releasing the req
		paragrapher_csx_release_read_request(req);
		req = NULL;
	}

	// Releasing the graph
		ret = paragrapher_release_graph(graph, NULL, 0);
		assert(ret == 0);
		graph = NULL;

	{
		// CC
			unsigned int ccs = 0;
			unsigned int* wcc_dist = calloc(sizeof(unsigned int), vertices_count);
			assert(wcc_dist != NULL);

			for(unsigned int v = 0; v < vertices_count; v++)
			{	
				while(cc[cc[v]] != cc[v])
					cc[v] = cc[cc[v]];

				if(cc[v] == v)
					ccs++;

				wcc_dist[cc[v]]++;
			}
			printf("  Number of WCC: %'u\n", ccs);

			unsigned int max_wcc = 0;
			printf("  Output file: obj/test2_wcc_distribution.txt\n");
			FILE* f = fopen("obj/test2_wcc_distribution.txt","w+");
			assert(f != NULL);
			unsigned int total_v = 0;
			for(unsigned int v = 0; v < vertices_count; v++)
			{
				if(wcc_dist[v] > wcc_dist[max_wcc])
					max_wcc = v;

				if(wcc_dist[v])
					fprintf(f, "%u; %u;\n", v, wcc_dist[v]);

				total_v += wcc_dist[v];
			}

			printf("  Largest WCC: %'u\n", wcc_dist[max_wcc]);
			assert(total_v == vertices_count);

			fclose(f);
			f=NULL;
			free(wcc_dist);
			wcc_dist = NULL;

	}
	
	t0 += __get_nano_time();
	printf("\nTotal time: %.2f seconds.\n\n", t0/1e9);	


	return 0;
}
