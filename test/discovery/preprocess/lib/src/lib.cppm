module;

#include "config.hpp"

export module fixture.discovery.lib;

#if TENON_DISCOVERY_DETAIL
export import TENON_DISCOVERY_MODULE;
#endif

export auto discovery_value() -> int {
    return discovery_detail();
}
