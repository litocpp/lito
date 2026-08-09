module;

#include "config.hpp"

export module fixture.discovery.lib;

#if LITO_DISCOVERY_DETAIL
export import LITO_DISCOVERY_MODULE;
#endif

export auto discovery_value() -> int {
    return discovery_detail();
}
