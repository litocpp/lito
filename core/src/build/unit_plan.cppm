export module lito.build.unit_plan;

import rstd;
import lito.error;
import lito.manifest.contract;
import lito.build.identity;
import lito.build.plan_contract;
import lito.package.target_contract;
import lito.package;
import lito.build.error_contract;
import lito.toolchain;
import lito.build.layout;
import lito.build.compile_test;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct PreparedBuildUnits {
    Vec<Vec<UnitId>>         target_units;
    Vec<Box<CompileContext>> owned_contexts;
    Vec<PreparedUnit>        units;
};

auto prepare_build_units(const PackageSpec&    package,
                         const PackagePlan&    package_plan,
                         const BuildLayout&    layout,
                         const ClangToolchain& toolchain) -> BuildResult<PreparedBuildUnits> {
    auto result = PreparedBuildUnits {
        .target_units   = Vec<Vec<UnitId>>::with_capacity(package.targets.len()),
        .owned_contexts = Vec<Box<CompileContext>>::make(),
        .units          = Vec<PreparedUnit>::make(),
    };
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        result.target_units.emplace_back();
    }
    for (auto target : package_plan.target_order) {
        const auto& target_spec = package.targets[target];
        for (const auto& source : target_spec.sources) {
            const auto* compile_test = static_cast<const CompileTestCase*>(nullptr);
            const auto* context      = rstd::addressof(package_plan.contexts[target]);
            if (target_spec.artifact_kind == ArtifactKind::CompileTest) {
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
                    Box<CompileContext>::make(rstd::move(selected_context).unwrap()));
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

            auto id       = result.units.len();
            auto prepared = toolchain.prepare(
                UnitSpec {
                    .id                  = id,
                    .target              = target,
                    .relative_source     = source.relative_path.clone(),
                    .source              = source.path.clone(),
                    .object              = rstd::move(object).unwrap(),
                    .cache_record        = rstd::move(cache_record).unwrap(),
                    .compile_test_record = rstd::move(compile_test_record),
                    .context             = context,
                    .compile_test        = compile_test,
                },
                target_spec.source_root.as_path());
            if (prepared.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(prepared).unwrap_err()));
            }
            auto unit = rstd::move(prepared).unwrap();
            if (source.frontend_analysis.is_some() &&
                source.frontend_analysis->context_identity.as_str() == context->scan_id.as_str()) {
                unit.frontend_analysis =
                    Some(as<rstd::clone::Clone>(*source.frontend_analysis).clone());
            }
            result.units.push(rstd::move(unit));
            result.target_units[target].emplace_back(id);
        }
    }
    return Ok(rstd::move(result));
}

} // namespace lito
