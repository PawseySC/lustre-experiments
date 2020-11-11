# Single threaded write test

Numbers below show the write performance when writing to an 8GiB file with
1GiB stripe size and without striping, using large stripe size. 

With smaller stripe size and smaller transfer buffer size it **might** happen that data on the server is buffered, then flushed to OSTs and disks in parallel, however I could not find any conclusive evidence of that happening and it would certinly also have a dependency on the number of clients connected to any specific OST anyway.

For a single threaded writer there seems to be no benefit in using striping.

See [here](https://www.nics.tennessee.edu/computing-resources/file-systems/io-lustre-tips) and [here](https://www.citutor.org/SITES/AContent/home/course/content.php?_cid=62), performance varies non-linerarly and non-uniformly with stripe size.

## 1 `magnus.pawsey.org.au`

```term
uvaretto@nid00051:~/projects/lustre-scratch/tmp> lfs setstripe data/8G_1G_striped -c 8 -S 1G
uvaretto@nid00051:~/projects/lustre-scratch/tmp> touch data/8G_unstriped
uvaretto@nid00051:~/projects/lustre-scratch/tmp> srun ./bin/release/simple_write_test ./data/8G_unstriped 1 $((8*2**30))
Node ID: 0
        Process: 0
        Bandwidth: 0.434517 GiB/s
        Elapsed time: 18.4113 seconds
                  
uvaretto@nid00051:~/projects/lustre-scratch/tmp> srun ./bin/release/simple_write_test ./data/8G_1G_striped 1 $((8*2**30)) 
Node ID: 0
        Process: 0
        Bandwidth: 0.405646 GiB/s
        Elapsed time: 19.7216 seconds
```

## 2 `zeus.pawsey.org.au`

```term
uvaretto@z055:~/projects/lustre-scratch/tmp> srun ./bin/release/simple_write_test ./data/8G_unstriped 1 $((8*2**30))
Node ID: 0
        Process: 0
        Bandwidth: 0.437287 GiB/s
        Elapsed time: 18.2946 seconds

uvaretto@z055:~/projects/lustre-scratch/tmp> srun ./bin/release/simple_write_test ./data/8G_1G_striped 1 $((8*2**30))
Node ID: 0
        Process: 0
        Bandwidth: 0.417709 GiB/s
        Elapsed time: 19.1521 seconds
```

## Code

The following code was used.

Simple write to file through `fopen/fseek/fwrite` reading data from
uninitialsed memory.

Multiple threads per process are supported. When run from *SLURM* it will
automatically partition the file among processess and per-process threads.

```cpp
//Multi-process, multi-threaded writer
//Author: Ugo Varetto
//SEE "LICENSE" file for license/copyright information
//compile with g++ -pthread -O2 simple_write_test.cpp -o simple_write_test
#include <sys/stat.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <numeric>

using namespace std;

#if __cplusplus < 201103L
#error "C++11 or newer required"
#endif

//------------------------------------------------------------------------------
void WritePart(const char* fname, char* src, size_t size, size_t offset) {
    // unbuffered open/pwrite/close preferred
    FILE* f = fopen(fname, "wb");
    if (!f) {
        cerr << "Error opening file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (fseek(f, offset, SEEK_SET)) {
        cerr << "Error moving file pointer (fseek): " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }
    if (fwrite(src, 1, size, f) != size) {
        cerr << "Error reading from file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (fclose(f)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------
double Write(const char* fname, size_t size, int nthreads,
             size_t globalOffset) {
    char* buffer = new char[size];
    const size_t partSize = size / nthreads;
    const size_t lastPartSize =
        size % nthreads == 0 ? partSize : size % nthreads + partSize;
    future<void> writers[nthreads];
    using Clock = chrono::high_resolution_clock;
    auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const bool isLast = t == nthreads - 1;
        const size_t sz = isLast ? lastPartSize : partSize;
        writers[t] = async(launch::async, WritePart, fname, buffer + offset, sz,
                           offset + globalOffset);
    }
    for (auto& w : writers) w.wait();
    const auto end = Clock::now();
    delete[] buffer;
    return double(chrono::duration_cast<chrono::nanoseconds>(end - start)
                      .count()) /
           1E9;
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Usage: " << argv[0]
             << " <file name> <number of threads per process> <file size>"
             << endl
             << " in case the executable is invoked within slurm it will "
                "distribute the computation across all processes automatically"
             << endl;
        exit(EXIT_FAILURE);
    }
    const char* fileName = argv[1];
    const size_t fileSize = strtoull(argv[3], NULL, 10);
    if (fileSize == 0) {
        cerr << "Error, wrong file size" << endl;
        exit(EXIT_FAILURE);
    }
    const int nthreads = strtoul(argv[2], NULL, 10);
    if (!nthreads) {
        cerr << "Error, invalid number of threads" << endl;
        exit(EXIT_FAILURE);
    }
    const char* slurmProcId = getenv("SLURM_PROCID");
    const char* slurmNumTasks = getenv("SLURM_NTASKS");
    const char* slurmNodeId = getenv("SLURM_NODEID");
    const int processIndex = slurmProcId ? strtoull(slurmProcId, NULL, 10) : 0;
    const int numProcesses =
        slurmNumTasks ? strtoull(slurmNumTasks, NULL, 10) : 1;
    // processes 0 to numProcesses - 1 read the same amount of data
    // process with index == numProcesses - 1 reads the same amount of data
    // as the others + remainder of fileSize / numProcesses division
    const size_t partSize =
        processIndex != numProcesses - 1
            ? fileSize / numProcesses
            : fileSize / numProcesses + fileSize % numProcesses;
    const size_t globalOffset = processIndex * partSize;
    const double elapsed = Write(fileName, partSize, nthreads, globalOffset);
    const double GiB = 1 << 30;
    const double GiBs = (partSize / GiB) / elapsed;
    if (slurmNodeId) cout << "Node ID: " << slurmNodeId << endl;
    cout << "\tProcess: " << processIndex << endl
         << "\tBandwidth: " << GiBs << " GiB/s" << endl
         << "\tElapsed time: " << elapsed << " seconds" << endl
         << endl;
    return 0;
}
```
