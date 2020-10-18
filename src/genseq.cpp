#include <fstream>
#include <iostream>
using namespace std;

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0]
             << " <file name> <num elements, divisible by " << sizeof(size_t)
             << ">" << endl;
        exit(EXIT_FAILURE);
    }
    ofstream os(argv[1], ios::binary | ios::trunc);
    for (size_t i = 0; i != atoll(argv[2]) / sizeof(size_t); ++i)
        os.write((const char *)&i, sizeof(i));
    return 0;
}
