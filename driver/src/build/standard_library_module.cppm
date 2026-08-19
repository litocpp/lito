module;
#include <rstd/macro.hpp>

module lito.driver:build.standard_library_module;

import rstd;
import lito.core;
import lito.cpp;
import lito.toolchain;
import :build.error;
import :build.frontend_analysis;
import :build.layout;
import :build.unit_plan;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

struct StandardModuleRequest {
    String                     logical_name;
    String                     context_identity;
    const cpp::CompileContext* context {};
};

auto standard_module_failure(String message) -> BuildResult<empty> {
    return Err(BuildError::Message(rstd::move(message)));
}

auto standard_module_failure(ref<str> message) -> BuildResult<empty> {
    return standard_module_failure(String::make(message));
}

auto standard_module_failure(cpp::StandardLibraryError error) -> BuildResult<empty> {
    return Err(rstd::into<BuildError>(rstd::move(error)));
}

auto has_standard_module_unit(const Vec<cpp::PreparedUnit>& units,
                              ref<str>                      logical_name,
                              ref<str>                      context_identity) -> bool {
    for (const auto& unit : units) {
        auto module = cpp::standard_library_module(unit.unit);
        if (module.is_some() && (*module)->logical_name.as_str() == logical_name &&
            (*module)->context_identity.as_str() == context_identity) {
            return true;
        }
    }
    return false;
}

auto queued(const Vec<StandardModuleRequest>& requests,
            ref<str>                          logical_name,
            ref<str>                          context_identity) -> bool {
    for (const auto& request : requests) {
        if (request.logical_name.as_str() == logical_name &&
            request.context_identity.as_str() == context_identity) {
            return true;
        }
    }
    return false;
}

auto append_request(Vec<StandardModuleRequest>&              requests,
                    ref<str>                                 logical_name,
                    const cpp::PreparedUnit&                 importer,
                    const cpp::StandardLibraryModuleCatalog* catalog) -> BuildResult<empty> {
    if (catalog != nullptr && catalog->get(logical_name).is_none()) {
        return standard_module_failure(
            rstd::format("selected {} module manifest '{}' does not provide '{}'",
                         cpp::standard_library_name(catalog->family),
                         catalog->manifest.as_path(),
                         logical_name));
    }
    if (importer.unit.context == nullptr || ! importer.unit.context->language.is_Cpp()) {
        return standard_module_failure(
            "standard library module importer has no C++ compile context"_str);
    }
    if (queued(requests, logical_name, importer.unit.standard_library_context_identity.as_str())) {
        return Ok(empty {});
    }
    requests.push(StandardModuleRequest {
        .logical_name     = String::make(logical_name),
        .context_identity = importer.unit.standard_library_context_identity.clone(),
        .context          = importer.unit.context,
    });
    return Ok(empty {});
}

auto append_unique_system_include(cpp::CppCompileOptions& options, ref<rstd::path::Path> path)
    -> void {
    for (const auto& include : options.preprocessor.include_directories) {
        if (include.kind == cpp::CppIncludeDirectoryKind::System &&
            include.path.as_path() == path) {
            return;
        }
    }
    options.preprocessor.include_directories.push(cpp::CppIncludeDirectory {
        .path = PathBuf::from(path),
        .kind = cpp::CppIncludeDirectoryKind::System,
    });
}

auto require_cxx23(const cpp::PreparedUnit& importer, ref<str> logical_name) -> BuildResult<empty> {
    const auto& standard = importer.unit.context->language.as_Cpp().options.language.standard;
    auto        parsed   = lito::manifest::parse_cpp_standard(standard.as_str());
    if (parsed.is_none() ||
        lito::manifest::cpp_standard_rank(*parsed) <
            lito::manifest::cpp_standard_rank(lito::manifest::CppStandard::Cpp23)) {
        return standard_module_failure(
            cpp::StandardLibraryError::LanguageStandard(importer.unit.source.clone(),
                                                        String::make(logical_name),
                                                        standard.clone(),
                                                        String::make("C++23"_str)));
    }
    return Ok(empty {});
}

} // namespace lito

