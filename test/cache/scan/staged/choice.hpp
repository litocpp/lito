#pragma once

#include_next <choice.hpp>

#undef LITO_SELECTED_VALUE
#define LITO_SELECTED_VALUE (100 + LITO_MID_VALUE)
