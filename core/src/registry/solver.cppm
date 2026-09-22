module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.core:registry.solver;

import rstd;
import :registry.digest;
import :registry.error;
import :registry.identity;
import :registry.metadata;
import :registry.version;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

enum class RegistryIndexErrorKind
{
    NotFound,
    Gone,
    LegalUnavailable,
    Network,
    OfflineCacheMiss,
    Schema,
    ContextMismatch,
    CorruptCache,
    Integrity,
    LockedVersionMissing,
};

struct RegistryIndexError {
    RegistryIndexErrorKind kind { RegistryIndexErrorKind::Network };
    RegistryPackageId      package;
    String                 message;
};

using RegistryIndexLoadResult = Result<RegistryPackageIndex, RegistryIndexError>;

struct RegistryIndexProvider {
    void* context {};
    RegistryIndexLoadResult (*load)(void*,
                                    const RegistryPackageId&,
                                    Option<ref<SemanticVersion>>) noexcept {};
};

inline auto require_index_version(const RegistryPackageIndex&  index,
                                  Option<ref<SemanticVersion>> version)
    -> Result<empty, RegistryIndexError> {
    if (version.is_none() || index.contains(**version)) return Ok(empty {});
    return Err(RegistryIndexError {
        .kind    = RegistryIndexErrorKind::LockedVersionMissing,
        .package = index.package().clone(),
        .message = rstd::format("Registry index for '{}' does not contain locked version '{}'",
                                registry_package_id_text(index.package()),
                                (**version).text()),
    });
}

struct RegistrySolverRequirement {
    RegistryPackageId  package;
    VersionRequirement requirement;
    String             source;

    auto clone() const -> RegistrySolverRequirement {
        return RegistrySolverRequirement {
            .package     = package.clone(),
            .requirement = requirement.clone(),
            .source      = source.clone(),
        };
    }
};

struct RegistryLockedConstraint {
    RegistryPackageId package;
    SemanticVersion   version;
    PackageChecksum   checksum;

    auto clone() const -> RegistryLockedConstraint {
        return RegistryLockedConstraint {
            .package  = package.clone(),
            .version  = version.clone(),
            .checksum = checksum.clone(),
        };
    }
};

struct RegistrySolverInput {
    Vec<RegistrySolverRequirement> roots;
    Vec<RegistryLockedConstraint>  locked;
    Vec<RegistryPackageId>         development_packages;
};

struct RegistryConstraintTrace {
    String requirement;
    String source;
};

class RegistrySolverError {
    RSTD_ENUM(RegistrySolverError,
              (Provider, (RegistryIndexError error;)),
              (SourceConflict,
               (RegistryPackageId selected; String selected_source; RegistryPackageId incoming;
                String incoming_source;)),
              (Incompatibility,
               (RegistryPackageId package; Vec<RegistryConstraintTrace> constraints;
                Vec<String>                                             candidates;
                bool                                                    locked_conflict;)),
              (Limit, (String message;)))
};

template<typename T>
using RegistrySolverResult = Result<T, RegistrySolverError>;

struct ResolvedRegistryPackage {
    RegistryPackageId         package;
    RegistryReleaseProjection release;
};

struct ResolvedRegistryGraph {
    Vec<ResolvedRegistryPackage> packages;
};

class RegistryVersionSolver {
public:
    static auto solve(const RegistrySolverInput& input, RegistryIndexProvider provider)
        -> RegistrySolverResult<ResolvedRegistryGraph>;
};

} // namespace lito::registry

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::registry::RegistrySolverError>
    : ImplBase<lito::registry::RegistrySolverError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::registry::RegistrySolverError>
    : ImplBase<lito::registry::RegistrySolverError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::registry::RegistrySolverError>
    : DefaultInImpl<error::Error, lito::registry::RegistrySolverError> {};

} // namespace rstd

namespace
{

using namespace lito::registry;

struct PackageConstraint {
    VersionRequirement requirement;
    String             source;

