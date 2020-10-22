//simple parallel read test: each threads reads from a different location
//                           in the input file (C++11)
//compilation: g++ -pthread simple_read_test.cpp -o simple_read_test
//Lustre:
// retrieve stripe count and size: lfs getstripe <file name>
// create 0 byte striped file: lfs 

#include <sys/stat.h>
#include <cerrno>
#include <chrono>
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
float BufferedRead(const char* fname, size_t fileSize, int nthreads) {
    vector<char> buffer(fileSize);
    const size_t partSize = fileSize / nthreads;
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads + partSize;
    vector<future<void>> readers(nthreads);
    using Clock = chrono::high_resolution_clock;
    auto start = Clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const bool isLast = t == nthreads - 1;
        const size_t sz = isLast ? lastPartSize : partSize;
        readers[t] = async(launch::async, ReadPart, fname,
                           buffer.data() + offset, sz, offset);
    }
    for (auto& r : readers) r.wait();
    const auto end = Clock::now();
    return chrono::duration_cast<chrono::microseconds>(end - start).count() /
           1000000;
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <file name> <number of threads>"
             << endl;
        exit(EXIT_FAILURE);
    }
    const char* fileName = argv[1];
    const size_t fileSize = FileSize(fileName);
    const int nthreads = strtoul(argv[2], NULL, 10);
    if (!nthreads) {
        cerr << "Error, invalid number of threads" << endl;
    }
    const float elapsed = BufferedRead(fileName, fileSize, nthreads);
    const float GiB = 0x40000000;
    const float GiBs = (fileSize / GiB) / elapsed;
    cout << "Bandwidth: " << GiBs << " GiB/s" << endl;
    return 0;
}