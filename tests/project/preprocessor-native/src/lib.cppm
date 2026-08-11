module;

#include "config.hpp"

#define LITO_JOIN(left, right) left##right
#define LITO_IMPORT(name) import name;
#define LITO_OPTIONAL_IMPORT(...) __VA_OPT__(LITO_IMPORT(__VA_ARGS__))
#define LITO_PRAGMA(value) _Pragma(#value)

export module fixture.preprocessor.native;

#if defined(LITO_ENABLED) && LITO_JOIN(LITO_, ENABLED) && ((2 + 3 * 4) == 14) && \
    __has_include("config.hpp")
LITO_OPTIONAL_IMPORT(LITO_PARTITION)
#endif

#if __has_include("missing.hpp")
import :missing;
#endif

#if !__has_builtin(__builtin_assume) || \
    !__has_cpp_attribute(_Clang::__lifetimebound__) || \
    !__has_attribute(__type_visibility__) || \
    !__has_warning("-Winvalid-specialization")
import :standard_library_capability_failure;
#endif

#if __has_builtin(__builtin_lito_missing) || __has_feature(cxx_exceptions) || \
    __has_extension(cxx_exceptions) || __has_feature(cxx_rtti) || \
    __has_extension(cxx_rtti) || __is_identifier(class) || \
    __is_identifier(_Atomic) || __is_identifier(__datasizeof) || \
    !__is_identifier(lito_identifier) || defined(__EXCEPTIONS) || \
    defined(__cpp_exceptions) || defined(__GXX_RTTI) || defined(__cpp_rtti)
import :native_builtin_failure;
#endif

#if 0 && (1 / 0)
import :short_circuit_failure;
#endif

#if !(1 ? 1 : (1 / 0))
import :conditional_failure;
#endif

#define LITO_STACKED 1
#pragma push_macro("LITO_STACKED")
#undef LITO_STACKED
#define LITO_STACKED 0
#pragma pop_macro("LITO_STACKED")

#if LITO_STACKED != 1
import :pragma_stack_failure;
#endif

LITO_PRAGMA(push_macro("LITO_STACKED"))
#undef LITO_STACKED
#define LITO_STACKED 0
LITO_PRAGMA(pop_macro("LITO_STACKED"))

#if LITO_STACKED != 1
import :pragma_operator_failure;
#endif

constexpr auto ignored_import = R"tag(import fixture.preprocessor.missing;)tag";

export auto native_preprocessor_value() -> int {
    return native_preprocessor_dependency();
}
