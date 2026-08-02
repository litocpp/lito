#include <cstdio>

import fixture.test.lib;

int main() {
    std::puts("fixture fail executed");
    return fixture::test::answer() == 42 ? 7 : 0;
}
