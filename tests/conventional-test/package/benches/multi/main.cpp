import fixture.conventional.multi;

auto main() -> int {
    return fixture_multi_bench_value() == 7 ? 0 : 1;
}
