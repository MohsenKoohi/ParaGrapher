# PG-FUSE: ParaGrapher FUSE File System 

The [`webgraph`](../src/webgraph.c) module, which loads graph in WebGraph format, uses the Java implementaion of
WebGraph. This may result in a large number of frequent accesses with small granularities (e.g., 128 kB per access)
to the underlying file system, degrading both the loading performance and stoarge performance.

To address this issue, ParaGrapher introduces a custom file system, [`pg_fuse`](../src/pg_fuse.c), built on top of 
the [FUSE (Filesystem in User Space)](https://github.com/libfuse/libfuse/) framework. 
By loading contents in larger granularity sizes (32 MB) 
and temporarily caching them, `pg_fuse` accelerates the loading process. 
Our measurements indicates a speedup of up to 4 times when using `pg_fuse` on top of LustreFS. 
However, for small graphs,  the impacts of `pg_fuse` may be less noticeable.

To enable `pg_fuse`, it is required to pass `USE_PG_FUSE` in the `args` parameter of
the `paragrapher_open_graph()` function.
It is required to have `libfuse3` and `libnuma` libraries for compiling and loading `pg_fuse`. 

The current implementation of `pg_fuse` only supports mounting a single file at a time. 
Hence, if multiple files are required to be mounted, multiple mounting and mount points are required.

When using `pg_fuse`, temporary folders are created in `/tmp` to serve as mount point(s).
The specific files mounted by ParaGrapher depend on the graph being loaded:
- For `PARAGRAPHER_CSX_WG_400_AP` and `PARAGRAPHER_CSX_WG_800_AP` graphs, 
the  `.graph`  and `offsets.bin` files are mounted
- For `PARAGRAPHER_CSX_WG_404_AP` graphs, the `.graph`, `offsets.bin`, and `.labels` files are mounted.
- 
Additionally, a temporary folder is created to hold soft-linked graph files.
All temporary folders/mount points are removed when the `paragrapher_release_graph()` function is called.

When a `pg_fuse` file system is mounted, the input file is divided into blocks, with a 
default block size of 32 MegaBytes (specified by the `block_size` variable). 
Upon the first access to a block, the block is loaded from the
underlying file system and stored in main memory. 
Since in loading graphs, consecutive blocks are sequentially accessed by threads, 
`pg_fuse` removes blocks that have not been accessed for a certain period of time.
The block expiration time is set by `idle_time_before_expiration` variable, defaulting to 10 seconds. 
In this way, `pg_fuse` minimizes its memory usage.

However, some blocks may be accessed by multiple threads, such as those containing partition borders. 
In these cases, the block expiration procedure can cause the block to be loaded multiple times.
To prevent this, setting `__PGF_RUB` to `0` deactivates the expiration procedure, 
but this increases memory usage of the `pg_fuse` and potentially prevent
decompressed graphs from being stored in memory, particularly for large graphs.
