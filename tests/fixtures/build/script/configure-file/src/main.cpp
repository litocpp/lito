#include <fixture/build_config.hpp>

static_assert(FIXTURE_ABI_REVISION == 3);
static_assert(! FIXTURE_ENABLE_TRACE);
static_assert(FIXTURE_LITERAL[0] == '@');
static_assert(FIXTURE_AT[0] == '@');

int main() {
    return FIXTURE_PROFILE[0] == 'd' ? 0 : 1;
}
