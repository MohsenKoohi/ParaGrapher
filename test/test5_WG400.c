/*
This program reads a WebGraph asynchronously (POPLAR_CSX_WG_400_AP) 
and writes it in 3 formats:
	(1, g3) Binary format Graptor V3
	(2, el) Textual COO edge list (el) format  https://github.com/sbeamer/gapbs/blob/master/src/writer.h#L32
	(3, ad) Textual Adjacency Graph  https://github.com/ParAlg/gbbs?tab=readme-ov-file#input-formats


Graptor V3:
	It has a 64 bytes header:

	uint64_t header[8] = {
	    3, // version
	    1,
	    (uint64_t)n, // num nodes
	    (uint64_t)m, // num edges
	    sizeof(VID), // sizeof(VID) = 4
	    sizeof(EID), // sizeof(EID) = 8
	    0, // unused
	    0, // unused
	};
	
	that is followed by
		|V|+1 elements, each of 8 bytes, for offsets_lists and
		|E| elements, each of 4 bytes, for edges_list 

	https://hpdc-gitlab.eeecs.qub.ac.uk/hvandierendonck/ligra-partition/blob/graptor2/include/graptor/graph/GraphCSx.h#L728

*/

#include "poplar.h"

#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <locale.h>
#include <libgen.h>
#include <linux/limits.h>

unsigned long __get_nano_time()
{
	struct timespec ts;
	timespec_get(&ts,TIME_UTC);
	return ts.tv_sec*1e9+ts.tv_nsec;
}

unsigned int* g3_edges_array = NULL;
unsigned long vertices_count = 0UL;
unsigned long completed_callbacks_count = 0UL;
unsigned long processed_edges = 0UL;

void callback(poplar_read_request* req, poplar_edge_block* eb, void* in_offsets, void* in_edges, void* buffer_id, void* args)
{
	// unsigned long bi= (unsigned long)buffer_id;
	// printf("Callback for bi: %lu,  eb: %lu.%lu - %lu.%lu\n", bi, eb->start_vertex, eb->start_edge, eb->end_vertex, eb->end_edge);

	unsigned long* offsets = (unsigned long*)in_offsets;
	unsigned int* edges = (unsigned int*)in_edges;
	unsigned long ec = offsets[eb->end_vertex] + eb->end_edge - offsets[eb->start_vertex] - eb->start_edge;
	unsigned long g3_offset = offsets[eb->start_vertex] + eb->start_edge;
	
	for(unsigned long e = 0; e < ec; e++)
		g3_edges_array[g3_offset + e] = edges[e];
	
	poplar_csx_release_read_buffers(req, eb, buffer_id);

	__atomic_add_fetch(&processed_edges, ec, __ATOMIC_RELAXED);
	__atomic_add_fetch(&completed_callbacks_count, 1UL, __ATOMIC_RELAXED);

	return;
}

