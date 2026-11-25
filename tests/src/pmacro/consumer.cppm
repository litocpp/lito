module;
#include "local.hpp"

export module lito.test.pmacro.consumer;

import lito.test.pmacro.generated;

struct ImportSeed {
    Generated value;
};

static_assert(PMACRO_LOCAL_VALUE == 7);
constexpr auto pmacro_logical_source = __FILE__;
static_assert(pmacro_logical_source[0] != '\0');

export {
    [[pmacro::attr("pmacro-basic::scoped_identity")]]
    inline constexpr bool scoped_identity_valid = true;

    struct [[pmacro::derive("pmacro-basic::scoped_derive")]] ScopedValue {};

    [[pmacro::invoke("pmacro-basic::scoped_invoke",
                     InvokedValue,
                     { value: (long), options: [missing_type, { text: "balanced,){}[]" }] })]];

    [[pmacro::invoke("pmacro-basic-provider::invoke_tokens")]];
    [[pmacro::invoke(
        "pmacro-basic-provider::invoke_tokens", struct InvokedFirst { int value; };
        struct InvokedSecond { InvokedFirst first; };
        inline constexpr InvokedSecond invoked_pair { { 42 } };
        inline constexpr int           invoked_spaced_number = 0xe + 1;
        static_assert(invoked_spaced_number == 15);
        namespace invoked_generated {
            struct [[pmacro::derive("pmacro-basic::scoped_derive")]] InvokedDerived {};
        })]];

    [[pmacro::attr("pmacro-basic-provider::identity")]]
    inline constexpr int prefix_value = 7;

    [[pmacro::invoke("pmacro-basic-provider::splice_tokens",
                     2,
                     { 0xe + 1 },
                     inline constexpr int  spliced_value     = (slot + (nested));
                     inline constexpr char spliced_literal[] = "slot";)]];
    static_assert(spliced_value == 17 && spliced_literal[0] == 's');

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

    struct [[pmacro::attr("pmacro-basic-provider::recursive")]] RecursiveSeed {};

    struct [[pmacro::attr("pmacro-basic-provider::diagnostic")]] DiagnosticSeed {};
}