namespace lito
{

auto prepare_standard_library_modules(PreparedBuildUnits&      prepared,
                                      Vec<cpp::ScanResult>&    scans,
                                      FrontendAnalysisService& analysis_service,
                                      const BuildLayout&       layout,
                                      const ClangToolchain&    toolchain) -> BuildResult<empty> {
    if (prepared.units.len() != scans.len()) {
        return standard_module_failure(
            "standard library module preparation received mismatched units and scans"_str);
    }
    auto requests = Vec<StandardModuleRequest>::make();
    for (auto unit = cpp::UnitId {}; unit < prepared.units.len(); ++unit) {
        const auto& source = prepared.units[unit];
        if (! source.unit.owner.is_Project() || ! scans[unit].language.is_Cpp()) continue;
        for (const auto& required : scans[unit].language.as_Cpp().facts.required_modules) {
            if (! cpp::is_standard_library_module_name(required.logical_name.as_str())) continue;
            rstd_try(require_cxx23(source, required.logical_name.as_str()));
            rstd_try(append_request(requests, required.logical_name.as_str(), source, nullptr));
        }
    }
    if (requests.is_empty()) return Ok(empty {});

    for (auto request_index = usize {}; request_index < requests.len(); ++request_index) {
        auto request = StandardModuleRequest {
            .logical_name     = requests[request_index].logical_name.clone(),
            .context_identity = requests[request_index].context_identity.clone(),
            .context          = requests[request_index].context,
        };
        if (has_standard_module_unit(
                prepared.units, request.logical_name.as_str(), request.context_identity.as_str())) {
            continue;
        }
        auto resolved_catalog =
            toolchain.resolve_standard_library_modules(request.context->language.as_Cpp().options);
        if (resolved_catalog.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(resolved_catalog).unwrap_err()));
        }
        auto catalog = rstd::move(resolved_catalog).unwrap();
        auto entry   = catalog.get(request.logical_name.as_str());
        if (entry.is_none()) {
            return standard_module_failure(cpp::StandardLibraryError::MissingProvider(
                catalog.manifest.clone(), request.logical_name.clone()));
        }
        auto  context     = request.context->clone();
        auto& cpp_context = context.language.as_Cpp();
        for (const auto& include : (*entry)->system_include_directories) {
            append_unique_system_include(cpp_context.options, include.as_path());
        }
        context.external_identities.push(rstd::format("standard-library-module-manifest:{}",
                                                      catalog.manifest_identity.as_str()));
        cpp::refresh_compile_context_identity(context);
        prepared.owned_contexts.push(Box<cpp::CompileContext>::make(rstd::move(context)));
        const auto* owned_context =
            prepared.owned_contexts[prepared.owned_contexts.len() - usize(1)].get();
        auto working = catalog.manifest.as_path().parent();
        if (working.is_none()) {
            return standard_module_failure(rstd::format(
                "standard library module manifest '{}' has no parent", catalog.manifest.as_path()));
        }
        auto object        = layout.standard_module_object(request.context_identity.as_str(),
                                                           request.logical_name.as_str());
        auto cache_record  = layout.cache_standard_module_unit(request.context_identity.as_str(),
                                                               request.logical_name.as_str());
        auto source_origin = rstd::format("standard-library-module-v1\nmanifest={}\nsource={}",
                                          catalog.manifest_identity.as_str(),
                                          (*entry)->source_identity.as_str());
        auto id            = prepared.units.len();
        auto unit          = toolchain.prepare(
            cpp::UnitSpec {
                .id    = id,
                .owner = cpp::CompileUnitOwner::StandardLibrary(cpp::StandardLibraryModuleUnit {
                    .logical_name      = request.logical_name.clone(),
                    .manifest_identity = catalog.manifest_identity.clone(),
                    .context_identity  = request.context_identity.clone(),
                }),
                .relative_source =
                    PathBuf::from(rstd::format("{}.cppm", request.logical_name.as_str())),
                .source_origin_identity            = source_origin.clone(),
                .source                            = (*entry)->source.clone(),
                .object                            = rstd::move(object),
                .cache_record                      = rstd::move(cache_record),
                .language                          = cpp::LanguageSourceUnit::Cpp(),
                .context                           = owned_context,
                .standard_library_context_identity = request.context_identity.clone(),
            },
            *working);
        if (unit.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(unit).unwrap_err()));
        }
        auto analysis = analysis_service.analyze_standard_module(request.logical_name.as_str(),
                                                                 request.context_identity.as_str(),
                                                                 source_origin.as_str(),
                                                                 (*entry)->source.as_path(),
                                                                 *owned_context,
                                                                 *working);
        if (analysis.is_err()) return Err(rstd::move(analysis).unwrap_err());
        unit->frontend_analysis = Some(rstd::move(analysis).unwrap());
        auto scan               = toolchain.scan(*unit);
        if (scan.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(scan).unwrap_err()));
        }
        auto provided     = Option<String> {};
        auto is_interface = false;
        if (scan->language.is_Cpp() && scan->language.as_Cpp().facts.provided.is_some()) {
            provided     = Some(scan->language.as_Cpp().facts.provided->logical_name.clone());
            is_interface = scan->language.as_Cpp().facts.provided->is_interface;
        }
        if (provided.is_none() || provided->as_str() != request.logical_name.as_str() ||
            ! is_interface) {
            return standard_module_failure(
                cpp::StandardLibraryError::ProviderMismatch(catalog.manifest.clone(),
                                                            (*entry)->source.clone(),
                                                            request.logical_name.clone(),
                                                            rstd::move(provided),
                                                            is_interface));
        }
        prepared.units.push(rstd::move(unit).unwrap());
        scans.push(rstd::move(scan).unwrap());
        const auto& facts = scans[scans.len() - usize(1)].language.as_Cpp().facts;
        for (const auto& required : facts.required_modules) {
            if (catalog.get(required.logical_name.as_str()).is_none()) {
                return standard_module_failure(
                    cpp::StandardLibraryError::UndeclaredDependency(catalog.manifest.clone(),
                                                                    (*entry)->source.clone(),
                                                                    request.logical_name.clone(),
                                                                    required.logical_name.clone()));
            }
            if (has_standard_module_unit(prepared.units,
                                         required.logical_name.as_str(),
                                         request.context_identity.as_str()) ||
                queued(
                    requests, required.logical_name.as_str(), request.context_identity.as_str())) {
                continue;
            }
            requests.push(StandardModuleRequest {
                .logical_name     = required.logical_name.clone(),
                .context_identity = request.context_identity.clone(),
                .context          = request.context,
            });
        }
    }
    return Ok(empty {});
}

} // namespace lito
