module;

#include "config.hpp"

#define TENON_JOIN(left, right) left##right
#define TENON_IMPORT(name) import name;
#define TENON_OPTIONAL_IMPORT(...) __VA_OPT__(TENON_IMPORT(__VA_ARGS__))
#define TENON_PRAGMA(value) _Pragma(#value)

export module fixture.preprocessor.native;

#if defined(TENON_ENABLED) && TENON_JOIN(TENON_, ENABLED) && ((2 + 3 * 4) == 14) && \
    __has_include("config.hpp")
TENON_OPTIONAL_IMPORT(TENON_PARTITION)
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

#if __has_builtin(__builtin_tenon_missing) || __has_feature(cxx_exceptions) || \
    __has_extension(cxx_exceptions) || __has_feature(cxx_rtti) || \
    __has_extension(cxx_rtti) || __is_identifier(class) || \
    __is_identifier(_Atomic) || __is_identifier(__datasizeof) || \
    !__is_identifier(tenon_identifier) || defined(__EXCEPTIONS) || \
    defined(__cpp_exceptions) || defined(__GXX_RTTI) || defined(__cpp_rtti)
import :native_builtin_failure;
#endif

#if 0 && (1 / 0)
import :short_circuit_failure;
#endif

#if !(1 ? 1 : (1 / 0))
import :conditional_failure;
#endif

#define TENON_STACKED 1
#pragma push_macro("TENON_STACKED")
#undef TENON_STACKED
#define TENON_STACKED 0
#pragma pop_macro("TENON_STACKED")

#if TENON_STACKED != 1
import :pragma_stack_failure;
#endif

TENON_PRAGMA(push_macro("TENON_STACKED"))
#undef TENON_STACKED
#define TENON_STACKED 0
TENON_PRAGMA(pop_macro("TENON_STACKED"))

#if TENON_STACKED != 1
import :pragma_operator_failure;
#endif

constexpr auto ignored_import = R"tag(import fixture.preprocessor.missing;)tag";

export auto native_preprocessor_value() -> int {
    return native_preprocessor_dependency();
}
