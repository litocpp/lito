import fixture.associated.workspace;
import fixture.associated.workspace.bench_helper;

auto main() -> int {
    return fixture_associated_value() + fixture_bench_helper_value() == 3 ? 0 : 1;
}
