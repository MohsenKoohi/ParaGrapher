#ifndef __POPLAR_H
#define __POPLAR_H

#ifdef __cplusplus
	extern "C"
	{
#endif

// Graph types
	/*
		POPLAR_(format)_(name)_(VID size)(VW size)(EW size)_(A/S)(P/S)
			format: CSX, COO
			name: WG (WebGraph), MM (MatrixMarket)
			VID: vertex ID in Bytes
			VW: vertex weight in Bytes
			EW: edge weight in Bytes
			A/S: Asynchronous/Synchronous
			P/S: Parallel/Serial
	*/

	typedef struct poplar_graph poplar_graph;
	typedef enum
	{
		POPLAR_CSX_WG_400_AP = 1,	// WebGraph with 4 Bytes ID per vertex, reading asynchronously (non-blocking) and in parallel
		POPLAR_CSX_WG_800_AP, // big WebGraph with 8 Bytes ID per vertex, reading asynchronously (non-blocking) and in parallel
		POPLAR_CSX_WG_404_AP, // edge-weighted WebGraph with 4 Bytes ID per vertex and 4 Bytes uint weight per edge, reading asynchronously (non-blocking) and in parallel

		// POPLAR_COO_MM_400_SS, // MatrixMarket with 4 Bytes ID per vertex, reading synchronously (blocking) and sequentially
		// POPLAR_COO_MM_404_SS, // MatrixMarket with 4 Bytes ID per vertex and 4 Bytes uint weight per edge, reading synchronously (blocking) and sequentially

		POPLAR_GRAPH_TYPES_COUNT 
	} poplar_graph_type;

	typedef struct poplar_read_request poplar_read_request;

// General API for opening, releasing all types of graphs (CSX, COO, ...) in synchronous or asynchronous mode
// And for communicating their library implementations
// The `args` arguments (void** args, int argc) are used for reader-specific arguments
	
	// Initializing the library
	int poplar_init();

	// Opening a graph
	poplar_graph* poplar_open_graph(char* name, poplar_graph_type type, void** args, int argc);

	// Releasing a graph
	int poplar_release_graph(poplar_graph* graph, void** args, int argc);

	// Getting or setting an option
	// Request types for communication between the library and user
	// Each line is followed by the size of required memory in the comment
	typedef enum
	{
		// Graph properties
		POPLAR_REQUEST_GET_GRAPH_PATH = 1,       // char[1024] for args[0]
		POPLAR_REQUEST_GET_VERTICES_COUNT,       // unsigned long for args[0] 
		POPLAR_REQUEST_GET_EDGES_COUNT,          // unsigned long for args[0] 

		// Reading Options
		POPLAR_REQUEST_LIB_USES_OWN_BUFFERS,     // unsigned long for args[0]
		POPLAR_REQUEST_LIB_USES_USER_ARRAYS,     // unsigned long for args[0]
		POPLAR_REQUEST_SET_BUFFER_SIZE,          // unsigned long for args[0]
		POPLAR_REQUEST_GET_BUFFER_SIZE,          // unsigned long for args[0]	
		POPLAR_REQUEST_SET_MAX_BUFFERS_COUNT,    // unsigned long for args[0]
		POPLAR_REQUEST_GET_MAX_BUFFERS_COUNT,    // unsigned long for args[0]	

		// Reading Status
		POPLAR_REQUEST_READ_STATUS,           // poplar_read_request* for args[0], and unsigned long for args[1]
		POPLAR_REQUEST_READ_TOTAL_CALLBACKS,  // poplar_read_request* for args[0], and unsigned long for args[1]
		POPLAR_REQUEST_READ_EDGES, 				 		// poplar_read_request* for args[0], and unsigned long for args[1]

		// Number of constants
		POPLAR_REQUESTS_SIZE
	} poplar_request_type;

	// The user should allocate the required memory for args values
	// Return values: 0 (done), < 0 (error)
	int poplar_get_set_options(poplar_graph* graph, poplar_request_type request_type, void** args, int argc);
	
// API for reading CSX graphs asynchronously

	// A block of continuous edges from [start_vertex.start_edge, end_vertex.end_edge)
	// -1UL: all ones 
	typedef struct
	{
		unsigned long start_vertex;
		unsigned long start_edge;
		unsigned long end_vertex;
		unsigned long end_edge;
	} poplar_edge_block;

	// The callback function that is called by the library to pass a block of edges to the reader
	typedef void (*poplar_csx_callback)(poplar_read_request* request, poplar_edge_block* eb, void* offsets, void* edges, void* buffer_id);

	// Getting the offsets of vertices from [start_vertex, end_vertex]
	void* poplar_csx_get_offsets(poplar_graph* graph, void* offsets, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);

	// Getting the weights of vertices from [start_vertex, end_vertex]
	void* poplar_csx_get_vertex_weights(poplar_graph* graph, void* weights, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);

	// Releasing the arrays received from poplar_csx_get_offsets() or poplar_csx_get_vertex_weights()
	void poplar_csx_release_offsets_weights_arrays(poplar_graph* graph, void* array);
	
	/* Reading an edge block of a CSX graph
		The user may pass additional arrays to the library (using args) to specify the location of writing offsets and edges
		The library has two options: 
			(1) To use its own buffers for reading the graph (Library can be queried by POPLAR_REQUEST_LIB_USES_OWN_BUFFERS), or
			(2) To fill the edges array specified by the user (Library can be queried by POPLAR_REQUEST_LIB_USES_USER_ARRAYS)
		The library may call the callback multiple times but for non-overlapping blocks of edges.
	*/
	poplar_read_request* poplar_csx_get_subgraph(poplar_graph* graph, poplar_edge_block* eb, void* offsets, void* edges, poplar_csx_callback callback, void** args, int argc);

	// In case the library has used its own buffers, the user should inform the library when copying the data is completed
	void poplar_csx_release_read_buffers(poplar_read_request* request, poplar_edge_block* eb, void* buffer_id);

	void poplar_csx_release_read_request(poplar_read_request* request);

// API for reading COO graphs asynchronously

	typedef void (*poplar_coo_callback)(poplar_read_request* request, unsigned long start_row, unsigned long end_row, void* edges, void* buffer_id);

	/* Reading an edge block of a COO graph: [start_row, end_row)
		Using -1UL for end_row: all the remaining edges. 
		
		The user may pass additional arrays to the library (using args) to specify the location of writing edges
		The library has two options: 
			(1) To use its own buffers for reading the graph (Library can be queried by POPLAR_REQUEST_LIB_USES_OWN_BUFFERS), or
			(2) To fill the edges array specified by the user (Library can be queried by POPLAR_REQUEST_LIB_USES_USER_ARRAYS)
		The library may call the callback multiple times but for non-overlapping blocks of edges.
	*/
	poplar_read_request poplar_coo_get_edges(poplar_graph* graph, unsigned long start_row, unsigned long end_row, void* edges, poplar_coo_callback callback, void** args, int argc);

#ifdef __cplusplus
	}
#endif 

#endif