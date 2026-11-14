#define FLAG 1
#undef PMACRO_CONDITIONAL_ABSENT_FLAG

#if FLAG
[[pmacro::invoke("compiler-test::declarations", inline constexpr int selected_if = 42;)]]
;
#else
[[pmacro::invoke("missing::must_not_execute")]];
[[pmacro::invoke(42, { invalid: [not_cpp::Type] })]];
#endif

#ifdef FLAG
[[pmacro::invoke("compiler-test::declarations", inline constexpr int selected_ifdef = 42;)]]
;
#else
[[pmacro::invoke("missing::must_not_execute")]];
#endif

#ifndef PMACRO_CONDITIONAL_ABSENT_FLAG
[[pmacro::invoke("compiler-test::declarations", inline constexpr int selected_ifndef = 42;)]]
;
#else
[[pmacro::invoke(42, { invalid: [not_cpp::Type] })]];
#endif

#ifdef PMACRO_CONDITIONAL_ABSENT_FLAG
[[pmacro::invoke("missing::must_not_execute")]];
#else
[[pmacro::invoke("compiler-test::declarations", inline constexpr int selected_else = 42;)]]
;
#endif

struct SkippedInvalidMember {
#if 0
    [[pmacro::invoke(42, { invalid: [not_cpp::Type] })]];
#endif
    int value = 42;
};

#if 0
struct SkippedInvalidRecord {
    [[pmacro::invoke(42, { invalid: [not_cpp::Type] })]];
};
#endif

static_assert(selected_if == 42 && selected_ifdef == 42 && selected_ifndef == 42 &&
              selected_else == 42);
static_assert(SkippedInvalidMember {}.value == 42);

namespace scoped
{
[[pmacro::invoke("compiler-test::function_positions")]];
// clang-format off
#if 0
{
[[pmacro::invoke(42, { invalid: [not_cpp::Type] })]];
#endif
// clang-format on
struct Later {
    long value;
};
} // namespace scoped
static_assert(scoped::invoked_after == 42);
static_assert(__is_same(scoped::invoked_later_type, scoped::Later));
static_assert(scoped::invoked_end() == sizeof(scoped::Later));

namespace inactive_close
{
[[pmacro::invoke("compiler-test::function_positions")]];
// clang-format off
#if 0
}
[[pmacro::invoke(42, { invalid: [not_cpp::Type] })]];
#endif
// clang-format on
struct Later {
    int value;
};
} // namespace inactive_close
static_assert(inactive_close::invoked_after == 42);
static_assert(__is_same(inactive_close::invoked_later_type, inactive_close::Later));
static_assert(inactive_close::invoked_end() == sizeof(inactive_close::Later));

namespace linkage_deferred
{
extern "C" {
[[pmacro::invoke("compiler-test::function_positions")]];
}
struct Later {
    long value;
};
} // namespace linkage_deferred
static_assert(linkage_deferred::invoked_after == 42);
static_assert(__is_same(linkage_deferred::invoked_later_type, linkage_deferred::Later));
static_assert(linkage_deferred::invoked_end() == sizeof(linkage_deferred::Later));

#undef FLAG

int main() {
    return selected_if != 42 || selected_else != 42;
}
