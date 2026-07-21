module demo.base;

auto twice_incremented(int value) -> int {
    return increment(increment(value));
}
