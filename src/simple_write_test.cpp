/*******************************************************************************
 * BSD 3-Clause License
 *
 * Copyright (c) 2020, Commonwealth Scientific and Industrial Research
 * Organisation (CSIRO) and The Pawsey Supercomputing Centre
 *
 * Author: Ugo Varetto
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

// Author: Ugo Varetto
// simple parallel write test: each thread writes to a different location
//                             in the output file, in case the executable is
//                             invoked within slurm it will distribute the
//                             computation across all processes automatically,
//                             with each process writing to a different
//                             sub-region in the file
// compilation:
//     g++ -pthread simple_write_test.cpp -O2 -o simple_write_test \
//          [-D PAGE_ALIGNED] [-D BUFFERED]
// options:
//   page aligned memory buffer: -D PAGE_ALIGNED
//   buffered: -D BUFFERED
// execution:
//   ./simple_write_test <output file name> <size> <num threads> <transfer size>
//
//   <transfer size> is the number of bytes written at each fwrite/pwrite call,
//   set to -1 to perform one single write operation per thread with 
//   buffer size = (file size) / ((number of processes) x (threads per process))
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
// Overstriping:
// https://wiki.lustre.org/images/b/b3/LUG2019-Lustre_Overstriping_Shared_Write_Performance-Farrell.pdf

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <numeric>

using namespace std;

#if __cplusplus < 201103L
#error "C++11 or newer required"
#endif

// The following functions write a single file part, starting at a specific
// offset, buffer transer size can be specified, otherwise the transfer buffer
// size will be equal to overall buffer size.
// Both buffered and unbuffered versions are implemented.
#ifdef BUFFERED
//------------------------------------------------------------------------------
// buffered
void WritePart(const char* fname, char* src, size_t size, size_t offset,
               int64_t partSize = -1) {
    FILE* f = fopen(fname, "wb");
    if (!f) {
        cerr << "Error opening file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    partSize = partSize < 0 ? size : partSize;
    const size_t lastPartSize = size % partSize
                                    ? size / partSize + size % partSize
                                    : partSize;
    for (size_t off = 0; off < size; off += partSize) {
        const size_t sz = off < size - partSize ? partSize : lastPartSize;
        if (fseek(f, offset + off, SEEK_SET)) {
            cerr << "Error moving file pointer (fseek): " << strerror(errno)
                 << endl;
            exit(EXIT_FAILURE);
        }
        if (fwrite(src + off, 1, sz, f) != sz) {
            cerr << "Error reading from file: " << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }
    }
    if (fclose(f)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}
#else
//------------------------------------------------------------------------------
// ubuffered
void WritePart(const char* fname, char* src, size_t size, size_t offset,
               int64_t partSize = -1) {
    const int flags = O_WRONLY | O_CREAT |
                      O_LARGEFILE;  // if supported by filesystem, add O_DIRECT
    const mode_t mode = 0644;       // user read/write, group read, all read
    int fd = open(fname, flags, mode);
    if (fd < 0) {
        cerr << "Failed to open file. Error: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    partSize = partSize < 0 ? size : partSize;
    const size_t lastPartSize = size % partSize
                                    ? size / partSize + size % partSize
                                    : partSize;
    for (size_t off = 0; off < size; off += partSize) {
        const size_t sz = off < size - partSize ? partSize : lastPartSize;
        if (pwrite(fd, src + off, sz, offset + off) < 0) {
            cerr << "Failed to write to file. Error: " << strerror(errno)
                 << endl;
            exit(EXIT_FAILURE);
        }
    }
    if (close(fd)) {
        cerr << "Error closing file " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}
#endif

//------------------------------------------------------------------------------
// Write to file in parallel starting at global offset (process id X file size /
// # processes)
double Write(const char* fname, size_t size, int nthreads, size_t globalOffset,
             int64_t transferSize = -1) {
#ifdef PAGE_ALIGNED
    char* buffer = static_cast<char*>(aligned_alloc(getpagesize(), size));
#else
    char* buffer = static_cast<char*>(malloc(size));
#endif
    if (!buffer) {
        cerr << "Failed to allocate memory. Error: " << strerror(errno) << endl;
    }
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
                           offset + globalOffset, transferSize);
    }
    for (auto& w : writers) w.wait();
    const auto end = Clock::now();
    free(buffer);
    return double(chrono::duration_cast<chrono::nanoseconds>(end - start)
                      .count()) /
           1E9;
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 5) {
        cerr << "Usage: " << argv[0]
             << " <file name> <number of threads per process> <file size> "
                "<transfer size>"
             << endl
             << " set transfer size to -1 to use default per thread buffer size"
             << endl
             << " SLURM required: it will "
                "distribute the computation across all processes automatically"
             << endl
             << " CSV output format: node id, process id, bandwidth (GiB/s), "
                "time (s)"
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
    int64_t transferSize = strtoll(argv[4], NULL, 10);
    if(transferSize == 0) {
        cerr << "Error, wrong transfer buffer size" << endl;
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
    const double elapsed = Write(fileName, partSize, nthreads, globalOffset, 
                                 transferSize);
    const double GiB = 1 << 30;
    const double GiBs = (partSize / GiB) / elapsed;
    if (slurmNodeId)
        cout << slurmNodeId << "," << processIndex << "," << GiBs << ","
             << elapsed << endl;

    return 0;
}
