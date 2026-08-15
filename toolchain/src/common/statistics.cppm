export module lito.toolchain.common:statistics;

import rstd;

using namespace rstd::prelude;

export namespace lito
{

struct ToolchainStatistics {
    usize target_queries {};
    usize preprocessor_environment_entries {};
    usize preprocessor_environment_queries {};
    usize preprocessor_environment_hits {};
    usize builtin_snapshots {};
    usize builtin_refreshes {};
    usize builtin_hits {};
    usize builtin_macro_processes {};
    usize builtin_capability_processes {};
    usize clang_macros {};
    usize native_macro_owners {};
    usize clang_capabilities {};
    usize native_capabilities {};
    usize builtin_macro_output_bytes {};
    usize builtin_capability_input_bytes {};
    usize builtin_capability_output_bytes {};
    usize ignored_builtin_options {};
};

} // namespace lito
