// Author: Ugo Varetto
// simple parallel write test: each thread writes to a different location
//                            in the output file, in case the executable is
//                            invoked within slurm it will distribute the
//                            computation across all processes automatically,
//                            with each process writing a different sub-region
//                            of the file
// compilation: g++ -pthread simple_write_test.cpp -o simple_write_test
//
// Lustre:
//
// retrieve stripe count and size: lfs getstripe <file name>
//
// create 0 byte 32-stripe, 10G/32 bytes per stripe:
// lfs setstripe file -c 32  -S $((10*2**30/32)
//
// fill file with data: dd if=/dev/zero of=infile bs=1G count=10
// note: when data validation is required /dev/urandom should be used
// instead


#include <sys/stat.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <numeric>
#include <vector>

using namespace std;

#if __cplusplus < 201103L
#error "C++11 or newer required"
#endif

//------------------------------------------------------------------------------
void WritePart(const char* fname, char* src, size_t size, size_t offset) {
    // unbuffered open/pread/close preferred
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
double BufferedWrite(const char* fname, size_t size, int nthreads,
                     size_t globalOffset) {
    vector<char> buffer(size);
    const size_t partSize = size / nthreads;
    const size_t lastPartSize =
        size % nthreads == 0 ? partSize : size % nthreads + partSize;
    vector<future<void>> writers(nthreads);
    using Clock = chrono::high_resolution_clock;
    auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const bool isLast = t == nthreads - 1;
        const size_t sz = isLast ? lastPartSize : partSize;
        writers[t] = async(launch::async, WritePart, fname,
                           buffer.data() + offset, sz, offset + globalOffset);
    }
    for (auto& w : writers) w.wait();
    const auto end = Clock::now();
    return chrono::duration_cast<chrono::nanoseconds>(end - start).count() /
           1E9;
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Usage: " << argv[0]
             << " <file name> <number of threads per process> <file size>" << endl
             << " in case the executable is invoked within slurm it will "
                "distribute the computation across all processes automatically"
             << endl;
        exit(EXIT_FAILURE);
    }
    const char* fileName = argv[1];
    const size_t fileSize = strtoull(argv[3], NULL, 10);
    if(fileSize == 0) {
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
    const double elapsed =
        BufferedWrite(fileName, partSize, nthreads, globalOffset);
    const double GiB = 0x40000000;
    const double GiBs = (partSize / GiB) / elapsed;
    if(slurmNodeId) cout << "Node ID: " << slurmNodeId << endl;
    cout << "\tProcess: " << processIndex 
         << endl
         << "\tBandwidth: " << GiBs << " GiB/s"
         << endl
         << "\tElapsed time: " << elapsed
         << " seconds" << endl << endl;
    return 0;
}