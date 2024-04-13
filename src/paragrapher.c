#ifndef __PARAGRAPHER_PARAGRAPHER_C
#define __PARAGRAPHER_PARAGRAPHER_C

#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <limits.h>

#ifdef NDEBUG
	#define __PD 0 
#else
	#define __PD 1
#endif

#include "aux.c"
#include "../include/paragrapher.h"

struct paragrapher_graph
{
	paragrapher_graph_type graph_type;
};

struct paragrapher_read_request
{
	paragrapher_graph* graph;
};

typedef struct
{
	paragrapher_graph_type reader_type;

	// General API
	paragrapher_graph* (*open_graph)(char* name, paragrapher_graph_type type, void** args, int argc);
	int (*release_graph)(paragrapher_graph* graph, void** args, int argc);
	int (*get_set_options)(paragrapher_graph* graph, paragrapher_request_type request_type, void** args, int argc);
	
	// CSX API
	void* (*csx_get_offsets)(paragrapher_graph* graph, void* offsets, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);
	void* (*csx_get_vertex_weights)(paragrapher_graph* graph, void* weights, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);
	void (*csx_release_offsets_weights_arrays)(paragrapher_graph* graph, void* array);

	paragrapher_read_request* (*csx_get_subgraph)(paragrapher_graph* graph, paragrapher_edge_block* eb, void* offsets, void* edges, paragrapher_csx_callback callback, void* callback_args, void** args, int argc);
	void (*csx_release_buffers)(paragrapher_read_request* graph, paragrapher_edge_block* eb, void* offsets, void* edges);
	void (*csx_release_read_request)(paragrapher_read_request* request);
	void (*csx_release_read_buffers)(paragrapher_read_request* request, paragrapher_edge_block* eb, void* buffer_id);

	// COO API
	paragrapher_read_request* (*coo_get_edges)(paragrapher_graph* graph, unsigned long start_row, unsigned long end_row, void* edges, paragrapher_coo_callback callback, void* callback_args, void** args, int argc);

} paragrapher_reader;

static paragrapher_reader* readers[PARAGRAPHER_GRAPH_TYPES_COUNT]={NULL};  

#include "webgraph.c"

int paragrapher_init()
{
	// __PD && printf("[PARAGRAPHER] paragrapher_init(), Initializing %u reader libraries\n", PARAGRAPHER_GRAPH_TYPES_COUNT - 1);
	assert(PARAGRAPHER_GRAPH_TYPES_COUNT > 1);
	
	if(readers[PARAGRAPHER_CSX_WG_400_AP] != NULL)
		return 0;	
	
	readers[PARAGRAPHER_CSX_WG_400_AP] = PARAGRAPHER_CSX_WG_400_AP_init();
	readers[PARAGRAPHER_CSX_WG_800_AP] = PARAGRAPHER_CSX_WG_800_AP_init();
	readers[PARAGRAPHER_CSX_WG_404_AP] = PARAGRAPHER_CSX_WG_404_AP_init();
	// readers[PARAGRAPHER_COO_MM_400_SS] = PARAGRAPHER_COO_MM_400_SS_init();
	// readers[PARAGRAPHER_COO_MM_404_SS] = PARAGRAPHER_COO_MM_404_SS_init();
	
	for(int t = 1; t < PARAGRAPHER_GRAPH_TYPES_COUNT; t++)
		if(readers[t]->reader_type != t)
		{
			printf("[PARAGRAPHER] paragrapher_init(),Error in initializing type: %u\n", t);
			return -1;
		}

	return 0;
}

paragrapher_graph* paragrapher_open_graph(char* name, paragrapher_graph_type type, void** args, int argc)
{
	if(type < 1 ||  type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return NULL;

	assert(strlen(name) < PATH_MAX);
	assert(readers[type]->open_graph != NULL);
	paragrapher_graph* ret = readers[type]->open_graph(name, type, args, argc);

	if(ret)
		assert(ret->graph_type == type);

	return ret;
}

int paragrapher_release_graph(paragrapher_graph* graph, void** args, int argc)
{
	assert(graph != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return -1;

	assert(readers[graph->graph_type]->release_graph != NULL);
	return readers[graph->graph_type]->release_graph(graph, args, argc);
}

int paragrapher_get_set_options(paragrapher_graph* graph, paragrapher_request_type request_type, void** args, int argc)
{
	assert(graph != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return -1;

	if(request_type < 1 || request_type >= PARAGRAPHER_REQUESTS_SIZE)
		return -2;

	assert(readers[graph->graph_type]->get_set_options != NULL);
	return readers[graph->graph_type]->get_set_options(graph, request_type, args, argc);
}

void* paragrapher_csx_get_offsets(paragrapher_graph* graph, void* offsets, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc)
{
	assert(graph != NULL);

	if(start_vertex > end_vertex)
		return NULL;

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return NULL;

	if(readers[graph->graph_type]->csx_get_offsets == NULL)
		return NULL;

	return readers[graph->graph_type]->csx_get_offsets(graph, offsets, start_vertex, end_vertex, args, argc);
}

void* paragrapher_csx_get_vertex_weights(paragrapher_graph* graph, void* weights, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc)
{
	assert(graph != NULL);
	
	if(start_vertex > end_vertex)
		return NULL;

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return NULL;

	if(readers[graph->graph_type]->csx_get_vertex_weights == NULL)
		return NULL;

	return readers[graph->graph_type]->csx_get_vertex_weights(graph, weights, start_vertex, end_vertex, args, argc);
}

void paragrapher_csx_release_offsets_weights_arrays(paragrapher_graph* graph, void* array)
{
	assert(graph != NULL);
	assert(array != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return;

	if(readers[graph->graph_type]->csx_get_vertex_weights == NULL)
		return;

	readers[graph->graph_type]->csx_release_offsets_weights_arrays(graph, array);

	return;
}

paragrapher_read_request* paragrapher_csx_get_subgraph(paragrapher_graph* graph, paragrapher_edge_block* eb, void* offsets, void* edges, paragrapher_csx_callback callback, void* callback_args, void** args, int argc)
{
	assert(graph != NULL);
	assert(eb != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return NULL;

	if(readers[graph->graph_type]->csx_get_subgraph == NULL)
		return NULL;

	if(eb->start_vertex > eb->end_vertex)
		return NULL;

	if(eb->start_vertex == eb->end_vertex && eb->start_edge > eb->end_edge)
		return NULL;

	paragrapher_read_request* ret = readers[graph->graph_type]->csx_get_subgraph(graph, eb, offsets, edges, callback, callback_args, args, argc);
	assert(ret->graph == graph);

	return ret;
}

void paragrapher_csx_release_read_request(paragrapher_read_request* request)
{
	if(request == NULL)
		return;

	paragrapher_graph* graph = request->graph;

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return;

	if(readers[graph->graph_type]->csx_release_read_request == NULL)
		return;

	readers[graph->graph_type]->csx_release_read_request(request);
	
	return;
}

void paragrapher_csx_release_read_buffers(paragrapher_read_request* request, paragrapher_edge_block* eb, void* buffer_id)
{
	assert(request != NULL);

	paragrapher_graph* graph = request->graph;

	if(graph->graph_type < 1 ||  graph->graph_type >= PARAGRAPHER_GRAPH_TYPES_COUNT)
		return;

	if(readers[graph->graph_type]->csx_release_read_buffers == NULL)
		return;

	readers[graph->graph_type]->csx_release_read_buffers(request, eb, buffer_id);
	
	return;
}

#endif