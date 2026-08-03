#pragma once

#include_next <choice.hpp>

#undef TENON_SELECTED_VALUE
#define TENON_SELECTED_VALUE (100 + TENON_MID_VALUE)
