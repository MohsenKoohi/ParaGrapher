![Poplar](https://blogs.qub.ac.uk/dipsa/wp-content/uploads/sites/319/2024/02/poplar.jpg)

# [Poplar: Graph Loading API and Library](https://blogs.qub.ac.uk/DIPSA/Poplar/)

**https://blogs.qub.ac.uk/DIPSA/Poplar**

This repository contains the source code of Poplar, an API and library for loading graphs.
For futher information about the library please refer to https://blogs.qub.ac.uk/DIPSA/Poplar/ and publications.

## Supperted Graph Types
1. **WG_400** : WebGraphs with 4 Bytes ID per vertex without weights on edges or vertices
2. **WG_800** : Big WebGraphs with 8 Bytes ID per vertex without weights on edges or vertices
3. **WG_404** : WebGraphs with 4 Bytes ID per vertex and 4 Bytes integer weight per edge and without weights on vertices

## Requirements

A `JDK` with version greater than 15 is required.

## Compiling and Executing Code
First, set the path to `gcc` compiler folder in Line 5 of the `Makefile` and `test/Makefile`. If you use the precompiled `gcc` of your machine, use uncomment Line 6.

With `make all` the C and Java source codes are compiled and required WebGraph libraries are downloaded. 
All compiled and downloaded are stored in `lib64` folder and future calls to the library requires setting
the `POPLAR_LIB_FOLDER` environemnt variable to be set to the `lib64` folder.

The `test` folder contains sample codes for different types of graphs. Use `make test` for running the test.
The `Makefile` inside this folder downloads the sample datasets if they do not exist in `test/dataset` folder.

## License
Licensed under the GNU v3 General Public License, as published by the Free Software Foundation. 
You must not use this Software except in compliance with the terms of the License. Unless required by applicable 
law or agreed upon in writing, this Software is distributed on an "as is" basis, without any warranty; without even 
the implied warranty of merchantability or fitness for a particular purpose, neither express nor implied. 
For details see terms of the License (see attached file: LICENSE). 

#### Copyright 2024 The Queen's University of Belfast, Northern Ireland, UK
