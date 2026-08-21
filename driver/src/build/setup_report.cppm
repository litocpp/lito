export module lito.driver:build.setup_report;

import rstd;
import lito.core;
import lito.cpp;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

export namespace lito
{

struct BuildToolResolution {
    PathBuf requested;
    PathBuf executable;
};

struct BuildToolchainReport {
    BuildToolResolution cc;
    BuildToolResolution cxx;
    BuildToolResolution ld;
    BuildToolResolution ar;
    LinkerFamily        linker_family { LinkerFamily::Lld };
    String              linker_version;
};

enum class BuildOptionReportDomain
{
    Cpp,
    C,
    Link,
};

struct BuildOptionReport {
    BuildOptionReportDomain domain { BuildOptionReportDomain::Cpp };
    String                  source;
    Vec<String>             arguments;
};

struct BuildProfileValueReport {
    BuildOptionReportDomain domain { BuildOptionReportDomain::Cpp };
    String                  field;
    String                  value;
    String                  source;
};

struct BuildTargetReport {
    String          host;
    String          effective;
    Option<String>  android_abi;
    Option<u32>     android_minimum_api;
    Option<String>  sdk_kind;
    Option<String>  sdk_version;
    Option<PathBuf> sysroot;
};

struct ScriptPackageReport {
    String  package;
    String  dependency;
    String  source_identity;
    String  require_name;
    String  supports;
    PathBuf entry;
    String  entry_digest;
};

struct BuildSetupReport {
    BuildToolchainReport         toolchain;
    BuildTargetReport            target;
    String                       profile;
    Vec<BuildProfileValueReport> profile_values;
    Vec<BuildOptionReport>       options;
    Vec<ScriptPackageReport>     script_packages;
};

struct BuildSetupReportSink {
    void* context {};
    void (*notify)(void*, const BuildSetupReport&) noexcept {};
};

} // namespace lito

