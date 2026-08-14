import fixture.test.attach;

auto main() -> int {
    return fixture::attach::registrations() == 1 ? 0 : 1;
}
