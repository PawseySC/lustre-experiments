#include <fcntl.h>
#include <lustre/lustreapi.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <vector>

using namespace std;

size_t ReadPart(int fd, char* dest, size_t size) {
    const size_t bytesRead = read(fd, dest, size);
    return bytesRead;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        cout << "Usage: " << argv[0]
             << " <file name> [# threads, default = stripe count]" << endl;
    }
    // To be used later to allow for multi-node processing:
    // size_t offset; //starting point
    // float percentage; //part of file to read, 1 all, 0.1 10%
    llapi_layout* layout = llapi_layout_get_by_path(argv[1], 0);
    // get layout attributes
    uint64_t size = 0;
    llapi_layout_stripe_size_get(layout, &size);
    uint64_t count = 0;
    llapi_layout_stripe_count_get(layout, &count);
    struct stat st;
    stat(argv[0], &st);
    const size_t fileSize = st.st_size;
    size_t nthreads = argc == 3 ? atoi(argv[2]) : count;
    cout << "File:         " << argv[1] << endl;
    cout << "File size:    " << fileSize << endl;
    cout << "Stripe count: " << count << endl;
    cout << "Stripe size:  " << size << endl;
    cout << "# threads:    " << nthreads << endl;
    const unsigned long long stripeSize = size;
    const unsigned long long stripeCount = count;
    const unsigned long long stripeOffset = 0;
    const unsigned long long stripePattern = 0;
    const int flags = O_RDONLY | O_LARGEFILE;
    const int mode = S_IRUSR;  // | S_IWUSR | S_IRGRP | S_IROTH;
    const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
                                   stripeOffset, stripeCount, stripePattern);
    vector<char> buffer(fileSize);
    const size_t partSize = fileSize / nthreads;
    const size_t lastPartSize =
        fileSize % nthreads == 0 ? partSize : fileSize % nthreads;
    vector<future<size_t>> readers(nthreads);
    const auto start = chrono::high_resolution_clock::now();
    for (int t = 0; t != nthreads; ++t) {
        const size_t offset = partSize * t;
        const size_t sz = t != nthreads - 1 ? partSize : lastPartSize;
        readers[t] = async(launch::async, ReadPart, fd, buffer.data(), sz);
    }
    size_t totalBytesRead = 0;
    for (auto& r : readers) {
        totalBytesRead += r.get();
    }
    const auto end = chrono::high_resolution_clock::now();
    const auto elapsed =
        chrono::duration_cast<chrono::seconds>(end - start).count();
    const uint32_t GiB = 1073741824;
    const float bw = (fileSize / elapsed) / GiB;  // Gi bytes/s
    cout << "Bandwidth: " << bw << " GiB/s" << endl;
    close(fd);
    return 0;
}