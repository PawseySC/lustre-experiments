// Lustre read test, single node, multiple threads
// unbuffered (file descriptor), buffered (FILE), memory mapped (mmap)
// Author: Ugo Varetto
// Required!: patched version of lustreapi includes, given non-C++ compliant
// C declarations.
// llapi is only used to retrieve layout in order to print information and
// initialise thread count.

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
#include <map>
#include <numeric>
#include <vector>

using namespace std;

const uint32_t GiB = 1073741824;
constexpr float us = 1E6;

enum class ReadMode { Buffered, Unbuffered, MemoryMapped };

struct ReadInfo {
    size_t readBytes = 0;
    float bandwidth = 0.f;
};

// C++20: use consteval
constexpr float Elapsed(const chrono::duration<float>& d) {
    return chrono::duration_cast<chrono::microseconds>(d).count() / us;
}

// C++20: use consteval
constexpr float GiBs(float seconds, size_t numBytes) {
    return seconds > 0 ? (numBytes / seconds) / GiB : 0;
}

struct Config {
    int numThreads = 0;
    ReadMode readMode = ReadMode::Unbuffered;
    size_t partNum = 0;
    size_t numParts = 1;
    size_t partFraction = 1;  // read 1/stripeFraction bytes from each stripe
};

using Clock = chrono::high_resolution_clock;

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

template <typename SeqT>
typename SeqT::value_type Median(SeqT seq) {
    nth_element(begin(seq), begin(seq) + seq.size() / 2, end(seq));
    return seq[seq.size() / 2];
}

//------------------------------------------------------------------------------
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

void printHelp(const char* name) {
    std::cerr
        << "Compute read bandwidth, if num threads = stripe counts per "
           "OST bandwidth is reported. Using -p and -N parameters "
           "it is possible to have a process read only a subregion of "
           "the file. E.g. "
        << endl
        << name << " -p $SLURM_PROCID -N $SLURM_NUMTASKS" << endl
        << "Usage: " << name
        << " <file name> [-t num threads, defaul = stripe count] "
           " [-p part number] [-N number of parts]"
        << " [-m read mode: buffered | unbuffered | mmap, default "
           "unbuffered]"
        << " [-f per-thread fraction to read: 1 / fraction X part size\n"
        << "e.g. -f 1/3 --> read one third of data assigned to each thread"
        << std::endl;
};

