#ifndef __POPLAR_C
#define __POPLAR_C

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
#include "../include/poplar.h"

struct poplar_graph
{
	poplar_graph_type graph_type;
};

struct poplar_read_request
{
	poplar_graph* graph;
};

typedef struct
{
	poplar_graph_type reader_type;

	// General API
	poplar_graph* (*open_graph)(char* name, poplar_graph_type type, void** args, int argc);
	int (*release_graph)(poplar_graph* graph, void** args, int argc);
	int (*get_set_options)(poplar_graph* graph, poplar_request_type request_type, void** args, int argc);
	
	// CSX API
	void* (*csx_get_offsets)(poplar_graph* graph, void* offsets, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);
	void* (*csx_get_vertex_weights)(poplar_graph* graph, void* weights, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);
	void (*csx_release_offsets_weights_arrays)(poplar_graph* graph, void* array);

	poplar_read_request* (*csx_get_subgraph)(poplar_graph* graph, poplar_edge_block* eb, void* offsets, void* edges, poplar_csx_callback callback, void** args, int argc);
	void (*csx_release_buffers)(poplar_read_request* graph, poplar_edge_block* eb, void* offsets, void* edges);
	void (*csx_release_read_request)(poplar_read_request* request);
	void (*csx_release_read_buffers)(poplar_read_request* request, poplar_edge_block* eb, void* buffer_id);

	// COO API
	poplar_read_request* (*coo_get_edges)(poplar_graph* graph, unsigned long start_row, unsigned long end_row, void* edges, poplar_coo_callback callback, void** args, int argc);

} poploar_reader;

poploar_reader* readers[POPLAR_GRAPH_TYPES_COUNT]={NULL};  

#include "webgraph.c"

int poplar_init()
{
	// __PD && printf("[POPLAR] poplar_init(), Initializing %u reader libraries\n", POPLAR_GRAPH_TYPES_COUNT - 1);
	assert(POPLAR_GRAPH_TYPES_COUNT > 1);
	
	readers[POPLAR_CSX_WG_400_AP] = POPLAR_CSX_WG_400_AP_init();
	readers[POPLAR_CSX_WG_800_AP] = POPLAR_CSX_WG_800_AP_init();
	readers[POPLAR_CSX_WG_404_AP] = POPLAR_CSX_WG_404_AP_init();
	// readers[POPLAR_COO_MM_400_SS] = POPLAR_COO_MM_400_SS_init();
	// readers[POPLAR_COO_MM_404_SS] = POPLAR_COO_MM_404_SS_init();
	
	for(int t = 1; t < POPLAR_GRAPH_TYPES_COUNT; t++)
		if(readers[t]->reader_type != t)
		{
			printf("[POPLAR] poplar_init(),Error in initializing type: %u\n", t);
			return -1;
		}

	return 0;
}

poplar_graph* poplar_open_graph(char* name, poplar_graph_type type, void** args, int argc)
{
	if(type < 1 ||  type >= POPLAR_GRAPH_TYPES_COUNT)
		return NULL;

	assert(strlen(name) < PATH_MAX);
	assert(readers[type]->open_graph != NULL);
	poplar_graph* ret = readers[type]->open_graph(name, type, args, argc);

	if(ret)
		assert(ret->graph_type == type);

	return ret;
}

int poplar_release_graph(poplar_graph* graph, void** args, int argc)
{
	assert(graph != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return -1;

	assert(readers[graph->graph_type]->release_graph != NULL);
	return readers[graph->graph_type]->release_graph(graph, args, argc);
}

int poplar_get_set_options(poplar_graph* graph, poplar_request_type request_type, void** args, int argc)
{
	assert(graph != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return -1;

	if(request_type < 1 || request_type >= POPLAR_REQUESTS_SIZE)
		return -2;

	assert(readers[graph->graph_type]->get_set_options != NULL);
	return readers[graph->graph_type]->get_set_options(graph, request_type, args, argc);
}

void* poplar_csx_get_offsets(poplar_graph* graph, void* offsets, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc)
{
	assert(graph != NULL);

	if(start_vertex > end_vertex)
		return NULL;

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return NULL;

	if(readers[graph->graph_type]->csx_get_offsets == NULL)
		return NULL;

	return readers[graph->graph_type]->csx_get_offsets(graph, offsets, start_vertex, end_vertex, args, argc);
}

void* poplar_csx_get_vertex_weights(poplar_graph* graph, void* weights, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc)
{
	assert(graph != NULL);
	
	if(start_vertex > end_vertex)
		return NULL;

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return NULL;

	if(readers[graph->graph_type]->csx_get_vertex_weights == NULL)
		return NULL;

	return readers[graph->graph_type]->csx_get_vertex_weights(graph, weights, start_vertex, end_vertex, args, argc);
}

void poplar_csx_release_offsets_weights_arrays(poplar_graph* graph, void* array)
{
	assert(graph != NULL);
	assert(array != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return;

	if(readers[graph->graph_type]->csx_get_vertex_weights == NULL)
		return;

	readers[graph->graph_type]->csx_release_offsets_weights_arrays(graph, array);

	return;
}

poplar_read_request* poplar_csx_get_subgraph(poplar_graph* graph, poplar_edge_block* eb, void* offsets, void* edges, poplar_csx_callback callback, void** args, int argc)
{
	assert(graph != NULL);
	assert(eb != NULL);

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return NULL;

	if(readers[graph->graph_type]->csx_get_subgraph == NULL)
		return NULL;

	if(eb->start_vertex > eb->end_vertex)
		return NULL;

	if(eb->start_vertex == eb->end_vertex && eb->start_edge >= eb->end_edge)
		return NULL;

	poplar_read_request* ret = readers[graph->graph_type]->csx_get_subgraph(graph, eb, offsets, edges, callback, args, argc);
	assert(ret->graph == graph);

	return ret;
}

void poplar_csx_release_read_request(poplar_read_request* request)
{
	if(request == NULL)
		return;

	poplar_graph* graph = request->graph;

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return;

	if(readers[graph->graph_type]->csx_release_read_request == NULL)
		return;

	readers[graph->graph_type]->csx_release_read_request(request);
	
	return;
}

void poplar_csx_release_read_buffers(poplar_read_request* request, poplar_edge_block* eb, void* buffer_id)
{
	assert(request != NULL);

	poplar_graph* graph = request->graph;

	if(graph->graph_type < 1 ||  graph->graph_type >= POPLAR_GRAPH_TYPES_COUNT)
		return;

	if(readers[graph->graph_type]->csx_release_read_buffers == NULL)
		return;

	readers[graph->graph_type]->csx_release_read_buffers(request, eb, buffer_id);
	
	return;
}

#endif