# ParaGrapher Docs: General Info

Several graph frameworks exist and each one optimizes particular graph algorithms for different optimization metrics 
and is implemented in a different programming language using different parallelization libraries/techniques. 
As each framework has implemented its own format of graphs, it becomes time-consuming, inaccurate, and sometimes impossible 
to compare the execution of graph algorithms across different frameworks for a wide range of graph datasets. Moreover, designing a 
new graph framework requires investing time for implementing solutions for loading the graphs (as input datasets to the algorithms), 
while it is the execution of graph algorithms that is the main optimization target of the graph frameworks and not loading the graphs.

It may be better to separate the process of loading graphs from designing high-performance graph algorithms. 
We present ParaGrapher, an API and a library for loading graphs. ParaGrapher supports 
- synchronous (blocking) and asynchronous (non-blocking) loading of
- simple, vertex- and edge-weighted graphs
- in different formats including WebGraph. 

In this way, ParaGrapher helps progressing High-Performance 
Graph Processing by simplifying the evaluation and refinement of new (and previous) 
falsifiable contributions on a wider range of datasets.

## ParaGrapher in a Few Bullet Points

ParaGrapher
  - provides an API for reading different graph formats from storage and
  - implements a library for this API for accessing graphs.

This allows new graph processing frameworks
 + to have immediate access for reading graphs and
 + to evaluate the new/previous contributions on a wide range of graph datasets.

Asynchronous reading of a graph in ParaGrapher:
- the user asks reading a range of vertices (and their edges),
- ParaGrapher calls the callback function defined by the user upon completing each block of requested vertices, and
- the user (in callback function) informs ParaGrapher when no further access to a block (i.e., its buffer) is required.

ParaGrapher library
+ is not responsible for allocating, releasing, or managing memory and is just responsible for loading block(s) of edges,
+ returns the read data as read-only buffers to the user or by populating memory allocated by the user, and
+ parallelizes reading and passes blocks of read data to a user-defined callback function on a new thread.

For now, ParaGrapher supports
- WebGraph format by implementing a C-consumser Java-producer algorithm using the shared-memory interface (/dev/shm). 
- Other formats: on its way.
