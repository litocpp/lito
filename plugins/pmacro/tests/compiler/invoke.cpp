[[pmacro::invoke("compiler-test::declarations")]];
[[pmacro::invoke("compiler-test::function_global_end")]];
[[pmacro::invoke("compiler-test::function_context", Global, NotExported)]];
static_assert(invoked_Global() == 42);

// clang-format off
[[pmacro::invoke("compiler-test::span_model", marker, { key: (long), text: "a,b){}[]", values: [3, {inner: missing::Type}] })]];
// clang-format on
static_assert(exact_invoke_spans);

[[pmacro::invoke("compiler-test::entity_input",
                 GeneratedInput,
                 { value: (long), options: [1, { missing: unknown::Type, text: "a,b){}[]" }] })]];
static_assert(__is_same(decltype(GeneratedInput::value), long));

[[pmacro::invoke(
    "compiler-test::declarations", struct First { int value; }; struct Second { First first; };
    inline constexpr Second pair { { 42 } };)]];
static_assert(pair.first.value == 42);

namespace scoped
{
[[pmacro::invoke("compiler-test::function_context", Named, NotExported)]];
[[pmacro::invoke("compiler-test::function_positions")]];
[[pmacro::invoke("compiler-test::function_emit",
                 inline constexpr int emitted_at_end = invoked_end() + 1;)]]
;
static_assert(invoked_Named() == 42 && invoked_after == 42);
struct Later {
    long value;
};
} // namespace scoped
static_assert(__is_same(scoped::invoked_later_type, scoped::Later));
static_assert(scoped::invoked_end() == sizeof(scoped::Later));
static_assert(scoped::emitted_at_end == sizeof(scoped::Later) + 1);

namespace nested::scope
{
[[pmacro::invoke("compiler-test::function_context", Nested, NotExported)]];
} // namespace nested::scope
static_assert(nested::scope::invoked_Nested() == 42);

namespace anonymous
{
namespace
{
[[pmacro::invoke("compiler-test::function_context", Anonymous, NotExported)]];
[[pmacro::invoke("compiler-test::function_positions")]];
struct Later {};
} // namespace
static_assert(invoked_Anonymous() == 42 && invoked_after == 42 && invoked_end() == sizeof(Later));
} // namespace anonymous

namespace inlined
{
inline namespace version
{
[[pmacro::invoke("compiler-test::function_context", Inline, NotExported)]];
[[pmacro::invoke("compiler-test::function_positions")]];
struct Later {};
} // namespace version
static_assert(invoked_Inline() == 42 && invoked_end() == sizeof(Later));
} // namespace inlined

namespace generated
{
using Choice = long;
[[pmacro::invoke(
    "compiler-test::declarations", struct [[pmacro::derive("compiler-test::after")]] Derived {};
    struct [[pmacro::attr("compiler-test::replace")]] Value { Missing discarded; };
    [[pmacro::invoke("compiler-test::declarations", inline constexpr int nested_invoke = 42;)]];)]];
static_assert(generated() == 42 && __is_same(generated_choice, long));
static_assert(__is_same(decltype(Value::value), long) && nested_invoke == 42);
struct [[pmacro::derive("compiler-test::invoke_from_derive")]] Seed {};
static_assert(invoked_from_derive == 42);
} // namespace generated

[[pmacro::invoke("compiler-test::function_depth", 63)]];
static_assert(invoked_depth == 64);

int main() {
    return pair.first.value == 42 ? 0 : 1;
}
