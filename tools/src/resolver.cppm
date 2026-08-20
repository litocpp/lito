module;
#include <rstd/macro.hpp>

export module lito.tools:resolver;

import rstd;
import lito.system;
import :error;
import :model;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using namespace lito::system;

export namespace lito::tools
{

struct ResolvedTool {
    PathBuf requested;
    PathBuf executable;

    auto clone() const -> ResolvedTool {
        return ResolvedTool {
            .requested  = requested.clone(),
            .executable = executable.clone(),
        };
    }
};

struct ToolProbeCacheEntry {
    PathBuf         requested;
    Option<PathBuf> executable;
};

class ToolResolver {
public:
    explicit ToolResolver(const ResolvedProcessEnvironment& environment,
                          ToolSpec                          tools    = {},
                          Option<HostToolResolutionSink>    reporter = None())
        : environment_(rstd::addressof(environment)),
          tools_(rstd::move(tools)),
          reporter_(rstd::move(reporter)) {}

    auto tools() const noexcept -> const ToolSpec& { return tools_; }

    auto probe(Tool tool) -> ToolResult<Option<ResolvedTool>> {
        return probe(tools_.requested(tool), tool_description(tool));
    }

    auto resolve(Tool tool) -> ToolResult<ResolvedTool> {
        return resolve(tools_.requested(tool), tool_description(tool));
    }

    auto require(Tool tool, const HostToolRequirement& requirement) -> ToolResult<ResolvedTool> {
        auto selected = rstd_try(probe(tool));
        if (selected.is_none()) {
            auto message = rstd::format("cannot provide {} required by {}; cannot resolve {} '{}' "
                                        "from effective PATH; searched: ",
                                        host_tool_capability_name(requirement.capability),
                                        host_tool_requirement_origin_text(requirement.origin),
                                        tool_description(tool),
                                        tools_.requested(tool));
            append_search_directories(message);
            return Err(ToolError::Message(rstd::move(message)));
        }
        auto result = rstd::move(selected).unwrap();
        report(requirement, tool_name(tool), result);
        return Ok(rstd::move(result));
    }

    auto report(const HostToolRequirement& requirement, ref<str> provider, const ResolvedTool& tool)
        -> void {
        for (const auto capability : reported_capabilities_) {
            if (capability == requirement.capability) return;
        }
        reported_capabilities_.push(HostToolCapability(requirement.capability));
        if (reporter_.is_none() || reporter_->notify == nullptr) return;
        auto resolution = HostToolResolution {
            .requirement = requirement.clone(),
            .provider    = String::make(provider),
            .requested   = tool.requested.clone(),
            .executable  = Some(tool.executable.clone()),
        };
        reporter_->notify(reporter_->context, resolution);
    }

    auto report_candidate_missing(const HostToolRequirement& requirement,
                                  ref<str>                   provider,
                                  Tool                       tool) const -> void {
        if (reporter_.is_none() || reporter_->notify == nullptr) return;
        auto resolution = HostToolResolution {
            .kind        = HostToolResolution::Kind::CandidateMissing,
            .requirement = requirement.clone(),
            .provider    = String::make(provider),
            .requested   = PathBuf::from(tools_.requested(tool)),
        };
        reporter_->notify(reporter_->context, resolution);
    }

    auto report_not_required(const HostToolRequirement& requirement, ref<str> detail) const
        -> void {
        if (reporter_.is_none() || reporter_->notify == nullptr) return;
        auto resolution = HostToolResolution {
            .kind        = HostToolResolution::Kind::NotRequired,
            .requirement = requirement.clone(),
            .detail      = String::make(detail),
        };
        reporter_->notify(reporter_->context, resolution);
    }

    auto probe(ref<rstd::path::Path> requested, ref<str> description)
        -> ToolResult<Option<ResolvedTool>> {
        for (const auto& cached : cache_) {
            if (! same_path(cached.requested.as_path(), requested)) continue;
            if (cached.executable.is_none()) return Ok(Option<ResolvedTool> {});
            return Ok(Some(ResolvedTool {
                .requested  = cached.requested.clone(),
                .executable = cached.executable->clone(),
            }));
        }

        auto selected = locate(requested, description);
        if (selected.is_err()) return Err(rstd::into<ToolError>(rstd::move(selected).unwrap_err()));
        if (selected->is_none()) {
            cache_.push(ToolProbeCacheEntry {
                .requested = PathBuf::from(requested),
            });
            return Ok(Option<ResolvedTool> {});
        }
        auto result = ResolvedTool {
            .requested  = PathBuf::from(requested),
            .executable = rstd::move(selected).unwrap().unwrap(),
        };
        cache_.push(ToolProbeCacheEntry {
            .requested  = result.requested.clone(),
            .executable = Some(result.executable.clone()),
        });
        return Ok(Some(rstd::move(result)));
    }

    auto resolve(ref<rstd::path::Path> requested, ref<str> description)
        -> ToolResult<ResolvedTool> {
        auto selected = rstd_try(probe(requested, description));
        if (selected.is_some()) return Ok(rstd::move(selected).unwrap());
        auto message = rstd::format(
            "cannot resolve {} '{}' from effective PATH; searched: ", description, requested);
        append_search_directories(message);
        return Err(ToolError::Message(rstd::move(message)));
    }

private:
    auto same_path(ref<rstd::path::Path> left, ref<rstd::path::Path> right) const noexcept -> bool {
        return left.as_os_str().as_encoded_bytes() == right.as_os_str().as_encoded_bytes();
    }

    auto append_search_directories(String& output) const -> void {
        const auto& directories = environment_->search_directories();
        for (usize index {}; index < directories.len(); ++index) {
            if (index != usize {}) output.push_str(", "_str);
            output.push_str(directories[index].as_path().to_string_lossy().as_str());
        }
    }

    auto locate(ref<rstd::path::Path> requested, ref<str> description)
        -> SystemResult<Option<PathBuf>>;

    const ResolvedProcessEnvironment* environment_ {};
    ToolSpec                          tools_;
    Option<HostToolResolutionSink>    reporter_;
    Vec<ToolProbeCacheEntry>          cache_;
    Vec<HostToolCapability>           reported_capabilities_;
};

} // namespace lito::tools

namespace lito::tools
{

auto ToolResolver::locate(ref<rstd::path::Path> requested, ref<str> description)
    -> SystemResult<Option<PathBuf>> {
    return environment_->locate_executable(requested, description);
}

} // namespace lito::tools