    auto clone() const -> PackageConstraint {
        return PackageConstraint {
            .requirement = requirement.clone(),
            .source      = source.clone(),
        };
    }
};

struct PackageState {
    RegistryPackageId      package;
    Vec<PackageConstraint> constraints;
    Option<usize>          selected_release;

    auto clone() const -> PackageState {
        auto cloned_constraints = constraints.iter()
                                      .map([](auto constraint) {
                                          return constraint->clone();
                                      })
                                      .collect<Vec<PackageConstraint>>();
        return PackageState {
            .package          = package.clone(),
            .constraints      = rstd::move(cloned_constraints),
            .selected_release = selected_release,
        };
    }
};

struct CachedIndex {
    String               key;
    RegistryPackageIndex index;
};

auto package_key(const RegistryPackageId& package) -> String {
    return registry_package_id_text(package);
}

auto same_package(const RegistryPackageId& left, const RegistryPackageId& right) -> bool {
    return left == right;
}

auto clone_states(const Vec<PackageState>& states) -> Vec<PackageState> {
    auto result = states.iter()
                      .map([](auto state) {
                          return state->clone();
                      })
                      .collect<Vec<PackageState>>();
    return result;
}

class Solver {
    const RegistrySolverInput& input_;
    RegistryIndexProvider      provider_;
    Vec<CachedIndex>           indices_;

    auto index_position(const RegistryPackageId& package) const -> Option<usize> {
        auto key = package_key(package);
        for (usize index {}; index < indices_.len(); ++index) {
            if (indices_[index].key == key) return Some(index);
        }
        return None();
    }

    auto ensure_index(const RegistryPackageId&     package,
                      Option<ref<SemanticVersion>> required = None())
        -> RegistrySolverResult<usize> {
        auto existing = index_position(package);
        if (existing.is_some() &&
            (required.is_none() || indices_[*existing].index.contains(**required)))
            return Ok(*existing);
        if (provider_.load == nullptr) {
            return Err(RegistrySolverError::Provider(RegistryIndexError {
                .kind    = RegistryIndexErrorKind::Network,
                .package = package.clone(),
                .message = "Registry index provider is not configured"_Str,
            }));
        }
        auto loaded = provider_.load(provider_.context, package, required);
        if (loaded.is_err()) {
            return Err(RegistrySolverError::Provider(rstd::move(loaded).unwrap_err()));
        }
        auto index = rstd::move(loaded).unwrap();
        if (! same_package(index.package(), package)) {
            return Err(RegistrySolverError::Provider(RegistryIndexError {
                .kind    = RegistryIndexErrorKind::ContextMismatch,
                .package = package.clone(),
                .message = "Registry index provider returned another package"_Str,
            }));
        }
        auto checked = require_index_version(index, required);
        if (checked.is_err())
            return Err(RegistrySolverError::Provider(rstd::move(checked).unwrap_err()));
        if (existing.is_some()) {
            indices_[*existing].index = rstd::move(index);
            return Ok(*existing);
        }
        auto position = indices_.len();
        indices_.push(CachedIndex {
            .key   = package_key(package),
            .index = rstd::move(index),
        });
        return Ok(position);
    }

    auto locked_constraint(const RegistryPackageId& package) const
        -> Option<ref<RegistryLockedConstraint>> {
        for (const auto& locked : input_.locked) {
            if (same_package(locked.package, package)) {
                return Some(ref<RegistryLockedConstraint>::from_raw_parts(rstd::addressof(locked)));
            }
        }
        return None();
    }

    auto development_enabled(const RegistryPackageId& package) const -> bool {
        return input_.development_packages.iter().any([&](auto candidate) {
            return same_package((*candidate), package);
        });
    }

    static auto satisfies(const PackageState& state, const RegistryReleaseProjection& release)
        -> bool {
        return state.constraints.iter().all([&](auto constraint) {
            return constraint->requirement.matches(release.version);
        });
    }

