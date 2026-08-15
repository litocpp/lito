module;
#include <rstd/enum.hpp>

export module lito.toolchain.contract;

import rstd;
export import lito.toolchain.error_contract;
import lito.error;
import lito.dependency.contract;

using namespace rstd::prelude;

export namespace lito
{

struct CompilerIdentity {
    PathBuf path;
    String  version;
    String  target;
    PathBuf resource_directory;
    String  build_identity;
    u64     size {};
    i64     modified_seconds {};
    u32     modified_nanoseconds {};

    auto clone() const -> CompilerIdentity {
        return CompilerIdentity {
            .path                 = path.clone(),
            .version              = version.clone(),
            .target               = target.clone(),
            .resource_directory   = resource_directory.clone(),
            .build_identity       = build_identity.clone(),
            .size                 = size,
            .modified_seconds     = modified_seconds,
            .modified_nanoseconds = modified_nanoseconds,
        };
    }
};

struct CompileCommandResult {
    i32                  exit_code {};
    String               standard_output;
    String               standard_error;
    rstd::time::Duration elapsed;
};

struct CompileInvocation {
    Vec<String>     arguments;
    PathBuf         working_directory;
    String          identity;
    PathBuf         staged_object;
    PathBuf         final_object;
    Option<PathBuf> staged_bmi;
    Option<PathBuf> final_bmi;

    auto clone() const -> CompileInvocation {
        return CompileInvocation {
            .arguments         = arguments.clone(),
            .working_directory = working_directory.clone(),
            .identity          = identity.clone(),
            .staged_object     = staged_object.clone(),
            .final_object      = final_object.clone(),
            .staged_bmi        = staged_bmi.is_some() ? Some(staged_bmi->clone()) : None(),
            .final_bmi         = final_bmi.is_some() ? Some(final_bmi->clone()) : None(),
        };
    }
};

enum class LinkArchiveMode
{
    Normal,
    Whole,
};

struct LinkArchive {
    PathBuf         path;
    LinkArchiveMode mode { LinkArchiveMode::Normal };
};

class ResolvedLinkInput {
    RSTD_ENUM(ResolvedLinkInput,
              (Archive, (LinkArchive archive;)),
              (External, (LinkArgumentSequence arguments;)))
};

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
