#include <cstdio>
#include <cstring>

import fixture.test.lib;

int main(int argc, char** argv) {
    std::puts("fixture pass executed");
    if (fixture::test::answer() != 42) return 1;
    if (argc != 2 || std::strcmp(argv[1], "expected-argument") != 0) return 2;
    auto* marker = std::fopen("marker.txt", "r");
    if (marker == nullptr) return 3;
    std::fclose(marker);
    return 0;
}
