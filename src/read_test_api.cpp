// Lustre read test, single node, multiple threads
// unbuffered (file descriptor), buffered (FILE), memory mapped (mmap)
// Author: Ugo Varetto
// Required patched version of lustreapi includes given non-C++ compliant
// C declarations.
// llapi is only used to retrieve layout in order to print information and 
// initialize thread count.

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
#include <vector>

using namespace std;

enum class ReadMode { Buffered, Unbuffered, MemoryMapped };

//------------------------------------------------------------------------------
size_t ReadPartFd(int fd, char* dest, size_t size, size_t offset) {
    // problems when size > 2 GB
    const size_t maxChunkSize = 1 << 30;  // read in chunks of 1GB max
    const size_t chunks = size / maxChunkSize;
    const size_t remainder = size % maxChunkSize;
    size_t bytesRead = 0;
    size_t off = 0;
    for (int i = 0; i < chunks; ++i) {
        off = maxChunkSize * i;
        bytesRead += pread(fd, dest + off, maxChunkSize, offset + off);
    }
    if (remainder) {
        bytesRead += pread(fd, dest + off, remainder, offset + off);
    }
    return bytesRead;
}

size_t ReadPartMem(const char* src, char* dest, size_t size, size_t offset) {
    copy(src + offset, src + offset + size, dest);
    return size;
}

size_t ReadPartFile(FILE* f, char* dest, size_t size, size_t offset) {
    if (fseek(f, offset, SEEK_SET)) return 0;
    if (fread(dest, size, 1, f) != 1) return 0;
    return size;
}

