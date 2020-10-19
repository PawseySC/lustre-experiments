#include <lustre/lustreapi.h>

#include <cerrno>
#include <iostream>
#include <vector>

using namespace std;

int main(int argc, char* argv[]) {
    if(argc != 2) {
        cerr << "Usage: " << argv[0] << " <file name>" << endl;
        exit(EXIT_FAILURE);
    }
    llapi_layout* layout = llapi_layout_get_by_path(argv[1], 0);
    // get layout attributes
    uint64_t size = 0;
    llapi_layout_stripe_size_get(layout, &size);
    uint64_t count = 0;
    llapi_layout_stripe_count_get(layout, &count);
    // print
    cout << "Stripe size: " << size << endl;
    cout << "Stripe count: " << count << endl;
    uint64_t ostIndex = uint64_t(-1);
    for (int i = 0; i != count; ++i) {
        llapi_layout_ost_index_get(layout, i, &ostIndex);
        cout << "Stripe " << i << ": OST " << ostIndex << endl;
    }
    llapi_layout_free(layout);

    return 0;
}