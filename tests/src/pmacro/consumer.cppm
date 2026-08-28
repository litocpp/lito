module;
#include "local.hpp"

export module lito.test.pmacro.consumer;

struct [[pmacro::attr("pmacro-basic-provider::emit_import")]] ImportSeed {};

static_assert(PMACRO_LOCAL_VALUE == 7);
constexpr auto pmacro_logical_source = __FILE__;
static_assert(pmacro_logical_source[0] != '\0');

export {
    [[pmacro::attr("pmacro-basic-provider::identity")]]
    inline constexpr int prefix_value = 7;

    struct [[pmacro::attr("pmacro-basic-provider::identity")]] Value {
        int value;
    };

    struct MemberValue {
        [[pmacro::attr("pmacro-basic-provider::identity")]]
        int value;
    };

    struct [[pmacro::attr("pmacro-basic-provider::identity")]] NestedValue {
        [[pmacro::attr("pmacro-basic-provider::identity")]]
        int value;
    };

    struct [[pmacro::attr("pmacro-basic-provider::token_model")]] TokenModel {
        int value = 42;
    };

    struct [[pmacro::attr("pmacro-basic-provider::arguments", 42, "value")]] Arguments {
        int value;
    };

    struct [[pmacro::derive("pmacro-basic-provider::derive_equal",
                            "pmacro-basic-provider::derive_marker")]] Equal {
        [[pmacro::helper("pmacro-basic-provider::derive_equal", "value")]]
        int value;
    };

    struct [[pmacro::attr("pmacro-basic-provider::replace")]] ReplaceSeed {};

    struct [[pmacro::attr("pmacro-basic-provider::remove")]] RemoveSeed {};

    struct [[pmacro::attr("pmacro-basic-provider::recursive")]] RecursiveSeed {};

    struct [[pmacro::attr("pmacro-basic-provider::diagnostic")]] DiagnosticSeed {};
}
