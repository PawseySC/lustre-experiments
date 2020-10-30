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

// Lustre read test
// Required: patched version of lustreapi #includes, given non-C++ compliant
// C declarations and enums used in bitwise operations!
// llapi is only used to retrieve layout in order to print information and
// initialise thread count.
// Run without arguments to read help text.

#include <fcntl.h>
#include <lustre/lustreapi.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <lyra/lyra.hpp>
#include <map>
#include <numeric>
#include <vector>

using namespace std;

// constants
const uint32_t GiB = 1073741824;
constexpr float us = 1E6;

// read mode: Buffered     --> fopen/fread/fclose
//            Unbuffered   --> open/pread/close
//            MemoryMapped --> mmap / munmap
enum class ReadMode { Buffered, Unbuffered, MemoryMapped };

// read performance
struct ReadInfo {
    size_t readBytes = 0;
    float bandwidth = 0.f;
};

// Compute elapsed time
// C++20: use consteval
constexpr float Elapsed(const chrono::duration<float>& d) {
    return chrono::duration_cast<chrono::microseconds>(d).count() / us;
}

// Compute bandwidth
// C++20: use consteval
constexpr float GiBs(float seconds, size_t numBytes) {
    return seconds > 0 ? (numBytes / seconds) / GiB : 0;
}

// Configuration information read from command line
struct Config {
    int numThreads = 0;
    ReadMode readMode = ReadMode::Unbuffered;
    size_t partFraction = 1;  // read 1/stripeFraction bytes from each stripe
    bool bwOnly = false;      // if true only print raw bandwidth number
    bool perOSTBw = false;
};

// default clock
using Clock = chrono::high_resolution_clock;

// standard deviation
template <typename SeqT>
typename SeqT::value_type StandardDeviation(const SeqT& seq) {
    using Value = typename SeqT::value_type;
    const typename SeqT::size_type N = seq.size();
    const Value sum =
        std::accumulate(std::cbegin(seq), std::cend(seq), Value(0));
    const Value avg = sum / N;
    SeqT s;
    std::transform(std::cbegin(seq), std::cend(seq), std::back_inserter(s),
                   [avg, N](Value v) { return (v - avg) * (v - avg) / N; });
    const Value variance =
        std::accumulate(std::cbegin(s), std::cend(s), Value(0));
    return std::sqrt(variance);
}

// median
template <typename SeqT>
typename SeqT::value_type Median(SeqT seq) {
    nth_element(begin(seq), begin(seq) + seq.size() / 2, end(seq));
    return seq[seq.size() / 2];
}

//------------------------------------------------------------------------------
// read file part, using file descriptor (unbuffered read)
ReadInfo ReadPartFd(const char* fname, char* dest, size_t size, size_t offset) {
    const int flags = O_RDONLY | O_LARGEFILE;
    const int mode = S_IRUSR;  // | S_IWUSR | S_IRGRP | S_IROTH;
    const int fd = open(fname, flags, mode);
    if (fd < 0) {
        cerr << "File creation has failed, error: " << strerror(errno);
        exit(EXIT_FAILURE);
    }
    // problems when size > 2 GB
    const size_t maxChunkSize = 1 << 30;  // read in chunks of 1GB max
    const size_t chunks = size / maxChunkSize;
    const size_t remainder = size % maxChunkSize;
    size_t bytesRead = 0;
    size_t off = 0;
    auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < chunks; ++i) {
        off = maxChunkSize * i;
        const ssize_t rb = pread(fd, dest + off, maxChunkSize, offset + off);
        if (rb == -1) {
            cerr << "Error reading file (pread): " << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }
        bytesRead += rb;
    }
    if (remainder) {
        const ssize_t rb = pread(fd, dest + off, remainder, offset + off);
        if (rb == -1) {
            cerr << "Error reading file (pread): " << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }
        bytesRead += rb;
    }
    auto end = chrono::high_resolution_clock::now();
    if (close(fd)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    return {bytesRead, GiBs(Elapsed(end - start), size)};
}

