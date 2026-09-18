module;
#include <rstd/macro.hpp>

module lito.driver:build.generated;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import licrypto;
import lito.tools;
import lito.toolchain;
import :build.script.support;
import :build.generated.model;
import :build.generated.cache;
import :build.layout;
import :build.host_tool;
import :build.event;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{
constexpr auto build_host_api_identity = "lito:build-host-api:v3"_str;

auto replace_all(String& value, ref<str> marker, ref<str> replacement) -> usize {
    auto count = usize {};
    while (value.as_str().contains(marker)) {
        auto found = Option<usize> {};
        for (usize index {}; index + marker.len() <= value.len(); ++index) {
            if (value.as_str().get(index, index + marker.len()).unwrap() == marker) {
                found = Some(index);
                break;
            }
        }
        if (found.is_none()) break;
        value.replace_range(*found, *found + marker.len(), replacement);
        ++count;
    }
    return count;
}

auto xml_text(ref<str> value) -> String {
    auto result = String::make();
    auto start  = usize {};
    for (usize index {}; index < value.len(); ++index) {
        auto byte        = value.as_bytes()[index];
        auto replacement = ""_str;
        if (byte == u8('&'))
            replacement = "&amp;"_str;
        else if (byte == u8('<'))
            replacement = "&lt;"_str;
        else if (byte == u8('>'))
            replacement = "&gt;"_str;
        else if (byte == u8('"'))
            replacement = "&quot;"_str;
        else if (byte == u8('\''))
            replacement = "&apos;"_str;
        else
            continue;
        result.push_str(value.get(start, index).unwrap());
        result.push_str(replacement);
        start = index + usize(1);
    }
    result.push_str(value.get(start, value.len()).unwrap());
    return result;
}

class ToolActionSession {
public:
    auto default_package() const -> const Option<String>& { return default_package_; }
    ToolActionSession(cpp::PackageMetadata&             metadata,
                      cpp::ResolvedNativeTargetPlan&    target_plan,
                      const BuildLayout&                layout,
                      ref<str>                          profile,
                      ref<rstd::path::Path>             script,
                      Option<String>                    default_package,
                      String                            script_owner,
                      Vec<String>                       packages,
                      BuildOutputRegistry&              outputs,
                      const ClangToolchain&             toolchain,
                      ResolvedHostBuildTools            tools,
                      const TargetInfo&                 target_info,
                      const ResolvedProcessEnvironment& environment,
                      const Option<BuildEventSink>&     observer)
        : metadata_(rstd::addressof(metadata)),
          target_plan_(rstd::addressof(target_plan)),
          layout_(rstd::addressof(layout)),
          profile_(String::make(profile)),
          script_(PathBuf::from(script)),
          default_package_(rstd::move(default_package)),
          script_owner_(rstd::move(script_owner)),
          packages_(rstd::move(packages)),
          output_registry_(rstd::addressof(outputs)),
          toolchain_(rstd::addressof(toolchain)),
          tools_(rstd::move(tools)),
          target_info_(rstd::addressof(target_info)),
          environment_(rstd::addressof(environment)),
          observer_(rstd::addressof(observer)) {}

    auto tool(ref<str> alias) const -> BuildScriptResult<BuildScriptHandle>;

    auto target(TargetScriptRequest request) const -> BuildScriptResult<BuildScriptHandle>;

    auto external_dependency(BuildScriptHandle target_handle, ref<str> alias) const
        -> BuildScriptResult<BuildScriptHandle>;

    auto external_source(BuildScriptHandle target_handle, ref<str> alias) const
        -> BuildScriptResult<BuildScriptHandle>;

    auto external_source_file(BuildScriptHandle source_handle, String relative)
        -> BuildScriptResult<BuildScriptHandle>;

    auto external_tool(BuildScriptHandle dependency_handle, ref<str> name) const
        -> BuildScriptResult<BuildScriptHandle>;

    auto host_tool(BuildScriptHandle target_handle, ref<str> package, ref<str> name)
        -> BuildScriptResult<BuildScriptHandle>;

    auto external_dependency_info(BuildScriptHandle dependency_handle) const
        -> BuildScriptResult<ScriptDependencyInfo>;

    auto preprocessor_environment(BuildScriptHandle target_handle) const
        -> BuildScriptResult<ScriptPreprocessorInfo>;

    auto add_generated_source(BuildScriptHandle target_handle, BuildScriptHandle output_handle)
        -> BuildScriptResult<bool>;

    auto add_generated_include(BuildScriptHandle target_handle, String relative)
        -> BuildScriptResult<bool>;

    auto add_generated_definition(BuildScriptHandle target_handle, String definition)
        -> BuildScriptResult<bool>;

    auto add_generated_artifact(BuildScriptHandle          target_handle,
                                BuildScriptHandle          output_handle,
                                cpp::GeneratedArtifactRole role) -> BuildScriptResult<bool>;

    auto write(WriteScriptRequest request) -> BuildScriptResult<ToolActionOutcome>;

    auto copy(CopyScriptRequest request) -> BuildScriptResult<ToolActionOutcome>;

    auto transform(TransformScriptRequest request) -> BuildScriptResult<ToolActionOutcome>;

    auto set_module_identities(Vec<String> identities) -> void {
        module_identities_ = rstd::move(identities);
    }

    auto finalize_module_identity() -> BuildScriptResult<empty>;

    auto host_tool_targets() const -> Vec<lito::package::PackageTargetId>;

    auto validate_action_schedule() const -> BuildScriptResult<empty>;

    auto bind_host_tools(const ResolvedPackageHostTools& tools) -> BuildScriptResult<empty>;

    void synchronize_generated_identities(cpp::PackageSpec& package,
                                          cpp::PackagePlan& plan) const noexcept;

    auto run(RunScriptRequest request) -> BuildScriptResult<ToolActionOutcome>;

    auto publish_actions(BuildActionGraph&                graph,
                         const ExecutionDomainId&         domain,
                         cpp::GeneratedSourceAvailability availability)
        -> BuildScriptResult<Vec<PublishedGeneratedAction>>;

    auto execute(BuildActionGraph&                graph,
                 const ExecutionDomainId&         domain,
                 cpp::GeneratedSourceAvailability availability,
                 usize                            jobs) -> BuildScriptResult<empty>;

private:
    auto action_availability(usize index) const noexcept -> cpp::GeneratedSourceAvailability;

    auto resolve_action_availability(const RegisteredAction& action) const noexcept
        -> cpp::GeneratedSourceAvailability;

    auto refresh_action_identities(ref<str>                                 closure,
                                   Option<cpp::GeneratedSourceAvailability> availability)
        -> BuildScriptResult<empty>;

    auto current_action_dependencies(const RegisteredAction& action) const
        -> BuildScriptResult<Vec<ActionDependency>>;

    static void merge_action_dependencies(Vec<ActionDependency>& destination,
                                          Vec<ActionDependency>  source) {
        for (auto& candidate : source) {
            auto duplicate = false;
            for (const auto& dependency : destination) {
                if (dependency.path.as_path() == candidate.path.as_path()) {
                    duplicate = true;
                    break;
                }
            }
            if (! duplicate) destination.push(rstd::move(candidate));
        }
        const auto order = [](const ActionDependency& left, const ActionDependency& right) {
            return left.path.as_path().to_string_lossy() < right.path.as_path().to_string_lossy();
        };
        rstd::slice_::sort_unstable_by(destination.as_mut_slice().as_mut_ref(), order);
    }

    auto render_process_invocation(const RegisteredAction& action,
                                   ref<rstd::path::Path>   staging) const
        -> BuildScriptResult<Vec<String>>;

    auto write_action_staging(const RegisteredAction& action, ref<rstd::path::Path> staging) const
        -> BuildScriptResult<empty>;

    auto copy_action_staging(const RegisteredAction& action, ref<rstd::path::Path> staging) const
        -> BuildScriptResult<empty>;

    auto transform_action_staging(const RegisteredAction& action,
                                  ref<rstd::path::Path> staging) const -> BuildScriptResult<empty>;

    auto execute_action(const RegisteredAction& action) const -> BuildScriptResult<empty>;

    auto resolve_action_tool(BuildScriptHandle handle) const
        -> BuildScriptResult<ResolvedActionTool>;

    auto external_source_root(BuildScriptHandle handle) const noexcept
        -> const cpp::ExternalSourceRoot*;

    auto external_source_file(BuildScriptHandle handle) const noexcept
        -> const ResolvedExternalSourceFile*;

    auto action_external_source(BuildScriptHandle handle, ref<str> context, usize index) const
        -> BuildScriptResult<const cpp::ExternalSourceRoot*>;

    auto resolve_action_input(const DeclaredActionInput& input,
                              ref<str>                   package,
                              ref<rstd::path::Path>      working_directory,
                              usize                      index,
                              ref<str> context) const -> BuildScriptResult<ResolvedActionInput>;

    auto target_index(BuildScriptHandle handle) const noexcept -> Option<cpp::TargetId>;

    auto generated_output(BuildScriptHandle handle) const noexcept -> const GeneratedActionOutput*;

    auto make_outcome(bool                            changed,
                      ref<str>                        package,
                      const Vec<PathBuf>&             outputs,
                      const Vec<GeneratedOutputKind>& output_kinds,
                      ref<str>                        identity,
                      usize                           producer) -> ToolActionOutcome;

    void emit(BuildEventKind kind, ref<str> target, ref<rstd::path::Path> path) const noexcept;

    cpp::PackageMetadata*             metadata_ {};
    cpp::ResolvedNativeTargetPlan*    target_plan_ {};
    const BuildLayout*                layout_ {};
    String                            profile_;
    PathBuf                           script_;
    Option<String>                    default_package_;
    String                            script_owner_;
    Vec<String>                       packages_;
    BuildOutputRegistry*              output_registry_ {};
    const ClangToolchain*             toolchain_ {};
    ResolvedHostBuildTools            tools_;
    const TargetInfo*                 target_info_ {};
    const ResolvedProcessEnvironment* environment_ {};
    const Option<BuildEventSink>*     observer_ {};
    Vec<String>                       module_identities_;
    Vec<RegisteredAction>             actions_           = Vec<RegisteredAction>::make();
    Vec<Box<GeneratedActionOutput>>   generated_outputs_ = Vec<Box<GeneratedActionOutput>>::make();
    Vec<Box<ResolvedExternalSourceFile>> external_source_files_ =
        Vec<Box<ResolvedExternalSourceFile>>::make();
    Vec<Box<PackageHostToolHandle>> package_host_tool_handles_ =
        Vec<Box<PackageHostToolHandle>>::make();
    struct GeneratedIdentityReplacement {
        cpp::TargetId target {};
        String        previous;
        String        replacement;
    };
    Vec<GeneratedIdentityReplacement> identity_replacements_ =
        Vec<GeneratedIdentityReplacement>::make();
};

} // namespace lito
