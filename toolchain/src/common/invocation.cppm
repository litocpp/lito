export module lito.toolchain.common:invocation;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

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

} // namespace lito
