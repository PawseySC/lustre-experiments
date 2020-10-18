#include <lustre/lustreapi.h>

#include <cerrno>
#include <iostream>

using namespace std;
//important: make sure the file does not exist already
int main(int argc, char *argv[]) {
    if (argc != 4) {
        cerr << "usage: " << argv[0]
             << " <filename> <stripe size> <number of OSTs>" << endl;
        exit(EXIT_FAILURE);
    }
    const int rc = llapi_file_create(argv[1], atoll(argv[2]), 0, atoll(argv[3]),
                                     LOV_PATTERN_RAID0);     
    if (rc < 0) {
        cerr << "file creation has failed, error: " << strerror(-rc);
        exit(EXIT_FAILURE);
    }
    cout << argv[1] << " with stripe size " << atoll(argv[2])
         << " striped across " << atoll(argv[3]) << " OSTs, has been created!"
         << endl;
    return 0;
}