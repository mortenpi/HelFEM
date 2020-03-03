#include <iostream>

using namespace std;

// libhelfem:
extern "C" {
    void helfem_test();
    void helfem_runstuff();
}


int main(int argc, char * argv[]) {
    helfem_test();
    helfem_runstuff();
}
