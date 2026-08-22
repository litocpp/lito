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
    Signature,
    Schema,
    ContextMismatch,
    CorruptCache,
    Rollback,
    Integrity,
};

struct RegistryIndexError {
    RegistryIndexErrorKind kind { RegistryIndexErrorKind::Network };
    RegistryPackageId      package;
    String                 message;
};

using RegistryIndexLoadResult = Result<VerifiedPackageIndex, RegistryIndexError>;

struct RegistryIndexProvider {
    void* context {};
    RegistryIndexLoadResult (*load)(void*, const RegistryPackageId&) noexcept {};
};

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

struct RegistryLockedPreference {
    RegistryPackageId package;
    SemanticVersion   version;
    ReleaseDigest     release;

    auto clone() const -> RegistryLockedPreference {
        return RegistryLockedPreference {
            .package = package.clone(),
            .version = version.clone(),
            .release = release.clone(),
        };
    }
};

struct RegistrySolverInput {
    Vec<RegistrySolverRequirement> roots;
    Vec<RegistryLockedPreference>  locked;
    Vec<RegistryPackageId>         development_packages;
};

struct RegistryConstraintTrace {
    String requirement;
    String source;
};

class RegistrySolverError {
    RSTD_ENUM(RegistrySolverError,
              (Provider, (RegistryIndexError error;)),
              (Incompatibility,
               (RegistryPackageId package; Vec<RegistryConstraintTrace> constraints;
                Vec<String>                                             candidates;)),
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

auto resolve_registry_tag(const VerifiedPackageIndex& index, ref<str> tag)
    -> RegistryValueResult<SemanticVersion>;

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
        auto cloned_constraints = Vec<PackageConstraint>::with_capacity(constraints.len());
        for (const auto& constraint : constraints) {
            cloned_constraints.push(constraint.clone());
        }
        return PackageState {
            .package          = package.clone(),
            .constraints      = rstd::move(cloned_constraints),
            .selected_release = selected_release,
        };
    }
};

struct CachedIndex {
    String               key;
    VerifiedPackageIndex index;
};

auto package_key(const RegistryPackageId& package) -> String {
    return registry_package_id_text(package);
}

auto same_package(const RegistryPackageId& left, const RegistryPackageId& right) -> bool {
    return left == right;
}

