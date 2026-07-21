module demo.app;

auto four_incremented(int value) -> int {
    return twice_incremented(twice_incremented(value));
}
