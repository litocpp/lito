import fixture.conventional;
import fixture.conventional.bench_helper;

auto main() -> int {
    return fixture_answer() + fixture_bench_value() == 43 ? 0 : 1;
}