int main(int argc, char** args)
{	
	// Initializing vars
		printf("\n---------------------\ntest2_WG400\n");
		for(int i=0; i< argc; i++)
			printf("  args[%d]: %s\n",i, args[i]);
		assert(argc > 1);

		char g3_file[1024] = {0};
		char el_file[1024] = {0};
		char ad_file[1024] = {0};
		sprintf(g3_file, "%s.g3_bin", args[1]);
		printf("  g3_file: %s\n", g3_file);
		sprintf(el_file, "%s.el", args[1]);
		printf("  el_file: %s\n", el_file);
		sprintf(ad_file, "%s.adj", args[1]);
		printf("  ad_file: %s\n", ad_file);

		setlocale(LC_NUMERIC, "");
		setbuf(stdout, NULL);
		setbuf(stderr, NULL);

	// Opening the graph
		unsigned long t0 = -__get_nano_time();
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

			unsigned long val = 0;
			op_args[0] = &val;

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

	// Writing to the g3_file
		unsigned long g3_graph_size = sizeof(unsigned long) * (8 + vertices_count + 1) + edges_count * sizeof(unsigned int);
		unsigned long* g3_graph = NULL;
		unsigned long* g3_offsets_array = NULL;
		{
			int fd = open(g3_file, O_RDWR|O_CREAT|O_TRUNC, 0644); 
			assert(fd > 0);

			int ret = ftruncate(fd, g3_graph_size);
			assert(ret == 0);

			g3_graph = (unsigned long*) mmap(NULL, g3_graph_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if(g3_graph == MAP_FAILED)
			{
				printf("\n\033[1;31mcan't get graph -> mmap error : %d, %s, %lx .\033[0;37m \n", errno, strerror(errno), g3_graph );
				assert (g3_graph != MAP_FAILED);
			}
			close(fd);

			unsigned long* g3_header = g3_graph;
			g3_header[0] = 3;
			g3_header[1] = 1;
			g3_header[2] = vertices_count;
			g3_header[3] = edges_count;
			g3_header[4] = 4;
			g3_header[5] = 8;
			g3_header[6] = 0;
			g3_header[7] = 0;

			g3_offsets_array = g3_graph + 8;
			g3_edges_array = (unsigned int*)(g3_graph + 8 + vertices_count + 1);

			for(unsigned int v = 0; v <= vertices_count; v++)
				g3_offsets_array[v] = offsets[v];
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

		unsigned long last_print = 0;
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

			unsigned long nt =  __get_nano_time();
			if( nt - last_print > 2e9)
			{
				printf("  Reading ..., status: %'ld, read_edges: %'lu, completed callbacks: %'u/%'u .\n", status, read_edges, completed_callbacks_count, callbacks_count);
				last_print =  nt;
			}
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
	t0 += __get_nano_time();

	printf("  Processed edges: %'lu\n", processed_edges);
	assert(processed_edges == edges_count);

	// Releasing the graph
		ret = poplar_release_graph(graph, NULL, 0);
		assert(ret == 0);
		graph = NULL;

	printf("\n  Decompressing and saving in g3_bin finished successfully in %'.2f seconds.\n", t0/1e9);
	printf("---------------------\n");

	// Writing the el and ad files
  long sum = 0;
	{
		t0 = - __get_nano_time();
		
		FILE* ad = fopen(ad_file, "w");
		assert(ad != NULL);
		fprintf(ad, "AdjacencyGraph\n%lu\n%lu\n", vertices_count, edges_count);

		for(unsigned int v = 0; v < vertices_count; v++)
			fprintf(ad, "%lu\n", g3_offsets_array[v]);		

		FILE* el = fopen(el_file, "w");
    assert(el != NULL);

    for(unsigned int v = 0; v < vertices_count; v++)
		{
			sum += g3_offsets_array[v + 1] - g3_offsets_array[v];
			unsigned long last_val = v + 1; 

			for(unsigned long e = g3_offsets_array[v]; e < g3_offsets_array[v + 1]; e++)
			{
				fprintf(ad, "%u\n", g3_edges_array[e]);
				fprintf(el, "%u %u\n", v, g3_edges_array[e]);
				// assert(e < edges_count);
				// assert(g3_edges_array[e]  < vertices_count);
				sum += last_val * (g3_edges_array[e] + 1);
				last_val = g3_edges_array[e] + 1;
			}
		}

    fclose(ad);
    fclose(el);

    t0 += __get_nano_time();
	}	
	printf("\n  Writing ad and el files finished successfully in %'.2f seconds.\n", t0/1e9);
	printf("---------------------\n");
	printf("\n  Validation sum: %lu\n", sum);

	// Releasing memory
	munmap(g3_graph, g3_graph_size);
	g3_graph = NULL;
	g3_offsets_array = NULL;
	g3_edges_array = NULL;

	// Creating sym links
	{
		char src[PATH_MAX], dest[PATH_MAX];
		realpath(g3_file, src);
		sprintf(dest, "%s", src);
		assert(strlen(dest) < PATH_MAX - 10);
		sprintf(dest+strlen(dest)-7,".sg");
		printf("  Linking %s to %s\n", src, dest);
		printf("---------------------\n");

		unlink(dest);
		ret = symlink(src, dest);
		assert(ret == 0);
	}
	
	return 0;
}
