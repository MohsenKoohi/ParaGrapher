# ParaGrapher FUSE File System 

The [`webgraph`](src/webgraph.c) module, which loads graph in WebGraph format, uses the Java implementaion of
WebGraph. This may result in a large number of frequent accesses with small granularities (e.g., 128 kB per access)
to the underlying file system, degrading both the loading performance and stoarge performance.

To address this issue, ParaGrapher introduces a custom file system, [`pg_fuse`](src/pg_fuse.c), built on top of 
the [FUSE (Filesystem in User Space)](https://github.com/libfuse/libfuse/) framework. 
By loading contents in larger granularity sizes (32 MB) 
and temporarily caching them, `pg_fuse` accelerates the loading process. 
Our measurements indicates a speedup of up to 8-10 times when using `pg_fuse` on top of LustreFS. 
However, for small graphs,  the impacts of `pg_fuse` may be less noticeable.

To enable `pg_fuse`, it is required to pass `USE_PG_FUSE` in `args` parameter of
the `paragrapher_open_graph()` function.

It is required to have `libfuse3` and `libnuma` for using `pg_fuse`.
