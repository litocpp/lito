module;

#include <choice.hpp>

#if __has_include(<optional.hpp>)
#include <optional.hpp>
#else
#define LITO_OPTIONAL_VALUE 0
#endif

export module fixture.scan.cache;

export auto scan_cache_value() -> int {
    return LITO_SELECTED_VALUE + LITO_OPTIONAL_VALUE;
}
