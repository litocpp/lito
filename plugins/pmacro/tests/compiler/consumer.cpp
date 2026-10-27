namespace sample
{
using Choice  = long;
using Payload = long;
struct [[pmacro::attr("compiler-test::inspect")]] Snapshot {
    Payload   value;
    Snapshot* next;
};
static_assert(__is_same(inspected_type, Payload));
template<class T>
struct [[pmacro::attr("compiler-test::inspect_template")]] Box {
    T value;
};
static_assert(__is_same(decltype(Box<int>::value), int));
static_assert(__is_same(decltype(Box<long>::value), long));
static_assert(template_once == 1);
struct Value;
using Before = Value;
struct [[pmacro::attr("compiler-test::replace")]] Value {
    Missing old;
    static_assert(false);
};
static_assert(__is_same(Before, Value));
static_assert(__is_same(decltype(Value::value), long));
static_assert(__is_same(decltype(Value::next), Value*));
static_assert(__is_trivial(Value));
struct [[pmacro::derive("compiler-test::after")]] Seed {
    using Choice = int;
};
static_assert(__is_same(generated_choice, long));
static_assert(generated() == 42);
struct [[pmacro::derive("compiler-test::at_end")]] End {};
struct Later {
    long value;
};
[[pmacro::attr("compiler-test::import_function")]]
int imported(int value);
[[pmacro::attr("compiler-test::export_function")]]
int exported(int value) {
    return Missing;
}
} // namespace sample
static_assert(__is_same(sample::later_type, sample::Later));
static_assert(sample::deferred() == sizeof(sample::Later));

struct [[pmacro::attr("compiler-test::no_namespace")]] Global {};
struct [[pmacro::attr("compiler-test::invalid_input")]] Invalid {
    Missing value;
};

namespace boundary
{
struct Base {
    int base;
};
struct [[pmacro::attr("compiler-test::rewrite", "struct Final final : Base { long value; };")]]
Final final : Base {
    Missing original;
};
static_assert(__is_final(Final));
static_assert(__is_base_of(Base, Final));
static_assert(__is_same(decltype(Final::value), long));
using Choice = long;
typedef struct [[pmacro::derive("compiler-test::after")]] Tail {
} Alias, *Pointer;
static_assert(__is_same(Alias, Tail) && __is_same(Pointer, Tail*));
static_assert(generated() == 42);
struct Outer {
    using Choice = short;
    struct [[pmacro::derive("compiler-test::after")]] Inner {
        using Choice = int;
    } object;
};
static_assert(__is_same(Outer::generated_choice, short));
static_assert(Outer {}.generated() == 42);
} // namespace boundary

namespace chain
{
using Payload = long;
struct [[pmacro::attr("compiler-test::rewrite",
                      "struct Chained { Payload value; Chained* next; };")]]
[[pmacro::attr("compiler-test::inspect")]] Chained {
    Missing original;
};
static_assert(__is_same(inspected_type, Payload));
static_assert(__is_same(decltype(Chained::next), Chained*));
} // namespace chain

namespace combined
{
using Choice = long;
struct [[pmacro::attr("compiler-test::rewrite", "struct Seed { long value; };")]] [[pmacro::derive(
    "compiler-test::after")]] Seed {
    Missing original;
};
static_assert(generated() == 42 && __is_same(decltype(Seed::value), long));
} // namespace combined
namespace reversed
{
using Choice = long;
struct [[pmacro::derive("compiler-test::after")]] [[pmacro::attr("compiler-test::rewrite",
                                                                 "struct Seed { long value; };")]]
Seed {
    Missing original;
};
static_assert(generated() == 42 && __is_same(decltype(Seed::value), long));
} // namespace reversed
namespace generated_derive
{
using Choice = long;
struct [[pmacro::attr("compiler-test::derive_generated")]] Generated {};
static_assert(generated() == 42 && __is_same(decltype(Generated::value), long));
} // namespace generated_derive

namespace model
{
namespace external
{
using Count = long;
}
struct Outer {
    using Payload = external::Count;
    struct [[pmacro::attr("compiler-test::inspect")]] Snapshot {
        Payload                value;
        const Outer::Snapshot* next;
    };
    static_assert(__is_same(inspected_type, Payload));
};
namespace constrained
{
template<class T>
    requires(sizeof(T) > 0)
struct [[pmacro::attr("compiler-test::inspect_template")]] Box {
    T value;
};
static_assert(sizeof(Box<long>) == sizeof(long));
} // namespace constrained
} // namespace model

namespace ordered
{
struct [[pmacro::derive("compiler-test::first", "compiler-test::second")]] Seed {};
static_assert(third_value == 3);
struct Later {
    long value;
};
} // namespace ordered
namespace ordered
{
static_assert(second_end == sizeof(Later) + 1);
}

namespace nested::target
{
struct Outer {
    struct [[pmacro::derive("compiler-test::at_end")]] Inner {};
};
struct Later {};
} // namespace nested::target
static_assert(__is_same(nested::target::later_type, nested::target::Later));

namespace anonymous
{
namespace
{
struct [[pmacro::derive("compiler-test::at_end")]] Seed {};
struct Later {};
} // namespace
static_assert(__is_same(later_type, Later));
} // namespace anonymous

namespace inlined
{
inline namespace version
{
extern "C" {
struct [[pmacro::derive("compiler-test::at_end")]] Seed {};
}
struct Later {};
} // namespace version
static_assert(__is_same(later_type, Later));
} // namespace inlined

int main() {
    return sample::imported(41) != 42 || sample::wrapper(41) != 42;
}
