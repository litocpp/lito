#include <cstdio>
#include <cstring>

import fixture.multi;

int main(int argc, char** argv) {
    if (fixture::multi::answer() != 42) return 1;
    if (argc != 2 || std::strcmp(argv[1], "expected-benchmark") != 0) return 2;
    auto* marker = std::fopen("marker.txt", "r");
    if (marker == nullptr) return 3;
    std::fclose(marker);
    return 0;
}