    auto applicable_lock(const PackageState& state) const -> Option<ref<SemanticVersion>> {
        auto locked = locked_constraint(state.package);
        if (locked.is_none()) return None();
        for (const auto& constraint : state.constraints) {
            if (! constraint.requirement.matches((*locked)->version)) return None();
        }
        return Some(ref<SemanticVersion>::from_raw_parts(rstd::addressof((*locked)->version)));
    }

    auto candidates(const PackageState& state, usize index_position, bool reuse_lock = true)
        -> RegistrySolverResult<Vec<usize>> {
        const auto& index           = indices_[index_position].index;
        auto        result          = Vec<usize>::make();
        auto        locked          = locked_constraint(state.package);
        auto        locked_position = Option<usize> {};
        if (locked.is_some()) {
            for (usize position {}; position < index.releases().len(); ++position) {
                const auto& release = index.releases()[position];
                if (! (release.version == (*locked)->version)) continue;
                if (! (release.checksum == (*locked)->checksum)) {
                    return Err(RegistrySolverError::Provider(RegistryIndexError {
                        .kind    = RegistryIndexErrorKind::Integrity,
                        .package = state.package.clone(),
                        .message = rstd::format("locked version '{}' is bound to another checksum",
                                                release.version.text().as_str()),
                    }));
                }
                locked_position = Some(position);
                if (reuse_lock && satisfies(state, release)) {
                    result.push(rstd::move(position));
                    return Ok(rstd::move(result));
                }
                break;
            }
        }
        for (usize position {}; position < index.releases().len(); ++position) {
            if (locked_position.is_some() && position == *locked_position) continue;
            const auto& release = index.releases()[position];
            if (release.yanked || ! satisfies(state, release)) continue;
            result.push(rstd::move(position));
        }
        return Ok(rstd::move(result));
    }

    static auto state_position(const Vec<PackageState>& states, const RegistryPackageName& name)
        -> Option<usize> {
        for (usize index {}; index < states.len(); ++index) {
            if (states[index].package.name == name) return Some(index);
        }
        return None();
    }

    static auto add_constraint(Vec<PackageState>&       states,
                               const RegistryPackageId& package,
                               VersionRequirement       requirement,
                               String                   source) -> RegistrySolverResult<empty> {
        auto position = state_position(states, package.name);
        if (position.is_none()) {
            if (states.len() >= usize(1024)) {
                return Err(RegistrySolverError::Limit("Registry solve exceeds 1024 packages"_Str));
            }
            auto constraints = Vec<PackageConstraint>::make();
            constraints.push(PackageConstraint {
                .requirement = rstd::move(requirement),
                .source      = rstd::move(source),
            });
            states.push(PackageState {
                .package     = package.clone(),
                .constraints = rstd::move(constraints),
            });
            return Ok(empty {});
        }
        if (! same_package(states[*position].package, package)) {
            return Err(RegistrySolverError::SourceConflict(
                states[*position].package.clone(),
                states[*position].constraints[usize {}].source.clone(),
                package.clone(),
                rstd::move(source)));
        }
        states[*position].constraints.push(PackageConstraint {
            .requirement = rstd::move(requirement),
            .source      = rstd::move(source),
        });
        return Ok(empty {});
    }

    auto incompatibility(const PackageState& state, usize index_position) -> RegistrySolverError {
        auto constraints = Vec<RegistryConstraintTrace>::with_capacity(state.constraints.len());
        for (const auto& constraint : state.constraints) {
            constraints.push(RegistryConstraintTrace {
                .requirement = String::make(constraint.requirement.text()),
                .source      = constraint.source.clone(),
            });
        }
        auto candidates = rstd::iter::from_slice(indices_[index_position].index.releases())
                              .map([](auto release) {
                                  return release->version.text();
                              })
                              .collect<Vec<String>>();
        return RegistrySolverError::Incompatibility(state.package.clone(),
                                                    rstd::move(constraints),
                                                    rstd::move(candidates),
                                                    locked_constraint(state.package).is_some() &&
                                                        applicable_lock(state).is_none());
    }

