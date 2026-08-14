#include <fixture/build_config.hpp>

int main() {
    return FIXTURE_WORKSPACE_PROFILE[0] == 'd' ? 0 : 1;
}