Config ParseCommandLine(int argc, char** argv) {
    int c{0};
    opterr = 0;  // extern int (from getopt)
    Config config;

    while ((c = getopt(argc, argv, "f:t:m:pNh")) != -1) switch (c) {
            case 't':
                config.numThreads = int(strtoul(optarg, nullptr, 10));
                if (config.numThreads == 0) {
                    cerr << "Invalid number of threads" << endl;
                    exit(EXIT_FAILURE);
                }
                break;
            case 'm':
                if (string(optarg) == "buffered")
                    config.readMode = ReadMode::Buffered;
                else if (string(optarg) == "unbuffered")
                    config.readMode = ReadMode::Unbuffered;
                else if (string(optarg) == "mmap")
                    config.readMode = ReadMode::MemoryMapped;
                else {
                    cerr
                        << "Error parsing command line: unrecognised read mode";
                    printHelp(argv[0]);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                config.partNum = int(strtoul(optarg, nullptr, 10));
                if (errno == ERANGE) {
                    cerr << "Invalid part number" << endl;
                    exit(EXIT_FAILURE);
                }
                break;
            case 'N':
                config.numParts = int(strtoul(optarg, nullptr, 10));
                if (errno == ERANGE) {
                    cerr << "Invalid number of parts" << endl;
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                config.partFraction = int(strtoul(optarg, nullptr, 10));
                if (errno == ERANGE) {
                    cerr << "Invalid stripe fraction" << endl;
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
                printHelp(argv[0]);
                exit(EXIT_SUCCESS);
                break;
            case '?':
                cerr << "Error parsing command line" << endl;
                exit(EXIT_FAILURE);
                break;
            default:
                cerr << "Error parsing command line" << endl;
                printHelp(argv[0]);
                exit(EXIT_FAILURE);
        }
    return config;
}

//------------------------------------------------------------------------------
float UnbfufferedRead(const char* fname, size_t fileSize, int nthreads,
                      size_t globalOffset, vector<float>& threadBandwidth,
                      size_t partFraction) {
    // NOTE: the following should return an error when opening a pre-existing
    // striped file.
    // const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
    //                               stripeOffset, stripeCount, stripePattern);
    vector<char> buffer(fileSize);
    const size_t partSize = fileSize / nthreads;
    if (partFraction > partSize || partFraction == 0) {
        cerr << "Invalid part fraction" << endl;
        exit(EXIT_FAILURE);
    }
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads + partSize;
    vector<future<ReadInfo>> readers(nthreads);
    const auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const size_t sz = t != nthreads - 1 ? partSize : lastPartSize;
        readers[t] =
            async(launch::async, ReadPartFd, fname, buffer.data() + offset,
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
    return GiBs(Elapsed(end - start), fileSize / partFraction);
}

float BufferedRead(const char* fname, size_t fileSize, int nthreads,
                   size_t globalOffset, vector<float>& threadBandwidth,
                   size_t partFraction) {
    // NOTE: the following should return an error when opening a pre-existing
    // striped file.
    // const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
    //                               stripeOffset, stripeCount, stripePattern);
    vector<char> buffer(fileSize);
    const size_t partSize = fileSize / nthreads;
    if (partFraction > partSize || partFraction == 0) {
        cerr << "Invalid part fraction" << endl;
        exit(EXIT_FAILURE);
    }
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads + partSize;
    vector<future<ReadInfo>> readers(nthreads);
    auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const bool isLast = t == nthreads - 1;
        const size_t sz = isLast ? lastPartSize : partSize;
        readers[t] =
            async(launch::async, ReadPartFile, fname, buffer.data() + offset,
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
    return GiBs(Elapsed(end - start), fileSize / partFraction);
}

float MMapRead(const char* fname, size_t fileSize, int nthreads,
               size_t globalOffset, vector<float>& threadBandwidth,
               size_t partFraction) {
    const size_t partSize = fileSize / nthreads;
    if (partFraction > partSize || partFraction == 0) {
        cerr << "Invalid part fraction" << endl;
        exit(EXIT_FAILURE);
    }
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads + partSize;
    vector<char> buffer(fileSize);
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
            async(launch::async, ReadPartMem, fname, buffer.data() + offset,
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
    return GiBs(Elapsed(end - start), fileSize / partFraction);
}

//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp(argv[0]);
        exit(EXIT_FAILURE);
    }
    // getopt will change the pointers, save data first
    // const char* programName = argv[0];
    const char* fileName = argv[1];
    Config config = ParseCommandLine(argc, argv);
    const ReadMode readMode = config.readMode;
    const size_t partNum = config.partNum;
    const size_t numParts = config.numParts;

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

    if (numParts == 1) {
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
    if (numParts == 1)
        cout << "Bandwidth: " << bw << " GiB/s" << endl << endl;
    else
        cout << bw << endl;  // when multiple process are invoked only print
                             // the bandwidth number to make it easy to parse
                             // output
    if (nthreads == stripeCount && stripeCount > 1 && numParts == 1) {
        map<int, float> ost2bw;
        map<float, int> bw2ost;
        for (int i = 0; i != nthreads; ++i) {
            ost2bw.insert({osts[i], threadBandwidth[i]});
            bw2ost.insert({threadBandwidth[i], osts[i]});
        }
        for (const auto& kv : bw2ost) {
            cout << "OST " << kv.second << ": " << kv.first << " GiB/s" << endl;
        }
        const float M = *max_element(std::begin(threadBandwidth),
                                     std::end(threadBandwidth));
        const float m = *min_element(std::begin(threadBandwidth),
                                     std::end(threadBandwidth));
        const float avg = accumulate(std::begin(threadBandwidth),
                                     std::end(threadBandwidth), 0.f) /
                          threadBandwidth.size();
        const float stdev = StandardDeviation(threadBandwidth);
        const float median = Median(threadBandwidth);
        // note: case of multiple OSTs with same bandwidth not handled, would
        // require storing values in map as vectors instead of plain floats,
        // but not really needed since OSTs are printed from slower to faster
        cout << "min:     " << m << " GiB/s"
             << " - OST " << bw2ost[m] << endl;
        cout << "Max:     " << M << " GiB/s"
             << " - OST " << bw2ost[M] << endl;
        cout << "Max/min: " << M / m << endl;
        cout << "Average: " << avg << " GiB/s" << endl;
        cout << "Median:  " << median << " - OST " << bw2ost[median] << endl;
        cout << "Standard deviation: " << stdev << " GiB/s" << endl;
        cout << "Standard deviation / average: " << (100 * stdev / avg) << " %"
             << endl;
    }
    return 0;
}