    auto validate_selected(const Vec<PackageState>& states) -> RegistrySolverResult<empty> {
        for (const auto& state : states) {
            if (state.selected_release.is_none()) continue;
            auto        index_position = rstd_try(ensure_index(state.package));
            const auto& release =
                indices_[index_position].index.releases()[*state.selected_release];
            if (! satisfies(state, release)) {
                return Err(incompatibility(state, index_position));
            }
        }
        return Ok(empty {});
    }

    auto append_dependencies(Vec<PackageState>&               states,
                             const RegistryPackageId&         package,
                             const RegistryReleaseProjection& release)
        -> RegistrySolverResult<empty> {
        for (const auto& dependency : release.dependencies) {
            if (dependency.kind == RegistryDependencyKind::Development &&
                ! development_enabled(package)) {
                continue;
            }
            auto source = rstd::format("{}@{} dependency '{}'",
                                       package_key(package).as_str(),
                                       release.version.text().as_str(),
                                       dependency.alias.as_str());
            rstd_try(add_constraint(
                states, dependency.package, dependency.requirement.clone(), rstd::move(source)));
        }
        return Ok(empty {});
    }

    auto complete_graph(const Vec<PackageState>& states)
        -> RegistrySolverResult<ResolvedRegistryGraph> {
        auto packages = Vec<ResolvedRegistryPackage>::with_capacity(states.len());
        for (const auto& state : states) {
            if (state.selected_release.is_none()) {
                return Err(RegistrySolverError::Limit(
                    "Registry solver reached an incomplete assignment"_Str));
            }
            auto index_position = rstd_try(ensure_index(state.package));
            packages.push(ResolvedRegistryPackage {
                .package = state.package.clone(),
                .release =
                    indices_[index_position].index.releases()[*state.selected_release].clone(),
            });
        }
        rstd::slice_::sort_unstable_by(
            packages.as_mut_slice().as_mut_ref(),
            [](const ResolvedRegistryPackage& left, const ResolvedRegistryPackage& right) {
                return package_key(left.package) < package_key(right.package);
            });
        return Ok(ResolvedRegistryGraph { .packages = rstd::move(packages) });
    }

