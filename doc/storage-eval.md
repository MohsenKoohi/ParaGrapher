
# An Evaluation of Bandwidth of Different Storage Types (HDD vs. SSD vs. LustreFS) for Different Block Sizes and Different Parallel Read Methods (mmap vs pread vs read)

We evaluate read bandwidth of three storage types:

*   **HDD**: A 6TB Hitachi HUS726060AL 7200RPM SATA v3.1
*   **SSD**: A 4TB Samsung MZQL23T8HCLS-00A07 PCIe4 NVMe v1.4
*   **LustreFS**: A parallel file system with total 2PB with a SSD pool

and for three **parallel** read methods:

*   **mmap**: [https://man7.org/linux/man-pages/man2/mmap.2.html](https://man7.org/linux/man-pages/man2/mmap.2.html)
*   **pread**: [https://man7.org/linux/man-pages/man2/pread.2.html](https://man7.org/linux/man-pages/man2/pread.2.html)
*   **read**: [https://man7.org/linux/man-pages/man2/read.2.html](https://man7.org/linux/man-pages/man2/read.2.html)

and for two block sizes:

*   **4 KB** blocks
*   **4 MB** blocks

The **source code** is available on ParaGrapher repository:

*   [https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/read\_bandwidth.c](https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/read_bandwidth.c)
*   [https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/read\_bandwidth\_mam.c](https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/read_bandwidth_mam.c) : this file is similar to previous one, but repeats each evaluation for a user-defined number of rounds and identifies Min, Average, and Max. values.
*   [https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/ReadBandwidth.java](https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/ReadBandwidth.java) : this is a Java-based evaluation of read bandwidth and the script ([https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/java-read-bandwidth.sh](https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/java-read-bandwidth.sh)) can be used to create changes in evaluation parameters.

The OS cache of storage contents have been dropped after each evaluation  
(`sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches'`).  
The flushcache.c file ([https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/flushcache.c](https://github.com/MohsenKoohi/ParaGrapher/blob/main/test/flushcache.c)) can be used with the same functionality for users without sudo access, however, it usually takes more time to be finished.

For LustreFS, we have repeated the evaluation of _read_ and _pread_ using O\_DIRECT flag as this flag prevents client-side caching.

For HDD and SSD experiments, we have used a machine with Intel W-2295 3.00GHz CPU, 18 cores, 36 hyper-threads, 24MB L3 cache, 256 GB DDR4 2933Mhz memory, running Debian 12 Linux 6.1. For LustreFS, we have used a machine with 2TB 3.2GHz DDR4 memory, 2 AMD 7702 CPUs, in total, 128 cores, 256 threads.

The results of the evaluation using **read\_bandwidth.c** are in the following table. The values are **Bandwidth in MB/s**. Also, 1-2 digits close to each number with a white background are are percentage of load imbalance between parallel threads.

[![](images/hdd-ssd-lustre.png)](../raw/main/doc/images/hdd-ssd-lustre.png)

