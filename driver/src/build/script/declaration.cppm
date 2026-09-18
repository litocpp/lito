module;
#include <rstd/macro.hpp>

module lito.driver:build.script.declaration;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import :build.configure;
import :build.generated;
import :build.generated.declaration;
import :build.generated.execution;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

struct BuildScriptInvocation {
    String         owner;
    usize          owner_index {};
    PathBuf        script;
    PathBuf        root;
    Option<String> package;
    Vec<String>    packages;
};

void merge_build_script_report(BuildScriptReport& total, BuildScriptReport report);

struct DeclaredBuildScriptInvocation {
    ToolActionSession    actions;
    ConfigureSession     configure;
    String               owner;
    PathBuf              script;
    rstd::time::Duration elapsed;
};

class BuildScriptDeclaration {
public:
    static auto make(const Option<BuildEventSink>& observer) -> BuildScriptDeclaration {
        return BuildScriptDeclaration(observer);
    }

    auto output_registry() noexcept -> BuildOutputRegistry& { return *outputs_; }

    void push(DeclaredBuildScriptInvocation invocation) {
        invocations_.push(rstd::move(invocation));
    }

    auto host_tool_targets() const -> Vec<lito::package::PackageTargetId> {
        auto result = Vec<lito::package::PackageTargetId>::make();
        for (const auto& invocation : invocations_) {
            auto targets = invocation.actions.host_tool_targets();
            for (auto& target : targets) {
                auto repeated = false;
                for (const auto& existing : result) {
                    if (existing == target) repeated = true;
                }
                if (! repeated) result.push(rstd::move(target));
            }
        }
        return result;
    }

    auto validate_action_schedule() const -> BuildScriptResult<empty> {
        for (const auto& invocation : invocations_) {
            rstd_try(invocation.actions.validate_action_schedule());
        }
        return Ok(empty {});
    }

    auto execute_before_scan(BuildActionGraph& graph, const ExecutionDomainId& domain, usize jobs)
        -> BuildScriptResult<empty> {
        for (auto& invocation : invocations_) {
            rstd_try(invocation.actions.execute(
                graph, domain, cpp::GeneratedSourceAvailability::BeforeScan, jobs));
        }
        return Ok(empty {});
    }

    auto execute(const ResolvedPackageHostTools& tools,
                 cpp::PackageSpec&               package,
                 cpp::PackagePlan&               plan,
                 BuildActionGraph&               graph,
                 const ExecutionDomainId&        domain,
                 usize                           jobs) -> BuildScriptResult<BuildScriptReport> {
        auto report = BuildScriptReport {};
        for (auto& invocation : invocations_) {
            rstd_try(invocation.actions.bind_host_tools(tools));
            invocation.actions.synchronize_generated_identities(package, plan);
            rstd_try(invocation.actions.execute(
                graph, domain, cpp::GeneratedSourceAvailability::AfterScan, jobs));
            invocation.configure.report().executed = true;
            invocation.configure.report().elapsed  = invocation.elapsed;
            auto finished                          = rstd_try(invocation.configure.finish());
            finished.executions.push(BuildScriptExecution {
                .owner   = invocation.owner.clone(),
                .script  = invocation.script.clone(),
                .elapsed = invocation.elapsed,
            });
            if (observer_.is_some() && observer_->notify != nullptr) {
                for (const auto& file : finished.files) {
                    auto kind = file.write == rstd::fs::WriteOutcome::Unchanged
                                    ? BuildEventKind::ConfigureReuse
                                    : BuildEventKind::Configure;
                    observer_->notify(
                        observer_->context,
                        BuildEvent { kind, "lito.configure_file"_str, file.output.as_path() });
                }
            }
            merge_build_script_report(report, rstd::move(finished));
        }
        return Ok(rstd::move(report));
    }

private:
    explicit BuildScriptDeclaration(const Option<BuildEventSink>& observer)
        : outputs_(Box<BuildOutputRegistry>::make()), observer_(observer) {}

    Box<BuildOutputRegistry>           outputs_;
    Vec<DeclaredBuildScriptInvocation> invocations_;
    Option<BuildEventSink>             observer_;
};

void merge_build_script_report(BuildScriptReport& total, BuildScriptReport report) {
    total.executed = total.executed || report.executed;
    total.elapsed += report.elapsed;
    total.created += report.created;
    total.replaced += report.replaced;
    total.unchanged += report.unchanged;
    total.stale_removed += report.stale_removed;
    for (auto& file : report.files) total.files.push(rstd::move(file));
    for (auto& execution : report.executions) total.executions.push(rstd::move(execution));
}

} // namespace lito
