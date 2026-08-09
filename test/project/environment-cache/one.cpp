#include "shared.hpp"

extern "C" auto fixture_environment_one() -> int {
    return 1 + fixture_environment_shared;
}
