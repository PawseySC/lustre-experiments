#include <lustre/lustreapi.h>

#include <cerrno>
#include <iostream>
#include <vector>

using namespace std;

int main(int argc, char *argv[]) {
    const size_t VERSION_BUFFER_SIZE = 128;
    vector<char> version(VERSION_BUFFER_SIZE, '\0');
    const int rc = llapi_get_version_string(version.data(), version.size());
    if (rc < 0) {
        cerr << "cannot retrieve version information, error: " 
             << strerror(-rc) << endl;
        exit(EXIT_FAILURE);
    }
    cout << version.data() << endl;
    return 0;
}