    auto search(Vec<PackageState> states, usize depth)
        -> RegistrySolverResult<ResolvedRegistryGraph> {
        if (depth > usize(1024)) {
            return Err(RegistrySolverError::Limit(
                "Registry solve recursion exceeds 1024 assignments"_Str));
        }
        rstd_try(validate_selected(states));

        auto selected_state          = Option<usize> {};
        auto selected_candidates     = Vec<usize>::make();
        auto selected_index_position = usize {};
        for (usize state_position {}; state_position < states.len(); ++state_position) {
            if (states[state_position].selected_release.is_some()) continue;
            auto index_position = rstd_try(ensure_index(states[state_position].package,
                                                        applicable_lock(states[state_position])));
            auto available      = rstd_try(candidates(states[state_position], index_position));
            if (available.is_empty()) {
                return Err(incompatibility(states[state_position], index_position));
            }
            auto key          = package_key(states[state_position].package);
            auto selected_key = selected_state.is_some()
                                    ? package_key(states[*selected_state].package)
                                    : String::make();
            if (selected_state.is_none() || available.len() < selected_candidates.len() ||
                (available.len() == selected_candidates.len() && key < selected_key)) {
                selected_state          = Some(state_position);
                selected_candidates     = rstd::move(available);
                selected_index_position = index_position;
            }
        }
        if (selected_state.is_none()) return complete_graph(states);

        auto failure = Option<RegistrySolverError> {};
        for (usize candidate {}; candidate < selected_candidates.len(); ++candidate) {
            auto release_position                    = selected_candidates[candidate];
            auto branch                              = clone_states(states);
            branch[*selected_state].selected_release = Some(release_position);
            const auto& package                      = states[*selected_state].package;
            const auto& release =
                indices_[selected_index_position].index.releases()[release_position];
            auto appended = append_dependencies(branch, package, release);
            if (appended.is_err()) {
                auto error = rstd::move(appended).unwrap_err();
                if (error.is_Limit()) return Err(rstd::move(error));
                failure = Some(rstd::move(error));
                continue;
            }
            auto solved = search(rstd::move(branch), depth + usize(1));
            if (solved.is_ok()) return solved;
            auto error = rstd::move(solved).unwrap_err();
            if (error.is_Provider() || error.is_Limit()) return Err(rstd::move(error));
            auto locked = applicable_lock(states[*selected_state]);
            if (candidate == usize {} && locked.is_some() && error.is_Incompatibility() &&
                error.as_Incompatibility().locked_conflict) {
                const auto& conflict     = error.as_Incompatibility().package;
                const auto& dependencies = indices_[selected_index_position]
                                               .index.releases()[release_position]
                                               .dependencies;
                auto        affected     = same_package(conflict, package) ||
                                           dependencies.iter().any([&](auto dependency) {
                                    return same_package(dependency->package, conflict);
                                           });
                if (affected) {
                    // New constraints can invalidate a lock or one of its dependency edges.
                    auto alternatives = rstd_try(
                        candidates(states[*selected_state], selected_index_position, false));
                    for (auto alternative : alternatives)
                        selected_candidates.push(rstd::move(alternative));
                }
            }
            failure = Some(rstd::move(error));
        }
        if (failure.is_some()) return Err(rstd::move(*failure));
        return Err(incompatibility(states[*selected_state], selected_index_position));
    }

public:
    Solver(const RegistrySolverInput& input, RegistryIndexProvider provider)
        : input_(input), provider_(provider) {}

    auto solve() -> RegistrySolverResult<ResolvedRegistryGraph> {
        auto states = Vec<PackageState>::make();
        for (const auto& root : input_.roots) {
            rstd_try(add_constraint(
                states, root.package, root.requirement.clone(), root.source.clone()));
        }
        if (states.is_empty()) {
            return Err(RegistrySolverError::Limit(
                "Registry solve requires at least one root requirement"_Str));
        }
        return search(rstd::move(states), usize {});
    }
};

} // namespace

auto lito::registry::RegistryVersionSolver::solve(const RegistrySolverInput& input,
                                                  RegistryIndexProvider      provider)
    -> RegistrySolverResult<ResolvedRegistryGraph> {
    return Solver(input, provider).solve();
}

auto rstd::Impl<rstd::fmt::Display, lito::registry::RegistrySolverError>::fmt(
    rstd::fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_Provider()) {
        const auto& provider = error.as_Provider().error;
        auto        message =
            rstd::format("cannot load Registry index for '{}': {}",
                         lito::registry::registry_package_id_text(provider.package).as_str(),
                         provider.message.as_str());
        return formatter.write_str(message.as_str());
    }
    if (error.is_SourceConflict()) {
        const auto& conflict = error.as_SourceConflict();
        auto        message  = rstd::format(
            "Registry package '{}' is required from conflicting sources '{}' ({}) and '{}' ({})",
            conflict.selected.name.as_str(),
            conflict.selected.registry.as_str(),
            conflict.selected_source.as_str(),
            conflict.incoming.registry.as_str(),
            conflict.incoming_source.as_str());
        return formatter.write_str(message.as_str());
    }
    if (error.is_Limit()) return formatter.write_str(error.as_Limit().message.as_str());
    const auto& incompatibility = error.as_Incompatibility();
    auto        message =
        rstd::format("no Registry version of '{}' satisfies",
                     lito::registry::registry_package_id_text(incompatibility.package).as_str());
    for (const auto& constraint : incompatibility.constraints) {
        message.push_str(rstd::format(" {} from {};",
                                      constraint.requirement.as_str(),
                                      constraint.source.as_str())
                             .as_str());
    }
    return formatter.write_str(message.as_str());
}
