export module lito.toolchain:clang_preprocessor_model;

import rstd;
import lito.cpp;
import lito.error;
import lito.toolchain.contract;
import lito.package.target_contract;
import lito.system.process;
import lito.system.environment;
import lito.frontend;
import :clang_options;
import :command;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{
namespace lexical      = frontend::lexical;
namespace preprocessor = frontend::preprocessor;
} // namespace lito

export namespace lito::toolchain
{

struct IncludeSearchEntry {
    PathBuf directory;
    bool    system { true };
};

struct BuiltinSemanticContext {
    String language_standard;
    bool   rtti { false };
    bool   exceptions { false };
};

inline constexpr auto CLANG_STANDARD_LIBRARY_CAPABILITY_ID =
    "lito-clang-standard-library-capabilities-v1"_str;

struct ClangBuiltinEnvironmentSnapshot {
    String                                   key;
    String                                   identity;
    lexical::SharedSourceSnapshot            source;
    Vec<preprocessor::SharedMacroDefinition> definitions;
    rstd::collections::HashMap<String, i64>  capabilities;
    usize                                    clang_macro_count {};
    usize                                    native_macro_count {};
    usize                                    clang_capability_count {};
    usize                                    native_capability_count {};
    usize                                    macro_output_bytes {};
    usize                                    capability_input_bytes {};
    usize                                    capability_output_bytes {};
};

using SharedClangBuiltinEnvironmentSnapshot = rstd::sync::Arc<ClangBuiltinEnvironmentSnapshot>;

struct PreprocessorEnvironmentKey {
    String  context_id;
    PathBuf working_directory;

    static auto make(ref<str> context_id, ref<rstd::path::Path> working_directory)
        -> PreprocessorEnvironmentKey {
        return PreprocessorEnvironmentKey {
            .context_id        = String::make(context_id),
            .working_directory = PathBuf::from(working_directory),
        };
    }

    auto matches(ref<str> context, ref<rstd::path::Path> working) const noexcept -> bool {
        return context_id.as_str() == context && working_directory.as_path() == working;
    }
};

struct PreprocessorEnvironment {
    PreprocessorEnvironmentKey                  key;
    SharedClangBuiltinEnvironmentSnapshot       builtin_environment;
    lexical::SharedSourceSnapshot               native_source;
    Vec<preprocessor::SharedMacroDefinition>    native_definitions;
    lexical::SharedSourceSnapshot               command_line_source;
    Vec<preprocessor::PredefinedMacroOperation> command_line_macros;
    BuiltinSemanticContext                      semantic_context;
    Vec<IncludeSearchEntry>                     include_search;
    Vec<String>                                 query_command;
    String                                      identity;
    String                                      date;
    String                                      time;
};

using SharedPreprocessorEnvironment = rstd::sync::Arc<PreprocessorEnvironment>;

} // namespace lito::toolchain
