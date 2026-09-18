module;
#include <rstd/macro.hpp>

module lito.driver:build.generated.model;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import :build.script.error;
import :build.action_graph;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

struct BuildScriptHandle {
    const void* identity {};
};

enum class GeneratedOutputKind
{
    Other,
    Header,
    Source,
};

struct DeclaredActionOutput {
    String              path;
    GeneratedOutputKind kind { GeneratedOutputKind::Other };
};

auto inferred_generated_output_kind(ref<str> path) noexcept -> GeneratedOutputKind {
    auto source    = PathBuf::from(path);
    auto extension = source.as_path().extension();
    if (extension.is_none()) return GeneratedOutputKind::Other;
    auto text = extension->to_str();
    if (text == Some("h"_str) || text == Some("hpp"_str)) return GeneratedOutputKind::Header;
    if (text == Some("cpp"_str) || text == Some("cppm"_str)) return GeneratedOutputKind::Source;
    return GeneratedOutputKind::Other;
}

constexpr auto generated_output_kind_name(GeneratedOutputKind kind) noexcept -> ref<str> {
    switch (kind) {
    case GeneratedOutputKind::Other: return "file"_str;
    case GeneratedOutputKind::Header: return "header"_str;
    case GeneratedOutputKind::Source: return "source"_str;
    }
    return "file"_str;
}

struct GeneratedActionOutput {
    String                           package;
    PathBuf                          relative;
    String                           action_identity;
    usize                            producer {};
    GeneratedOutputKind              kind { GeneratedOutputKind::Other };
    cpp::GeneratedSourceAvailability availability { cpp::GeneratedSourceAvailability::BeforeScan };
};

struct ResolvedActionInput {
    PathBuf       path;
    String        digest;
    Option<usize> producer;
};

struct ResolvedExternalSourceFile {
    const cpp::ExternalSourceRoot* source {};
    PathBuf                        relative;
    PathBuf                        path;
};

struct ResolvedActionTool {
    String                                 package;
    String                                 alias;
    String                                 identity;
    String                                 digest;
    PathBuf                                executable;
    Option<lito::package::PackageTargetId> target;
    Option<BuildArtifactId>                artifact;
};

struct ResolvedActionInputRoot {
    const cpp::ExternalSourceRoot* source {};
    PathBuf                        path;
};

enum class RegisteredActionKind
{
    Process,
    Write,
    Copy,
    CppLeadingPreamble,
};

struct RegisteredAction {
    RegisteredActionKind             kind { RegisteredActionKind::Process };
    String                           package;
    String                           identity;
    String                           label;
    PathBuf                          working_directory;
    Option<ResolvedActionTool>       tool;
    Vec<ResolvedActionTool>          tools;
    Vec<ResolvedActionInputRoot>     input_roots;
    Vec<String>                      arguments;
    Vec<ResolvedActionInput>         inputs;
    Vec<PathBuf>                     outputs;
    Option<usize>                    output_working_directory;
    Option<usize>                    depfile_output;
    Vec<PathBuf>                     depfile_roots;
    String                           content;
    cpp::GeneratedSourceAvailability availability { cpp::GeneratedSourceAvailability::BeforeScan };
};

struct ToolActionOutcome {
    bool                   changed { false };
    Vec<BuildScriptHandle> outputs;
};

struct PackageHostToolHandle {
    String                         package;
    lito::package::PackageTargetId target;
};

struct GeneratedActionWorkerResult {
    usize                    action {};
    BuildScriptResult<empty> outcome;
};

struct PublishedGeneratedAction {
    usize         action {};
    BuildActionId id;
};

struct DeclaredActionInput {
    Option<String>    path;
    BuildScriptHandle handle;
};
struct TargetScriptRequest {
    String package;
    String kind;
    String name;
};
struct WriteScriptRequest {
    String                   package;
    DeclaredActionOutput     output;
    String                   content;
    Vec<DeclaredActionInput> inputs;
};
struct CopyScriptRequest {
    String               package;
    DeclaredActionInput  input;
    DeclaredActionOutput output;
};
struct TransformScriptRequest {
    String                    package;
    String                    kind;
    BuildScriptHandle         input;
    Vec<DeclaredActionOutput> outputs;
};
struct ActionDepfileRequest {
    i64         output;
    Vec<String> roots;
};
struct RunScriptRequest {
    BuildScriptHandle            tool;
    String                       package;
    String                       cwd;
    Vec<String>                  args;
    Vec<DeclaredActionInput>     inputs;
    Vec<DeclaredActionOutput>    outputs;
    Vec<BuildScriptHandle>       tools;
    Vec<BuildScriptHandle>       input_roots;
    Option<i64>                  output_cwd;
    Option<ActionDepfileRequest> depfile;
};
struct ScriptDependencyInfo {
    String      alias;
    String      provider;
    String      version;
    String      identity;
    Vec<String> targets;
};
struct ScriptPreprocessorInfo {
    cpp::PreprocessorProjection projection;
    String                      compiler_flavor;
    String                      target;
};

} // namespace lito
