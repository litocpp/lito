#define UNUSED_GLOBAL_INVOKE [[pmacro::invoke("missing::must_not_execute")]];
#define UNUSED_ATTRIBUTE     [[deprecated]]
[[pmacro::invoke("compiler-test::declarations",
                 inline constexpr bool invoked_after_unused_attribute = true;)]]
;
static_assert(invoked_after_unused_attribute);

struct SkippedInput {
#if 0
    [[pmacro::invoke("missing::must_not_execute")]];
#endif
    int value = 42;
};

struct UnusedMacroInput {
#define UNUSED_MEMBER_INVOKE [[pmacro::invoke("missing::must_not_execute")]];
#define UNUSED_MULTILINE_INVOKE \
    [[pmacro::invoke("missing::must_not_execute", { not_cpp: [unresolved::Type] })]];
    int value = 42;
};

struct LiteralInput {
    static constexpr auto quoted = "[[pmacro::invoke(\"missing::must_not_execute\")]];";
    static constexpr auto raw =
        R"attribute([[pmacro::invoke("missing::must_not_execute")]];)attribute";
    // [[pmacro::invoke("missing::must_not_execute")]];
    /* [[pmacro::invoke("missing::must_not_execute")]]; */
    int value = 42;
};

struct AttributeArguments {
    [[unrecognized::marker([[pmacro::invoke("missing::must_not_execute")]];)]]
    int unknown = 42;

    [[pmacro::attr("compiler-test::warning", [[pmacro::invoke("missing::must_not_execute")]];)]]
    int registered = 42;
};

static_assert(SkippedInput {}.value == 42);
static_assert(UnusedMacroInput {}.value == 42);
static_assert(LiteralInput {}.value == 42 && LiteralInput::quoted[0] == '[' &&
              LiteralInput::raw[0] == '[');
static_assert(AttributeArguments {}.unknown == 42 && AttributeArguments {}.registered == 42);

#undef UNUSED_GLOBAL_INVOKE
#undef UNUSED_ATTRIBUTE
#undef UNUSED_MEMBER_INVOKE
#undef UNUSED_MULTILINE_INVOKE
int main() {
    return AttributeArguments {}.registered != 42;
}
