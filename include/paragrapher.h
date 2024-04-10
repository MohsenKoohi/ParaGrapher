#ifndef __PARAGRAPHER_H
#define __PARAGRAPHER_H

#ifdef __cplusplus
	extern "C"
	{
#endif

// Graph types
	/*
		PARAGRAPHER_(format)_(name)_(VID size)(VW size)(EW size)_(A/S)(P/S)
			format: CSX, COO
			name: WG (WebGraph), MM (MatrixMarket)
			VID: vertex ID in Bytes
			VW: vertex weight in Bytes
			EW: edge weight in Bytes
			A/S: Asynchronous/Synchronous
			P/S: Parallel/Serial
	*/

	typedef struct paragrapher_graph paragrapher_graph;
	typedef enum
	{
		PARAGRAPHER_CSX_WG_400_AP = 1,	// WebGraph with 4 Bytes ID per vertex, reading asynchronously (non-blocking) and in parallel
		PARAGRAPHER_CSX_WG_800_AP, // big WebGraph with 8 Bytes ID per vertex, reading asynchronously (non-blocking) and in parallel
		PARAGRAPHER_CSX_WG_404_AP, // edge-weighted WebGraph with 4 Bytes ID per vertex and 4 Bytes uint weight per edge, reading asynchronously (non-blocking) and in parallel

		// PARAGRAPHER_COO_MM_400_SS, // MatrixMarket with 4 Bytes ID per vertex, reading synchronously (blocking) and sequentially
		// PARAGRAPHER_COO_MM_404_SS, // MatrixMarket with 4 Bytes ID per vertex and 4 Bytes uint weight per edge, reading synchronously (blocking) and sequentially

		PARAGRAPHER_GRAPH_TYPES_COUNT 
	} paragrapher_graph_type;

	typedef struct paragrapher_read_request paragrapher_read_request;

// General API for opening, releasing all types of graphs (CSX, COO, ...) in synchronous or asynchronous mode
// And for communicating their library implementations
// The `args` arguments (void** args, int argc) are used for reader-specific arguments
	
	// Initializing the library
	int paragrapher_init();

	// Opening a graph
	paragrapher_graph* paragrapher_open_graph(char* name, paragrapher_graph_type type, void** args, int argc);

	// Releasing a graph
	int paragrapher_release_graph(paragrapher_graph* graph, void** args, int argc);

	// Getting or setting an option
	// Request types for communication between the library and user
	// Each line is followed by the size of required memory in the comment
	typedef enum
	{
		// Graph properties
		PARAGRAPHER_REQUEST_GET_GRAPH_PATH = 1,       // char[PATH_MAX] for args[0]
		PARAGRAPHER_REQUEST_GET_VERTICES_COUNT,       // unsigned long for args[0] 
		PARAGRAPHER_REQUEST_GET_EDGES_COUNT,          // unsigned long for args[0] 

		// Reading Options
		PARAGRAPHER_REQUEST_LIB_USES_OWN_BUFFERS,     // unsigned long for args[0]
		PARAGRAPHER_REQUEST_LIB_USES_USER_ARRAYS,     // unsigned long for args[0]
		PARAGRAPHER_REQUEST_SET_BUFFER_SIZE,          // unsigned long for args[0]
		PARAGRAPHER_REQUEST_GET_BUFFER_SIZE,          // unsigned long for args[0]	
		PARAGRAPHER_REQUEST_SET_MAX_BUFFERS_COUNT,    // unsigned long for args[0]
		PARAGRAPHER_REQUEST_GET_MAX_BUFFERS_COUNT,    // unsigned long for args[0]	

		// Reading Status
		PARAGRAPHER_REQUEST_READ_STATUS,           // paragrapher_read_request* for args[0], and unsigned long for args[1]
		PARAGRAPHER_REQUEST_READ_TOTAL_CALLBACKS,  // paragrapher_read_request* for args[0], and unsigned long for args[1]
		PARAGRAPHER_REQUEST_READ_EDGES,            // paragrapher_read_request* for args[0], and unsigned long for args[1]

		// Number of constants
		PARAGRAPHER_REQUESTS_SIZE
	} paragrapher_request_type;

	// The user should allocate the required memory for args values
	// Return values: 0 (done), < 0 (error)
	int paragrapher_get_set_options(paragrapher_graph* graph, paragrapher_request_type request_type, void** args, int argc);
	
// API for reading CSX graphs asynchronously

	// A block of continuous edges from [start_vertex.start_edge, end_vertex.end_edge)
	// -1UL: all ones 
	typedef struct
	{
		unsigned long start_vertex;
		unsigned long start_edge;
		unsigned long end_vertex;
		unsigned long end_edge;
	} paragrapher_edge_block;

	// The callback function that is called by the library to pass a block of edges to the reader
	typedef void (*paragrapher_csx_callback)(paragrapher_read_request* request, paragrapher_edge_block* eb, void* offsets, void* edges, void* buffer_id, void* args);

	// Getting the offsets of vertices from [start_vertex, end_vertex]
	void* paragrapher_csx_get_offsets(paragrapher_graph* graph, void* offsets, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);

	// Getting the weights of vertices from [start_vertex, end_vertex]
	void* paragrapher_csx_get_vertex_weights(paragrapher_graph* graph, void* weights, unsigned long start_vertex, unsigned long end_vertex, void** args, int argc);

	// Releasing the arrays received from paragrapher_csx_get_offsets() or paragrapher_csx_get_vertex_weights()
	void paragrapher_csx_release_offsets_weights_arrays(paragrapher_graph* graph, void* array);
	
	/* Reading an edge block of a CSX graph
		The user may pass additional arrays to the library (using args) to specify the location of writing offsets and edges
		The library has two options: 
			(1) To use its own buffers for reading the graph (Library can be queried by PARAGRAPHER_REQUEST_LIB_USES_OWN_BUFFERS), or
			(2) To fill the edges array specified by the user (Library can be queried by PARAGRAPHER_REQUEST_LIB_USES_USER_ARRAYS)
		The library may call the callback multiple times but for non-overlapping blocks of edges.
	*/
	paragrapher_read_request* paragrapher_csx_get_subgraph(paragrapher_graph* graph, paragrapher_edge_block* eb, void* offsets, void* edges, paragrapher_csx_callback callback, void* callback_args, void** args, int argc);

	// In case the library has used its own buffers, the user should inform the library when copying the data is completed
	void paragrapher_csx_release_read_buffers(paragrapher_read_request* request, paragrapher_edge_block* eb, void* buffer_id);

	void paragrapher_csx_release_read_request(paragrapher_read_request* request);

// API for reading COO graphs asynchronously

	typedef void (*paragrapher_coo_callback)(paragrapher_read_request* request, unsigned long start_row, unsigned long end_row, void* edges, void* buffer_id);

	/* Reading an edge block of a COO graph: [start_row, end_row)
		Using -1UL for end_row: all the remaining edges. 
		
		The user may pass additional arrays to the library (using args) to specify the location of writing edges
		The library has two options: 
			(1) To use its own buffers for reading the graph (Library can be queried by PARAGRAPHER_REQUEST_LIB_USES_OWN_BUFFERS), or
			(2) To fill the edges array specified by the user (Library can be queried by PARAGRAPHER_REQUEST_LIB_USES_USER_ARRAYS)
		The library may call the callback multiple times but for non-overlapping blocks of edges.
	*/
	paragrapher_read_request paragrapher_coo_get_edges(paragrapher_graph* graph, unsigned long start_row, unsigned long end_row, void* edges, paragrapher_coo_callback callback, void* callback_args, void** args, int argc);

#ifdef __cplusplus
	}
#endif 

#endif
