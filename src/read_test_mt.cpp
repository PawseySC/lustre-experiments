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

// simple multithreaed read test: each thread reads from a different location
//                               in the input file
// compilation:
//     g++ -pthread read_test_mt.cpp -O3 -o read_test_mt \
//          [-D PAGE_ALIGNED] [-D BUFFERED]
// options:
//   page aligned memory buffer: -D PAGE_ALIGNED
//   buffered: -D BUFFERED
//   memory mapped: -D MMAP automatically enables PAGE_ALIGNED
// execution:
// ./read_test_mt <input file name> <num threads> <transfer size>
//
// memory mapped option does not support transfer size
//
// WARNING: when enabling memory mapped I/O each chunk in the memory
//          buffer must be aligned to a page boundary; the chunk size
//          and number of threads is changed to address this requirement
//          resulting in the number of threads spawned different from the 
//          one specified on the command line in some cases
//
// <transfer size> is the number of bytes read at each fread/pread call,
// set to -1 to perform one single read operation per thread with 
// buffer size = (file size) / (number of threads)

// To compile statically:
// g++ -pthread ../read_test_mt.cpp -o write_test_buffered -O3 \
// -static -static-libstdc++ -static-libgcc -lrt  -Wl,--whole-archive \
// -lpthread -Wl,--no-whole-archive

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

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

// The following functions write a single file part, starting at a specific
// offset. Both buffered and unbuffered versions are implemented.
#ifdef BUFFERED
//------------------------------------------------------------------------------
void ReadPart(const char* fname, char* dest, size_t size, size_t offset,
              int64_t partSize = -1) {
    FILE* f = fopen(fname, "rb");
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
        if (fread(dest + off, 1, sz, f) != sz) {
            cerr << "Error reading from file: " << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }
    }

    if (fclose(f)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}
#elif MMAP
#ifndef PAGE_ALIGNED
#define PAGE_ALIGNED
#endif //  PAGE_ALIGNED 
// read file part from #define PAGE_ALIGNEDmemory mapped file
void ReadPart(const char* fname, char* dest, size_t size,
              size_t offset, int64_t partSize = -1) {
    int fd = open(fname, O_RDONLY | O_LARGEFILE); // O_DIRECT
    if (fd < 0) {
        cerr << "Error cannot open input file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    const size_t sz = max(size, size_t(4096));
    char* src = (char*) mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, offset);
    if (src == MAP_FAILED) {  // mmap returns (void *) -1 == MAP_FAILED
        cerr << "Error mmap: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    auto start = chrono::high_resolution_clock::now();
    copy(src, src + size,
         dest);  // note: it invokes __mempcy_avx_unaligned!
    auto end = chrono::high_resolution_clock::now();
    if (munmap(src, sz)) {
        cerr << "Error unmapping memory: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (close(fd)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
}
#else
// ubuffered
void ReadPart(const char* fname, char* dest, size_t size, size_t offset,
              int64_t partSize = -1) {
    const int flags = O_RDONLY | O_LARGEFILE;  // if supported add O_DIRECT
    const mode_t mode = 0444;                  // user, goup, all: read
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
        if (pread(fd, dest + off, size, offset + off) < 0) {
            cerr << "Failed to read from file. Error: " << strerror(errno)
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
size_t FileSize(const char* fname) {
    struct stat st;
    if (stat(fname, &st)) {
        cerr << "Error retrieving file size: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    return st.st_size;
}

//------------------------------------------------------------------------------
// Read file starting a specified global offset.
// Global offset = process id X file size / # processes
double Read(const char* fname, size_t size, int nthreads,
            int64_t transferSize = -1) {
#ifdef PAGE_ALIGNED
    char* buffer = static_cast<char*>(aligned_alloc(getpagesize(), size));
#else
    char* buffer = static_cast<char*>(malloc(size));
#endif
    if (!buffer) {
        cerr << "Failed to allocate memory. Error: " << strerror(errno) << endl;
    }
    size_t partSize = size / nthreads;
#ifdef MMAP // each part must be aligned to a page boundary
    if(partSize % getpagesize() != 0) {
        const size_t Q = partSize / getpagesize();
        partSize = getpagesize() * (Q + 1);
    }
    while(partSize * nthreads > size) {
        --nthreads;
    }
#endif
    const size_t lastPartSize = size - partSize * (nthreads - 1);
    future<void> readers[nthreads];
    using Clock = chrono::high_resolution_clock;
    auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const bool isLast = t == nthreads - 1;
        const size_t sz = isLast ? lastPartSize : partSize;
        readers[t] = async(launch::async, ReadPart, fname, buffer + offset, sz,
                           offset, transferSize);
    }
    for (auto& r : readers) r.wait();
    const auto end = Clock::now();
    free(buffer);
    return double(chrono::duration_cast<chrono::nanoseconds>(end - start)
                      .count()) /
           1E9;
}

// Compilation options
#ifdef PAGE_ALIGNED
static const char* page_aligned = "Page aligned: yes";
#else
static const char* page_aligned = "Page aligned: no";
#endif
#ifdef BUFFERED
static const char* buffered = "Buffered: yes";
#else
static const char* buffered = "Buffered: no";
#endif
#ifdef MMAP
static const char* mmapped = "Memory mapped: yes";
#else
static const char* mmapped = "Memory mapped: no";
#endif


//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Usage: " << argv[0]
             << " <file name> <number of threads per process> <transfer size>"
             << endl
             << " set transfer size to -1 to use default per thread buffer size"
             << endl;
        cerr << "Compilation options:" << endl
             << "  " << buffered << endl
             << "  " << page_aligned << endl;    
        exit(EXIT_FAILURE);
    }
    const char* fileName = argv[1];
    const size_t fileSize = FileSize(fileName);
    const int nthreads = strtoul(argv[2], NULL, 10);
    if (!nthreads) {
        cerr << "Error, invalid number of threads" << endl;
        exit(EXIT_FAILURE);
    }
    const int64_t transferSize = strtoll(argv[3], NULL, 10);
    if (transferSize == 0) {
        cerr << "Error, wrong transfer buffer size" << endl;
        exit(EXIT_FAILURE);
    }
    const double elapsed =
        Read(fileName, fileSize, nthreads, transferSize);
    const double GiB = 1 << 30;
    const double GiBs = (fileSize / GiB) / elapsed;
    cout << GiBs << " GB/s" << endl;
    return 0;
}