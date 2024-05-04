![ParaGrapher](https://blogs.qub.ac.uk/dipsa/wp-content/uploads/sites/319/2024/02/poplar.jpg)

# [ParaGrapher: Graph Loading API and Library](https://blogs.qub.ac.uk/DIPSA/ParaGrapher/)

**https://blogs.qub.ac.uk/DIPSA/ParaGrapher**

This repository contains the source code of ParaGrapher, an API and library for loading graphs.
For futher information about the library please refer to https://blogs.qub.ac.uk/DIPSA/ParaGrapher/ and publications.

### Supperted Graph Types
1. **PARAGRAPHER_CSX_WG_400_AP** : WebGraphs with 4 Bytes ID per vertex without weights on edges or vertices
2. **PARAGRAPHER_CSX_WG_800_AP** : Big WebGraphs with 8 Bytes ID per vertex without weights on edges or vertices
3. **PARAGRAPHER_CSX_WG_404_AP** : WebGraphs with 4 Bytes ID per vertex and 4 Bytes integer weight per edge and without weights on vertices

### Requirements

1. `gcc` with a version greater than 9
2. `JDK` with a version greater than 15
3. `bc`, `wget`, and `unzip`

### API Documentation
Please visit the [Wiki](../../wiki/API-Documentation) or download the PDF file using [this link](../../raw/main/doc/api.pdf).


### Download Sample Datasets
- Run `make download_WG400`, `make download_WG404`, or `make download_WG800`  
to download and store sample datasets into `test/datasets` folder.

### Compiling and Executing Code
- If `gcc` is not in `PATH`, please set path to `gcc` compiler folder in Line 9 of the `Makefile` and `test/Makefile`. 

- By commenting `-DNDEBUG` in Line 25 of the `Makefile`, ParaGrapher will output its logs.

- With `make all` the C and Java source codes are compiled and the required WebGraph libraries are downloaded.
  
- All compiled and downloaded files are stored in the `lib64` folder and future calls to the library requires setting
the `PARAGRAPHER_LIB_FOLDER` environemnt variable to the `lib64` folder.

- The `test` folder contains sample codes for different types of graphs. Use `make test` for running the test. 
You may pass argument `dataset` to specify the location of the test, e.g., `make test dataset=path/to/dataset`.

- In the first access to the graphs in WebGraph format a delay may be experienced for creating two files by the library:
  1. A WebGraph `.offset` file is required which is created through a call to the WebGraph framework.
  2. An `_offsets.bin` file is created that contains the offsets array of the CSX format but in binary and 
littel-endian format with 8-Bytes values for each of |V|+1 elements.
In case of [MS-BioGraphs](https://blogs.qub.ac.uk/DIPSA/MS-BioGraphs/), the file with name `MS??_offsets.bin` can
be downloaded and renamed as `MS??-underlying_offsets.bin` to prevent creating.

- ParaGrapher creates some temporary files in `/dev/shm` with names starting by `paragrapher_`. The files are deleted at the end of a 
successful exuection. Otherwise, they should be deleted by the user.

- After calling ParaGrapher, OS cache of storage contents should be dropped using 
[`echo 3 > /proc/sys/vm/drop_caches`](https://www.kernel.org/doc/Documentation/sysctl/vm.txt)
or by calling the [`flushcache`](test/flushcache.c) program that 
has the same functionality but with a longer execution time.

### Bandwidht Measurement

- The file [test/read_bandwidth.c](test/read_bandwidth.c) contains a 
benchmark implemented in C to measure the read bandwidth of storage for (i) different thread numbers, (ii) different block sizes, and
(iii) different read methods (read(), pread(), mmap()).

- The file [test/ReadBandwidth.java](test/ReadBandwidth.java) contains a 
benchmark implemented in Java to measure the read bandwidth of storage for (i) different thread numbers, (ii) different block sizes, and
(iii) different read methods (read(), mmap()). The script [test/java-read-bandwidth.sh](test/java-read-bandwidth.sh) 
may  be used for changing parameters.

### Remained Works
0. Binary format
1. MatrixMarket format
2. Textual CSX
3. Sleep time of communicating threads in webgraph.c and WebGraphRRServer.java, using semapher?
4. A permanently-in-background (daemon) Java program may be required to answer all queries, but its memory usage and GC impacts should be considered

### License
Licensed under the GNU v3 General Public License, as published by the Free Software Foundation. 
You must not use this Software except in compliance with the terms of the License. Unless required by applicable 
law or agreed upon in writing, this Software is distributed on an "as is" basis, without any warranty; without even 
the implied warranty of merchantability or fitness for a particular purpose, neither express nor implied. 
For details see terms of the License (see attached file: LICENSE). 

#### Copyright 2024 The Queen's University of Belfast, Northern Ireland, UK