namespace lito
{

auto emit_build_setup_report(const Option<BuildSetupReportSink>&              reporter,
                             const lito::config::ToolchainSpec&               requested,
                             const ClangToolchain&                            resolved,
                             const lito::config::ProjectBuildOptions&         options,
                             const lito::package::EffectiveLanguageStandards& standards,
                             lito::config::StandardLibraryRuntime       standard_library_runtime,
                             const lito::cpp::ProfileSpec&              profile,
                             const BuildPlatform&                       platform,
                             const lito::package::ResolvedPackageGraph& graph,
                             const Vec<String>&                         selected_packages) -> void {
    if (reporter.is_none() || reporter->notify == nullptr) return;
    auto report = BuildSetupReport {
        .toolchain =
            BuildToolchainReport {
                .cc  = BuildToolResolution { .requested  = requested.cc.clone(),
                                             .executable = PathBuf::from(resolved.cc_path()) },
                .cxx = BuildToolResolution { .requested  = requested.cxx.clone(),
                                             .executable = PathBuf::from(resolved.cxx_path()) },
                .ld  = BuildToolResolution { .requested  = requested.ld.clone(),
                                             .executable = PathBuf::from(resolved.ld_path()) },
                .ar  = BuildToolResolution { .requested  = requested.ar.clone(),
                                             .executable = PathBuf::from(resolved.ar_path()) },
                .linker_family  = resolved.linker_identity().family,
                .linker_version = resolved.linker_identity().version.clone(),
            },
        .target =
            BuildTargetReport {
                .host = rstd::format(
                    "{}-{}", platform.host.architecture.as_str(), platform.host.os.as_str()),
                .effective           = platform.effective_target.triple.clone(),
                .android_abi         = as<Clone>(platform.android_abi).clone(),
                .android_minimum_api = platform.android_minimum_api,
                .sdk_kind            = as<Clone>(platform.sdk_kind).clone(),
                .sdk_version         = as<Clone>(platform.sdk_version).clone(),
                .sysroot             = as<Clone>(platform.sysroot).clone(),
            },
        .profile = profile.name.clone(),
    };
    for (const auto& package : graph.packages) {
        auto selected = false;
        for (const auto& name : selected_packages) {
            if (package.manifest.name == name.as_str()) selected = true;
        }
        if (! selected) continue;
        for (const auto& dependency : package.dependencies) {
            if (! dependency.is_Script()) continue;
            const auto&                           script   = dependency.as_Script().value;
            const lito::package::ResolvedPackage* provider = nullptr;
            for (const auto& candidate : graph.packages) {
                if (candidate.manifest.name == script.name.as_str()) {
                    provider = rstd::addressof(candidate);
                    break;
                }
            }
            if (provider == nullptr) continue;
            auto digest = String::make();
            if (provider->embedded_source.is_some()) {
                for (const auto& entry : provider->embedded_source->entries()) {
                    if (entry.path().as_str() == "lib.lua"_str &&
                        entry.kind() == lito::source::SourceEntryKind::File) {
                        digest = rstd::crypto::sha256_hex(entry.contents());
                        break;
                    }
                }
            } else {
                auto contents = rstd::fs::read(
                    provider->manifest.root.join(PathBuf::from("lib.lua"_str).as_path()).as_path());
                if (contents.is_ok()) digest = rstd::crypto::sha256_hex(contents->as_slice());
            }
            auto supports = String::make();
            for (auto host : script.supports) {
                if (! supports.is_empty()) supports.push_ascii(',');
                supports.push_str(lito::manifest::script_host_kind_name(host));
            }
            report.script_packages.push(ScriptPackageReport {
                .package         = package.manifest.name.clone(),
                .dependency      = script.name.clone(),
                .source_identity = script.source_identity.clone(),
                .require_name    = script.require_name.clone(),
                .supports        = rstd::move(supports),
                .entry           = PathBuf::from("lib.lua"_str),
                .entry_digest    = rstd::move(digest),
            });
        }
    }
    const auto source_text = [](const Option<String>& source) {
        return source.is_some() ? source->clone() : String::make("compiler default"_str);
    };
    const auto append_codegen = [&](BuildOptionReportDomain                 domain,
                                    const lito::compiler::CodegenOptions&   options,
                                    const lito::cpp::CodegenSettingSources& sources,
                                    const Option<bool>&                     ndebug) {
        const auto append_value =
            [&](ref<str> field, ref<str> value, const Option<String>& source) {
                report.profile_values.push(BuildProfileValueReport {
                    .domain = domain,
                    .field  = String::make(field),
                    .value  = String::make(value),
                    .source = source_text(source),
                });
            };
        append_value("optimization"_str,
                     options.optimization.is_some()
                         ? lito::cpp::cpp_optimization_option(options.optimization)
                         : "compiler default"_str,
                     sources.optimization);
        append_value("debug info"_str,
                     options.debug_info.is_some() ? lito::cpp::cpp_debug_option(options.debug_info)
                                                  : "compiler default"_str,
                     sources.debug_info);
        append_value("LTO"_str,
                     options.lto.is_some() ? lito::cpp::cpp_lto_option(options.lto)
                                           : "compiler default"_str,
                     sources.lto);
        auto ndebug_value = "compiler default"_str;
        if (ndebug.is_some()) {
            ndebug_value = *ndebug ? "defined"_str : "undefined"_str;
        } else if (sources.ndebug.is_some()) {
            ndebug_value = "absent"_str;
        }
        append_value("NDEBUG"_str, ndebug_value, sources.ndebug);
    };
    if (standards.cpp.is_some()) {
        append_codegen(BuildOptionReportDomain::Cpp,
                       profile.cpp.common.codegen,
                       profile.cpp_sources,
                       profile.cpp_ndebug);
        report.profile_values.push(BuildProfileValueReport {
            .domain = BuildOptionReportDomain::Cpp,
            .field  = String::make("exceptions"_str),
            .value = String::make(profile.cpp.language.exceptions ? "enabled"_str : "disabled"_str),
            .source = profile.cpp_language_sources.exceptions.clone(),
        });
        report.profile_values.push(BuildProfileValueReport {
            .domain = BuildOptionReportDomain::Cpp,
            .field  = String::make("RTTI"_str),
            .value  = String::make(profile.cpp.language.rtti ? "enabled"_str : "disabled"_str),
            .source = profile.cpp_language_sources.rtti.clone(),
        });
    }
    if (standards.c.is_some()) {
        append_codegen(BuildOptionReportDomain::C,
                       profile.c.common.codegen,
                       profile.c_sources,
                       profile.c_ndebug);
    }
    const auto append_microsoft_runtime =
        [&report](BuildOptionReportDomain                                domain,
                  const Option<lito::compiler::MicrosoftRuntimeLibrary>& runtime) {
            report.profile_values.push(BuildProfileValueReport {
                .domain = domain,
                .field  = String::make("Microsoft runtime"_str),
                .value =
                    runtime.is_some()
                        ? String::make(lito::compiler::microsoft_runtime_library_name(*runtime))
                        : String::make("not applicable"_str),
                .source = String::make("effective toolchain policy"_str),
            });
        };
    if (standards.cpp.is_some()) {
        append_microsoft_runtime(BuildOptionReportDomain::Cpp,
                                 profile.cpp.common.microsoft_runtime_library);
        report.profile_values.push(BuildProfileValueReport {
            .domain = BuildOptionReportDomain::Link,
            .field  = String::make("standard library"_str),
            .value =
                String::make(lito::config::standard_library_name(profile.cpp.abi.standard_library)),
            .source = String::make("toolchain.stdlib"_str),
        });
        report.profile_values.push(BuildProfileValueReport {
            .domain = BuildOptionReportDomain::Link,
            .field  = String::make("standard library runtime"_str),
            .value =
                String::make(lito::config::standard_library_runtime_name(standard_library_runtime)),
            .source = String::make("toolchain.stdlib-runtime"_str),
        });
    }
    if (standards.c.is_some()) {
        append_microsoft_runtime(BuildOptionReportDomain::C,
                                 profile.c.common.microsoft_runtime_library);
    }
    report.profile_values.push(BuildProfileValueReport {
        .domain = BuildOptionReportDomain::Link,
        .field  = String::make("LTO"_str),
        .value  = profile.link_lto.is_some()
                      ? String::make(lito::cpp::cpp_lto_option(profile.link_lto))
                      : String::make("per-language"_str),
        .source = profile.link_lto_source.is_some()
                      ? profile.link_lto_source->clone()
                      : String::make(standards.c.is_some() && standards.cpp.is_some()
                                         ? "C/C++ compile settings"_str
                                         : (standards.c.is_some() ? "C compile settings"_str
                                                                  : "C++ compile settings"_str)),
    });
    auto strip_value  = String::make("compiler default"_str);
    auto strip_source = Option<String> {};
    if (profile.linker_strip.is_some()) {
        strip_value  = String::make(*profile.linker_strip == lito::artifact::StripMode::Symbols
                                        ? "symbols"_str
                                        : "debuginfo"_str);
        strip_source = profile.linker_strip_source.clone();
    } else if (profile.strip_source.is_some()) {
        switch (profile.strip) {
        case lito::artifact::StripMode::None: strip_value = String::make("none"_str); break;
        case lito::artifact::StripMode::DebugInfo:
            strip_value = String::make("debuginfo"_str);
            break;
        case lito::artifact::StripMode::Symbols: strip_value = String::make("symbols"_str); break;
        }
        strip_source = profile.strip_source.clone();
    }
    report.profile_values.push(BuildProfileValueReport {
        .domain = BuildOptionReportDomain::Link,
        .field  = String::make("strip"_str),
        .value  = rstd::move(strip_value),
        .source = source_text(strip_source),
    });
    const auto append = [&report](BuildOptionReportDomain                    domain,
                                  const Vec<lito::config::BuildOptionInput>& inputs) {
        for (const auto& input : inputs) {
            report.options.push(BuildOptionReport {
                .domain    = domain,
                .source    = input.source.clone(),
                .arguments = input.arguments.clone(),
            });
        }
    };
    append(BuildOptionReportDomain::Cpp, options.cpp);
    append(BuildOptionReportDomain::C, options.c);
    append(BuildOptionReportDomain::Link, options.linker);
    reporter->notify(reporter->context, report);
}

} // namespace lito