// read file part from memory mapped file
ReadInfo ReadPartMem(const char* fname, char* dest, size_t size,
                     size_t offset) {
    int fd = open(fname, O_RDONLY | O_LARGEFILE);
    if (fd < 0) {
        cerr << "Error cannot open input file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    const size_t sz = max(size, size_t(4096));
    char* src = (char*)mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, offset);
    if (src == MAP_FAILED) {  // mmap returns (void *) -1 == MAP_FAILED
        cerr << "Error mmap: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    auto start = chrono::high_resolution_clock::now();
    copy(src, src + size,
         dest);  // note: it will invoke __mempcy_avx_unaligned!
    auto end = chrono::high_resolution_clock::now();
    if (munmap(src, sz)) {
        cerr << "Error unmapping memory: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (close(fd)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    return {size, GiBs(Elapsed(end - start), size)};
}

// read file part using standard buffered operations
ReadInfo ReadPartFile(const char* fname, char* dest, size_t size,
                      size_t offset) {
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
    auto start = chrono::high_resolution_clock::now();
    if (fread(dest, 1, size, f) != size) {
        cerr << "Error reading from file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    auto end = chrono::high_resolution_clock::now();
    if (fclose(f)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    return {size, GiBs(Elapsed(end - start), size)};
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
Config ParseCommandLine(int argc, char** argv) {
    static const char* HELP_TEXT = R"(
        Compute read bandwidth, if num threads = stripe count per 
        OST bandwidth is reported. Using -p and -N parameters 
        it is possible to have a process read only a subregion of 
        the file. E.g.
        >read_test -p $SLURM_PROCID -N $SLURM_NUMTASKS
    )";

    Config cfg;
    bool showHelp = false;
    string readMode = "buffered";
    auto cli =
        lyra::help(showHelp).description(HELP_TEXT) |
        lyra::opt(cfg.numThreads, "num threads")["-t"]["--threads"](
            "Number of concurrent threads")
            .optional() |
        lyra::opt(readMode, "read mode")["-m"]["--read-mode"](
            "Read mode: buffered (fread), unbuffered (pread), mmap")
            .choices("buffered", "unbuffered", "mmap")
            .optional() |
        lyra::opt(cfg.partFraction,
                  "fractional part")["-f"]["--fractional-part"](
            "per-thread fraction: e.g. 4, read 1/4 of allocated file region")
            .optional() |
        lyra::opt(cfg.bwOnly, "bandwidth only")["-b"]["--bandwidth-only"](
            "only print the raw overall bandwidth information number")
            .optional() |
        lyra::opt(cfg.perOSTBw,
                  "per OST bw")["-o"]["--per-ost-bw"]("print per-OST bandwidth")
            .optional();

    // Parse the program arguments:
    auto result = cli.parse({argc, argv});
    if (!result) {
        cerr << result.errorMessage() << endl;
        cerr << cli << endl;
        exit(EXIT_FAILURE);
    }
    if (showHelp) {
        cout << cli;
        exit(EXIT_FAILURE);
    }

    if (readMode == "unbuffered")
        cfg.readMode = ReadMode::Unbuffered;
    else if (readMode == "buffered")
        cfg.readMode = ReadMode::Buffered;
    else if (readMode == "mmap")
        cfg.readMode = ReadMode::MemoryMapped;
    else {
        cerr << "Invalid read mode: " << readMode << endl;
        cout << cli;
        exit(EXIT_FAILURE);
    }

    return cfg;
}

//------------------------------------------------------------------------------
// read data from file using unbuffered operations: open/pread/close
// filePartSize is == file size in the case of single process,
// file size / num processes (+ file size % num processes) otherwise
float UnbfufferedRead(const char* fname, size_t filePartSize, int nthreads,
                      size_t globalOffset, vector<float>& threadBandwidth,
                      size_t partFraction) {
    // NOTE: the following should return an error when opening a pre-existing
    // striped file.
    // const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
    //                               stripeOffset, stripeCount, stripePattern);
    // never ever use std::vector<> for uninitialised buffers: it will try to
    // default initialise every single POD element!
    char* buffer = new char[filePartSize];
    const size_t partSize = filePartSize / nthreads;
    if (partFraction > partSize || partFraction == 0) {
        cerr << "Invalid part fraction" << endl;
        exit(EXIT_FAILURE);
    }
    const size_t lastPartSize = filePartSize % nthreads == 0
                                    ? partSize
                                    : filePartSize % nthreads + partSize;
    vector<future<ReadInfo>> readers(nthreads);
    const auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const size_t sz = t != nthreads - 1 ? partSize : lastPartSize;
        readers[t] = async(launch::async, ReadPartFd, fname, buffer + offset,
                           sz / partFraction, offset + globalOffset);
    }
    for (auto& r : readers) r.wait();
    const auto end = Clock::now();
    size_t totalBytesRead = 0;
    for (int r = 0; r != readers.size(); ++r) {
        const ReadInfo ri = readers[r].get();
        totalBytesRead += ri.readBytes;  // not used
        threadBandwidth[r] = ri.bandwidth;
    }
    delete[] buffer;
    return GiBs(Elapsed(end - start), filePartSize / partFraction);
}

// read data from file using buffered operations: fopen, fread, fclose
// filePartSize is == file size in the case of single process,
// file size / num processes (+ file size % num processes) otherwise
float BufferedRead(const char* fname, size_t filePartSize, int nthreads,
                   size_t globalOffset, vector<float>& threadBandwidth,
                   size_t partFraction) {
    // NOTE: the following should return an error when opening a pre-existing
    // striped file.
    // const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
    //                               stripeOffset, stripeCount, stripePattern);
    // never ever use std::vector<> for uninitialised buffers: it will try to
    // default initialise every single POD element!
    char* buffer = new char[filePartSize];
    const size_t partSize = filePartSize / nthreads;
    if (partFraction > partSize || partFraction == 0) {
        cerr << "Invalid part fraction" << endl;
        exit(EXIT_FAILURE);
    }
    const size_t lastPartSize = filePartSize % nthreads == 0
                                    ? partSize
                                    : filePartSize % nthreads + partSize;
    vector<future<ReadInfo>> readers(nthreads);
    auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const bool isLast = t == nthreads - 1;
        const size_t sz = isLast ? lastPartSize : partSize;
        readers[t] = async(launch::async, ReadPartFile, fname, buffer + offset,
                           sz / partFraction, offset + globalOffset);
    }
    for (auto& r : readers) r.wait();
    const auto end = Clock::now();
    size_t totalBytesRead = 0;
    for (int r = 0; r != readers.size(); ++r) {
        const ReadInfo ri = readers[r].get();
        totalBytesRead += ri.readBytes;  // not used
        threadBandwidth[r] = ri.bandwidth;
    }
    delete[] buffer;
    return GiBs(Elapsed(end - start), filePartSize / partFraction);
}

// read data from file using memory-mapped operations
// filePartSize is == file size in the case of single process,
// file size / num processes (+ file size % num processes) otherwise
float MMapRead(const char* fname, size_t filePartSize, int nthreads,
               size_t globalOffset, vector<float>& threadBandwidth,
               size_t partFraction) {
    const size_t partSize = filePartSize / nthreads;
    if (partFraction > partSize || partFraction == 0) {
        cerr << "Invalid part fraction" << endl;
        exit(EXIT_FAILURE);
    }
    const size_t lastPartSize = filePartSize % nthreads == 0
                                    ? partSize
                                    : filePartSize % nthreads + partSize;
    // never ever use std::vector<> for uninitialised buffers: it will try to
    // default initialise every single POD element!
    char* buffer = new char[filePartSize];
    vector<future<ReadInfo>> readers(nthreads);
    if (mlockall(MCL_CURRENT)) {  // normally a bad idea: locks *all* process
                                  // memory at once
        cerr << "Error locking memory (mlockall): " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    const auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const size_t sz = t != nthreads - 1 ? partSize : lastPartSize;
        readers[t] =
            async(launch::async, ReadPartMem, fname, buffer + offset,
                  sz / partFraction, offset + globalOffset);
    }
    for (auto& r : readers) r.wait();
    const auto end = Clock::now();
    size_t totalBytesRead = 0;
    for (int r = 0; r != readers.size(); ++r) {
        const ReadInfo ri = readers[r].get();
        totalBytesRead += ri.readBytes;  // not used
        threadBandwidth[r] = ri.bandwidth;
    }
    if (munlockall()) {
        cerr << "Error unlocking memory (mlunlockall): " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }
    return GiBs(Elapsed(end - start), filePartSize / partFraction);
}

//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    Config config = ParseCommandLine(argc, argv);

    const char* slurmProcId = getenv("SLURM_PROCID");
    const char* slurmNumTasks = getenv("SLURM_NTASKS");
    const char* slurmNodeId = getenv("SLURM_NODEID");
    const int processIndex = slurmProcId ? strtoull(slurmProcId, NULL, 10) : 0;
    const int numProcesses =
        slurmNumTasks ? strtoull(slurmNumTasks, NULL, 10) : 1;
    // getopt will change the pointers, save data first
    // const char* programName = argv[0];
    const char* fileName = argv[1];

    const ReadMode readMode = config.readMode;
    const size_t partNum = processIndex;
    const size_t numParts = numProcesses;

    llapi_layout* layout = llapi_layout_get_by_path(fileName, 0);

    if (!layout) {
        cerr << "Error retrieving layout information: " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }
    // get layout attributes
    uint64_t stripeSize = 0;
    if (llapi_layout_stripe_size_get(layout, &stripeSize)) {
        cerr << "Error retrieving stripe size information: " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }

    uint64_t stripeCount = 0;
    if (llapi_layout_stripe_count_get(layout, &stripeCount)) {
        cerr << "Error retrieving stripe count information: " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }
    vector<uint64_t> osts(stripeCount);
    for (int i = 0; i != stripeCount; ++i) {
        uint64_t ostIndex;
        if (llapi_layout_ost_index_get(layout, i, &ostIndex)) {
            cerr << "Error retrieving OST index: " << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }
        osts[i] = ostIndex;
    }
    llapi_layout_free(layout);

    const size_t fileSize = FileSize(fileName);
    const size_t globalOffset = partNum * fileSize / numParts;
    const size_t partSize = partNum != numParts - 1
                                ? fileSize / numParts
                                : fileSize / numParts + fileSize % numParts;
    // size_t partSize = stripeCount;
    // if process read only a subregion of the files we need to conpute
    // how many stripes are included in the region
    if (numParts > 1) {
        const size_t numStripes =
            partSize / stripeSize + (partSize % stripeSize ? 1 : 0);
        stripeCount = numStripes;
    }

    const int nthreads = config.numThreads ? config.numThreads : stripeCount;

    if (numParts == 1 && !config.bwOnly) {
        cout << "File:         " << fileName << endl;
        cout << "File size:    " << fileSize << endl;
        cout << "Stripe count: " << stripeCount << endl;
        cout << "Stripe size:  " << stripeSize << endl;
        cout << "# threads:    " << nthreads << endl;
        cout << "Read factor:  "
             << "1/" << config.partFraction << " ~"
             << (fileSize / config.partFraction) << " bytes "
             << (fileSize / config.partFraction) / nthreads
             << " bytes per thread" << endl;
    }

    vector<float> threadBandwidth(nthreads);
    float bw = 0;
    switch (readMode) {
        case ReadMode::Buffered:
            cout << "Read mode: buffered" << endl;
            bw = BufferedRead(fileName, partSize, nthreads, globalOffset,
                              threadBandwidth, config.partFraction);
            break;
        case ReadMode::Unbuffered:
            cout << "Read mode: unbuffered" << endl;
            bw = UnbfufferedRead(fileName, partSize, nthreads, globalOffset,
                                 threadBandwidth, config.partFraction);
            break;
        case ReadMode::MemoryMapped:
            cout << "Read mode: memory mapped" << endl;
            bw = MMapRead(fileName, partSize, nthreads, globalOffset,
                          threadBandwidth, config.partFraction);
            break;
        default:
            break;
    }
    if (bw == 0) {
        cout << "Elapsed time < 1ms " << endl;
        return 0;
    }
    if (!config.bwOnly)
        cout << "Bandwidth: " << bw << " GiB/s" << endl << endl;
    else
        cout << bw << endl;  // when multiple process are invoked only print
                             // the bandwidth number to make it easy to parse
                             // output
    if (config.perOSTBw) {
        map<int, float> ost2bw;
        map<float, int> bw2ost;
        for (int i = 0; i != nthreads; ++i) {
            ost2bw.insert({osts[i], threadBandwidth[i]});
            bw2ost.insert({threadBandwidth[i], osts[i]});
        }
        for (const auto& kv : bw2ost) {
            cout << "OST " << kv.second << ": " << kv.first << " GiB/s" << endl;
        }
        if (nthreads > 1) {
            const float M = *max_element(std::begin(threadBandwidth),
                                         std::end(threadBandwidth));
            const float m = *min_element(std::begin(threadBandwidth),
                                         std::end(threadBandwidth));
            const float avg = accumulate(std::begin(threadBandwidth),
                                         std::end(threadBandwidth), 0.f) /
                              threadBandwidth.size();
            const float stdev = StandardDeviation(threadBandwidth);
            const float median = Median(threadBandwidth);
            // note: case of multiple OSTs with same bandwidth not handled,
            // would require storing values in map as vectors instead of plain
            // floats, if printed number of OST < total OST number it means
            // the some OSTs have the exact same bandwidth up to the last
            // decimal digit
            cout << "min:     " << m << " GiB/s"
                 << " - OST " << bw2ost[m] << endl;
            cout << "Max:     " << M << " GiB/s"
                 << " - OST " << bw2ost[M] << endl;
            cout << "Max/min: " << M / m << endl;
            cout << "Average: " << avg << " GiB/s" << endl;
            cout << "Median:  " << median << " - OST " << bw2ost[median]
                 << endl;
            cout << "Standard deviation: " << stdev << " GiB/s" << endl;
            cout << "Standard deviation / average: " << (100 * stdev / avg)
                 << " %" << endl;
        }
    }
    return 0;
}