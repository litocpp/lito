module;

#include <choice.hpp>

#if __has_include(<optional.hpp>)
#include <optional.hpp>
#else
#define TENON_OPTIONAL_VALUE 0
#endif

export module fixture.scan.cache;

export auto scan_cache_value() -> int {
    return TENON_SELECTED_VALUE + TENON_OPTIONAL_VALUE;
}
