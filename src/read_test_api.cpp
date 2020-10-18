#include <fcntl.h>
#include <lustre/lustreapi.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <vector>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 5) {
        cout << "Usage: " << argv[0]
             << " <file name> <stripe size> <stripe offset> <stripe count>"
             << endl;
    }
    const unsigned long long stripeSize = atoll(argv[2]);
    const unsigned long long stripeOffset = atoll(argv[3]);
    const unsigned long long stripeCount = atoll(argv[4]);
    const unsigned long long stripePattern = 0;
    const int flags = O_RDONLY | O_LARGEFILE;
    const int mode = S_IRUSR;  // | S_IWUSR | S_IRGRP | S_IROTH;
    const int fd = llapi_file_open(argv[1], flags, mode, stripeSize,
                                   stripeOffset, stripeCount, stripePattern);
    vector<char> buffer(stripeSize);
    const int64_t rbytes = read(fd, buffer.data(), buffer.size());
    if (rbytes > 0) {
        cout << "read " << rbytes << endl;
        size_t* ptr = (size_t*)&*(buffer.end() - sizeof(size_t));
        cout << *ptr << endl;
    }
    pread(fd, buffer.data(), stripeSize, stripeSize);
    size_t* ptr = (size_t*)&*(buffer.end() - sizeof(size_t));
    cout << *ptr << endl;

    return 0;
}