auto clone_states(const Vec<PackageState>& states) -> Vec<PackageState> {
    auto result = Vec<PackageState>::with_capacity(states.len());
    for (const auto& state : states) result.push(state.clone());
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

    auto ensure_index(const RegistryPackageId& package) -> RegistrySolverResult<usize> {
        auto existing = index_position(package);
        if (existing.is_some()) return Ok(*existing);
        if (provider_.load == nullptr) {
            return Err(RegistrySolverError::Provider(RegistryIndexError {
                .kind    = RegistryIndexErrorKind::Network,
                .package = package.clone(),
                .message = String::make("Registry index provider is not configured"_str),
            }));
        }
        auto loaded = provider_.load(provider_.context, package);
        if (loaded.is_err()) {
            return Err(RegistrySolverError::Provider(rstd::move(loaded).unwrap_err()));
        }
        auto index = rstd::move(loaded).unwrap();
        if (! same_package(index.package(), package)) {
            return Err(RegistrySolverError::Provider(RegistryIndexError {
                .kind    = RegistryIndexErrorKind::ContextMismatch,
                .package = package.clone(),
                .message = String::make("Registry index provider returned another package"_str),
            }));
        }
        auto position = indices_.len();
        indices_.push(CachedIndex {
            .key   = package_key(package),
            .index = rstd::move(index),
        });
        return Ok(position);
    }

    auto locked_preference(const RegistryPackageId& package) const
        -> Option<ref<RegistryLockedPreference>> {
        for (const auto& locked : input_.locked) {
            if (same_package(locked.package, package)) {
                return Some(ref<RegistryLockedPreference>::from_raw_parts(rstd::addressof(locked)));
            }
        }
        return None();
    }

    auto development_enabled(const RegistryPackageId& package) const -> bool {
        for (const auto& candidate : input_.development_packages) {
            if (same_package(candidate, package)) return true;
        }
        return false;
    }

    static auto satisfies(const PackageState& state, const RegistryReleaseProjection& release)
        -> bool {
        for (const auto& constraint : state.constraints) {
            if (! constraint.requirement.matches(release.version)) return false;
        }
        return true;
    }

    auto candidates(const PackageState& state, usize index_position)
        -> RegistrySolverResult<Vec<usize>> {
        const auto& index           = indices_[index_position].index;
        auto        result          = Vec<usize>::make();
        auto        locked          = locked_preference(state.package);
        auto        locked_position = Option<usize> {};
        if (locked.is_some()) {
            for (usize position {}; position < index.releases().len(); ++position) {
                const auto& release = index.releases()[position];
                if (! (release.version == (*locked)->version)) continue;
                if (! (release.release == (*locked)->release)) {
                    return Err(RegistrySolverError::Provider(RegistryIndexError {
                        .kind    = RegistryIndexErrorKind::Integrity,
                        .package = state.package.clone(),
                        .message =
                            rstd::format("locked version '{}' is bound to another release digest",
                                         release.version.text().as_str()),
                    }));
                }
                locked_position = Some(position);
                if (satisfies(state, release)) result.push(rstd::move(position));
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

    static auto state_position(const Vec<PackageState>& states, const RegistryPackageId& package)
        -> Option<usize> {
        for (usize index {}; index < states.len(); ++index) {
            if (same_package(states[index].package, package)) return Some(index);
        }
        return None();
    }

    static auto add_constraint(Vec<PackageState>&       states,
                               const RegistryPackageId& package,
                               VersionRequirement       requirement,
                               String                   source) -> RegistrySolverResult<empty> {
        auto position = state_position(states, package);
        if (position.is_none()) {
            if (states.len() >= usize(1024)) {
                return Err(RegistrySolverError::Limit(
                    String::make("Registry solve exceeds 1024 packages"_str)));
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
        auto candidates = Vec<String>::make();
        for (const auto& release : indices_[index_position].index.releases()) {
            candidates.push(release.version.text());
        }
        return RegistrySolverError::Incompatibility(
            state.package.clone(), rstd::move(constraints), rstd::move(candidates));
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
                    String::make("Registry solver reached an incomplete assignment"_str)));
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
                String::make("Registry solve recursion exceeds 1024 assignments"_str)));
        }
        rstd_try(validate_selected(states));

        auto selected_state          = Option<usize> {};
        auto selected_candidates     = Vec<usize>::make();
        auto selected_index_position = usize {};
        for (usize state_position {}; state_position < states.len(); ++state_position) {
            if (states[state_position].selected_release.is_some()) continue;
            auto index_position = rstd_try(ensure_index(states[state_position].package));
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
        for (auto release_position : selected_candidates) {
            auto branch                              = clone_states(states);
            branch[*selected_state].selected_release = Some(release_position);
            const auto& package                      = states[*selected_state].package;
            const auto& release =
                indices_[selected_index_position].index.releases()[release_position];
            auto appended = append_dependencies(branch, package, release);
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
            auto solved = search(rstd::move(branch), depth + usize(1));
            if (solved.is_ok()) return solved;
            failure = Some(rstd::move(solved).unwrap_err());
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
                String::make("Registry solve requires at least one root requirement"_str)));
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

auto lito::registry::resolve_registry_tag(const VerifiedPackageIndex& index, ref<str> tag)
    -> RegistryValueResult<SemanticVersion> {
    for (const auto& candidate : index.tags()) {
        if (candidate.name.as_str() == tag) return Ok(candidate.version.clone());
    }
    return registry_value_failure<SemanticVersion>(
        rstd::format("Registry package has no tag '{}'", tag));
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