//------------------------------------------------------------------------------
size_t FileSize(const char* fname) {
    struct stat st;
    if (stat(fname, &st)) {
        cerr << "Error retrieveing file size: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    return st.st_size;
}

struct Config {
    int numThreads = 1;
    ReadMode readMode = ReadMode::Unbuffered;
};

void printHelp(const char* name) {
    std::cerr << "Usage: " << name
              << " <file name> [-t num threads, defaul = stripe count]"
              << " [-m read mode: buffered | unbuffered | mmap, default "
                 "unbuffered]"
              << std::endl;
};

Config ParseCommandLine(int argc, char** argv) {
    int c{0};
    opterr = 0;  // extern int (from getopt)
    Config config;

    while ((c = getopt(argc, argv, "t:m:h")) != -1) 
        switch (c) {
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
void UnbfufferedRead(const char* fname, size_t fileSize, int nthreads) {
    const int flags = O_RDONLY | O_LARGEFILE;
    const int mode = S_IRUSR;  // | S_IWUSR | S_IRGRP | S_IROTH;
    const int fd = open(fname, flags, mode);
    if (fd < 0) {
        cerr << "file creation has failed, error: " << strerror(errno);
        exit(EXIT_FAILURE);
    }
    // NOTE: the following should return an error when opening a pre-existing
    // striped file.
    // const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
    //                               stripeOffset, stripeCount, stripePattern);
    vector<char> buffer(fileSize);
    const size_t partSize = fileSize / nthreads;
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads;
    vector<future<size_t>> readers(nthreads);

    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const size_t sz = t != nthreads - 1 ? partSize : lastPartSize;
        readers[t] = async(launch::async, ReadPartFd, fd,
                           buffer.data() + offset, sz, offset);
    }
    size_t totalBytesRead = 0;
    for (auto& r : readers) {
        totalBytesRead += r.get();
    }
    if (close(fd)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (totalBytesRead != fileSize) {
        cerr << "Error reading file. File size: " << fileSize
             << ", bytes read: " << totalBytesRead << endl;
        exit(EXIT_FAILURE);
    }
}

void BufferedRead(const char* fname, size_t fileSize, int nthreads) {
    const int flags = O_RDONLY | O_LARGEFILE;
    const int mode = S_IRUSR;  // | S_IWUSR | S_IRGRP | S_IROTH;
    FILE* f = fopen(fname, "rb");
    if (!f) {
        cerr << "file creation has failed, error: " << strerror(errno);
        exit(EXIT_FAILURE);
    }
    // NOTE: the following should return an error when opening a pre-existing
    // striped file.
    // const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
    //                               stripeOffset, stripeCount, stripePattern);
    vector<char> buffer(fileSize);
    const size_t partSize = fileSize / nthreads;
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads;
    vector<future<size_t>> readers(nthreads);
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const size_t sz = t != nthreads - 1 ? partSize : lastPartSize;
        readers[t] = async(launch::async, ReadPartFile, f,
                           buffer.data() + offset, sz, offset);
    }
    size_t totalBytesRead = 0;
    for (auto& r : readers) {
        totalBytesRead += r.get();
    }
    if (fclose(f)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (totalBytesRead != fileSize) {
        cerr << "Error reading file" << endl;
        exit(EXIT_FAILURE);
    }
}

void MMapRead(const char* fname, size_t fileSize, int nthreads) {
    int fin = open(fname, O_RDONLY | O_LARGEFILE);
    if (fin < 0) {
        cerr << "Error cannot open input file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    const char* src =
        (char*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fin, 0);
    if (src == MAP_FAILED) {
        cerr << "Error mmap: " << strerror(errno) << endl;
    }
    const size_t partSize = fileSize / nthreads;
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads;
    vector<char> buffer(fileSize);
    vector<future<size_t>> readers(nthreads);
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const size_t sz = t != nthreads - 1 ? partSize : lastPartSize;
        readers[t] = async(launch::async, ReadPartMem, src,
                           buffer.data() + offset, sz, offset);
    }
    size_t totalBytesRead = 0;
    for (auto& r : readers) {
        totalBytesRead += r.get();
    }
    if (close(fin)) {
        cerr << "Error closing file: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    if (totalBytesRead != fileSize) {
        cerr << "Error reading file" << endl;
        exit(EXIT_FAILURE);
    }
}

//-----------------------------------------------------------------------------
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
    // To be used later to allow for multi-node processing:
    // size_t offset; //starting point
    // float percentage; //part of file to read, 1 all, 0.1 10%
    llapi_layout* layout = llapi_layout_get_by_path(fileName, 0);

    if (!layout) {
        cerr << "Error retrieving layout information: " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }
    // get layout attributes
    uint64_t size = 0;
    if (llapi_layout_stripe_size_get(layout, &size)) {
        cerr << "Error retrieving stripe size information: " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }

    uint64_t count = 0;
    if (llapi_layout_stripe_count_get(layout, &count)) {
        cerr << "Error retrieving stripe count information: " << strerror(errno)
             << endl;
        exit(EXIT_FAILURE);
    }
    llapi_layout_free(layout);
    const int nthreads = config.numThreads ? config.numThreads : count;
    const size_t fileSize = FileSize(fileName);
    cout << "File:         " << fileName << endl;
    cout << "File size:    " << fileSize << endl;
    cout << "Stripe count: " << count << endl;
    cout << "Stripe size:  " << size << endl;
    cout << "# threads:    " << nthreads << endl;
    const unsigned long long stripeSize = size;
    const unsigned long long stripeCount = count;
    const unsigned long long stripeOffset = 0;
    const unsigned long long stripePattern = 0;

    const auto start = chrono::high_resolution_clock::now();
    switch (readMode) {
        case ReadMode::Buffered:
            BufferedRead(fileName, fileSize, nthreads);
            break;
        case ReadMode::Unbuffered:
            UnbfufferedRead(fileName, fileSize, nthreads);
            break;
        case ReadMode::MemoryMapped:
            MMapRead(fileName, fileSize, nthreads);
            break;
        default:
            break;
    }
    const auto end = chrono::high_resolution_clock::now();
    const float elapsed =
        chrono::duration_cast<chrono::milliseconds>(end - start).count() /
        1000.f;
    if (elapsed == 0) {
        cout << "Elapsed time < 1ms " << endl;
        return 0;
    }
    const uint32_t GiB = 1073741824;
    const float bw = (float(fileSize) / elapsed) / GiB;  // Gi bytes/s
    cout << "Bandwidth: " << bw << " GiB/s" << endl;
    return 0;
}