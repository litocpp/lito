import fixture.conventional.workspace;

auto main() -> int {
    return workspace_fixture_answer() == 42 ? 0 : 1;
}
