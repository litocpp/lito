module lito.driver:build.compile.units;

import rstd;
import lito.core;
import lito.cpp;
import :build.error;
import lito.toolchain;
import :build.layout;
import :build.compile.test;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

struct PreparedBuildUnits {
    Vec<Vec<cpp::UnitId>>         target_units;
    Vec<Box<cpp::CompileContext>> owned_contexts;
    Vec<cpp::PreparedUnit>        units;
    Vec<cpp::ScanResult>          scans;
};

auto align_build_units(PreparedBuildUnits& next, const PreparedBuildUnits& previous)
    -> BuildResult<empty> {
    auto order    = Vec<cpp::UnitId>::make();
    auto selected = Vec<bool>::with_capacity(next.units.len());
    for (const auto& unit : next.units) selected.emplace_back(false);
    for (const auto& unit : previous.units) {
        auto found = false;
        for (cpp::UnitId candidate {}; candidate < next.units.len(); ++candidate) {
            if (selected[candidate] ||
                next.units[candidate].unit.object.as_path() != unit.unit.object.as_path())
                continue;
            selected[candidate] = true;
            order.emplace_back(candidate);
            found = true;
            break;
        }
        if (! found)
            return Err(BuildError::Message(
                rstd::format("source transformation removed existing compile unit '{}'",
                             unit.unit.source.as_path())));
    }
    for (cpp::UnitId candidate {}; candidate < next.units.len(); ++candidate)
        if (! selected[candidate]) order.emplace_back(candidate);
    auto units = Vec<cpp::PreparedUnit>::with_capacity(order.len());
    auto scans = Vec<cpp::ScanResult>::with_capacity(order.len());
    for (auto& targets : next.target_units) targets.clear();
    for (auto prior : order) {
        auto id      = units.len();
        auto unit    = rstd::move(next.units[prior]);
        auto scan    = rstd::move(next.scans[prior]);
        unit.unit.id = id;
        scan.unit    = id;
        auto target  = cpp::project_target(unit.unit);
        if (target.is_some()) next.target_units[*target].emplace_back(id);
        units.push(rstd::move(unit));
        scans.push(rstd::move(scan));
    }
    next.units = rstd::move(units);
    next.scans = rstd::move(scans);
    return Ok(empty {});
}

auto append_build_units(PreparedBuildUnits&       result,
                        cpp::PackageSpec&         package,
                        const cpp::PackagePlan&   package_plan,
                        const Vec<cpp::TargetId>& targets,
                        const BuildLayout&        layout,
                        const ClangToolchain&     toolchain) -> BuildResult<empty> {
    if (result.target_units.is_empty()) {
        result.target_units.reserve(package.targets.len());
        for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
            result.target_units.emplace_back();
        }
    } else if (result.target_units.len() != package.targets.len()) {
        return Err(BuildError::Message("prepared build units do not match package targets"_Str));
    }
    for (auto target : targets) {
        if (target >= package.targets.len()) {
            return Err(
                BuildError::Message("build unit target selection does not match package"_Str));
        }
        auto& target_spec = package.targets[target];
        for (auto& source : target_spec.sources) {
            if (source.scan_artifact.is_none()) continue;
            const auto* compile_test = static_cast<const cpp::ResolvedCompileTestCase*>(nullptr);
            const auto* context      = rstd::addressof(package_plan.contexts[target]);
            if (target_spec.artifact_kind == cpp::ArtifactKind::CompileTest) {
                auto selected =
                    compile_test_for_source(target_spec, source.relative_path.as_path());
                if (selected.is_none()) {
                    return Err(BuildError::Message(
                        rstd::format("compile-test package '{}' has no case for source '{}'",
                                     target_spec.id.package.as_str(),
                                     source.relative_path.as_path())));
                }
                compile_test = *selected;
                auto selected_context =
                    compile_test_context(package_plan.contexts[target], *compile_test);
                if (selected_context.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(selected_context).unwrap_err()));
                }
                result.owned_contexts.push(
                    Box<cpp::CompileContext>::make(rstd::move(selected_context).unwrap()));
                context = result.owned_contexts[result.owned_contexts.len() - usize(1)].get();
            }
            auto object       = layout.object(target_spec.id, source.relative_path.as_path());
            auto cache_record = layout.cache_unit(target_spec.id, source.relative_path.as_path());
            if (object.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(object).unwrap_err()));
            }
            if (cache_record.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(cache_record).unwrap_err()));
            }
            auto compile_test_record = Option<PathBuf> {};
            if (compile_test != nullptr) {
                auto record =
                    layout.cache_compile_test(target_spec.id, source.relative_path.as_path());
                if (record.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(record).unwrap_err()));
                }
                compile_test_record = Some(rstd::move(record).unwrap());
            }

            auto artifact = source.scan_artifact.take().unwrap();
            if (artifact.context_identity.as_str() != context->scan_id.as_str()) {
                return Err(BuildError::Message(rstd::format(
                    "source '{}' scan context '{}' does not match compile context '{}'",
                    source.path.as_path(),
                    artifact.context_identity.as_str(),
                    context->scan_id.as_str())));
            }
            auto id       = result.units.len();
            auto prepared = toolchain.prepare(
                cpp::UnitSpec {
                    .id                     = id,
                    .owner                  = cpp::CompileUnitOwner::Project(target),
                    .relative_source        = source.relative_path.clone(),
                    .source_origin_identity = source.origin_identity.clone(),
                    .source                 = source.path.clone(),
                    .source_overlay         = source.transformed.is_some()
                                                  ? Some(source.transformed->overlay.clone())
                                                  : Option<PathBuf> {},
                    .object                 = rstd::move(object).unwrap(),
                    .cache_record           = rstd::move(cache_record).unwrap(),
                    .compile_test_record    = rstd::move(compile_test_record),
                    .language         = context->language.is_C() ? cpp::LanguageSourceUnit::C()
                                                                 : cpp::LanguageSourceUnit::Cpp(),
                    .cpp_dialect      = source.cpp_dialect,
                    .context          = context,
                    .compile_metadata = rstd::addressof(target_spec.compile_metadata),
                    .compile_test     = compile_test,
                    .standard_library_context_identity = context->id.clone(),
                },
                target_spec.source_root.as_path());
            if (prepared.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(prepared).unwrap_err()));
            }
            auto unit                    = rstd::move(prepared).unwrap();
            auto bound                   = cpp::bind_scan(rstd::move(artifact), id);
            unit.source_content_identity = rstd::move(bound.source_content_identity);
            result.units.push(rstd::move(unit));
            result.scans.push(rstd::move(bound.scan));
            result.target_units[target].emplace_back(id);
        }
    }
    return Ok(empty {});
}

auto prepare_build_units(cpp::PackageSpec&       package,
                         const cpp::PackagePlan& package_plan,
                         const BuildLayout&      layout,
                         const ClangToolchain&   toolchain) -> BuildResult<PreparedBuildUnits> {
    auto result = PreparedBuildUnits {
        .target_units   = Vec<Vec<cpp::UnitId>>::make(),
        .owned_contexts = Vec<Box<cpp::CompileContext>>::make(),
        .units          = Vec<cpp::PreparedUnit>::make(),
        .scans          = Vec<cpp::ScanResult>::make(),
    };
    auto appended = append_build_units(
        result, package, package_plan, package_plan.target_order, layout, toolchain);
    if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    return Ok(rstd::move(result));
}

} // namespace lito
