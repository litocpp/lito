module;

#include "detail.hpp"

export module demo.base:detail;

export auto increment(int value) -> int {
    return value + demo_increment;
}
