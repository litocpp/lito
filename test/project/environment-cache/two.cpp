#include "shared.hpp"

extern "C" auto fixture_environment_two() -> int {
    return 2 + fixture_environment_shared;
}
