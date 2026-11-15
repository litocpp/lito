export module helper_codegen;

export namespace helper_codegen
{

template<class T>
constexpr auto helper_identity(T value) -> T {
    return value;
}

struct [[pmacro::derive("compiler-test::helper_fields", "compiler-test::helper_second")]]
[[pmacro::helper("compiler-test::helper_fields", record)]] Mask {
    [[clang::annotate("user.field")]] [[pmacro::helper("compiler-test::helper_fields", default)]]
    [[pmacro::helper("compiler-test::helper_second", rename("selected", nested(1, 2)))]]
    int selected = 42;
    [[pmacro::helper("compiler-test::helper_fields")]]
    int spare = 0;

    [[pmacro::helper("compiler-test::helper_fields", method)]]
    auto allows() const -> int {
        auto read = [](int value) {
            return value;
        };
        return read(selected) + spare;
    }
    template<class Input>
    auto validate() const -> int {
        // Instantiation flushes deferred inline CodeGen before this record is complete.
        static_assert(helper_identity(0) == 0);
        auto read = [](int value) {
            return value;
        };
        return read(selected);
    }
};

static_assert(helper_fields_checked && helper_second_checked);

struct Outer {
    struct [[pmacro::derive("compiler-test::helper_member")]] Nested {
        [[pmacro::helper("compiler-test::helper_member", default)]]
        int  value = 42;
        auto read() const -> int { return value; }
    };
};

template<typename T>
struct [[pmacro::derive("compiler-test::helper_member")]] Box {
    [[pmacro::helper("compiler-test::helper_member", default)]]
    T    value = 42;
    auto read() const -> T { return value; }
};

enum class [[pmacro::derive("compiler-test::helper_member")]] Choice
{
    Value [[pmacro::helper("compiler-test::helper_member", default)]] = 42,
};

struct [[pmacro::attr("compiler-test::helper_snapshot")]] [[pmacro::derive(
    "compiler-test::helper_member")]] Snapshot {
    [[pmacro::helper("compiler-test::helper_member", default)]]
    int  value = 42;
    auto read() const -> int { return value; }
};

auto check() -> int {
    return Mask {}.allows() + Outer::Nested {}.read() + Box<int> {}.read() +
           static_cast<int>(Choice::Value) + Snapshot {}.read();
}

} // namespace helper_codegen
