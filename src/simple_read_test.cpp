// Author: Ugo Varetto
// simple parallel read test: each thread reads from a different location
//                            in the input file, in case the executable is
//                            invoked within slurm it will distribute the
//                            computation across all processes automatically,
//                            with each process reading a different sub-region
//                            of the file
// compilation: g++ -pthread simple_read_test.cpp -o simple_read_test
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

// uvaretto@zeus-1:~/projects/lustre-scratch/tmp> time srun -n 4 -N 4 --cpus-per-task 16 --mem 32000 -p copyq ./rt data/striped_10G_over_64/10Ghpc3 16
// srun: job 4866510 queued and waiting for resources
// srun: job 4866510 has been allocated resources
// Process: 2 Bandwidth: 5.00696 GiB/s
// Elapsed time: 1.99722 seconds
//
// Process: 1 Bandwidth: 4.84904 GiB/s
// Elapsed time: 2.06227 seconds
//
// Process: 3 Bandwidth: 4.61036 GiB/s
// Elapsed time: 2.16903 seconds
//
// Process: 0 Bandwidth: 4.82129 GiB/s
// Elapsed time: 2.07414 seconds
//
// real    0m3.469s
// user    0m0.013s
// sys     0m0.008s

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
void ReadPart(const char* fname, char* dest, size_t size, size_t offset) {
    // unbuffered open/pread/close preferred
    FILE* f = fopen(fname, "rb");
    if (!f) {
        cerr << "Error opening file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (fseek(f, offset, SEEK_SET)) {
        cerr << "Error moving file pointer (fseek): " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }
    if (fread(dest, 1, size, f) != size) {
        cerr << "Error reading from file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (fclose(f)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------
size_t FileSize(const char* fname) {
    struct stat st;
    if (stat(fname, &st)) {
        cerr << "Error retrieving file size: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    return st.st_size;
}

//------------------------------------------------------------------------------
double BufferedRead(const char* fname, size_t size, int nthreads,
                    size_t globalOffset) {
    char* buffer = new char[size];
    if(!buffer) {
        cerr << "Error, cannot allocate memory" << endl;
        exit(EXIT_FAILURE);
    }
    const size_t partSize = size / nthreads;
    const size_t lastPartSize =
        size % nthreads == 0 ? partSize : size % nthreads + partSize;
    future<void> readers[nthreads];
    using Clock = chrono::high_resolution_clock;
    auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const bool isLast = t == nthreads - 1;
        const size_t sz = isLast ? lastPartSize : partSize;
        readers[t] = async(launch::async, ReadPart, fname,
                           buffer + offset, sz, offset + globalOffset);
    }
    for (auto& r : readers) r.wait();
    const auto end = Clock::now();
    delete [] buffer;
    return chrono::duration_cast<chrono::nanoseconds>(end - start).count() /
           1E9;
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0]
             << " <file name> <number of threads per process>" << endl
             << " in case the executable is invoked within slurm it will "
                "distribute the computation across all processes automatically"
             << endl;
        exit(EXIT_FAILURE);
    }
    const char* fileName = argv[1];
    const size_t fileSize = FileSize(fileName);
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
        BufferedRead(fileName, partSize, nthreads, globalOffset);
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