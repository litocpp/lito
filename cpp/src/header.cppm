module;
#include <rstd/enum.hpp>

export module lito.cpp:header;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito::cpp
{

class HeaderOwner : public DefaultInClass<HeaderOwner, Clone> {
    RSTD_ENUM_DEFAULT(HeaderOwner,
                      (Unknown),
                      (Unknown),
                      (ProjectPackage, (String identity;)),
                      (ExternalTarget, (String identity;)),
                      (Toolchain, (String identity;)),
                      (Ambiguous, (Vec<String> identities;)))

public:
    auto clone() const -> HeaderOwner {
        RSTD_MATCH(*this) {
            RSTD_CASE(Unknown) {
                return Unknown();
            }
            RSTD_CASE(ProjectPackage, identity) {
                return ProjectPackage(identity.clone());
            }
            RSTD_CASE(ExternalTarget, identity) {
                return ExternalTarget(identity.clone());
            }
            RSTD_CASE(Toolchain, identity) {
                return Toolchain(identity.clone());
            }
            RSTD_CASE(Ambiguous, identities) {
                return Ambiguous(as<Clone>(identities).clone());
            }
        }
        rstd::unreachable();
    }

    auto retained_bytes() const noexcept -> usize {
        if (is_ProjectPackage()) return as_ProjectPackage().identity.capacity();
        if (is_ExternalTarget()) return as_ExternalTarget().identity.capacity();
        if (is_Toolchain()) return as_Toolchain().identity.capacity();
        if (! is_Ambiguous()) return usize {};
        const auto& identities = as_Ambiguous().identities;
        auto        result     = identities.capacity() * usize(sizeof(String));
        for (const auto& identity : identities) result += identity.capacity();
        return result;
    }
};

class HeaderAccess : public DefaultInClass<HeaderAccess, Clone> {
    RSTD_ENUM_DEFAULT(HeaderAccess,
                      (Global),
                      (Global),
                      (Public),
                      (TargetPrivate, (lito::package::PackageTargetId target;)))

public:
    auto clone() const -> HeaderAccess {
        if (is_Global()) return Global();
        if (is_Public()) return Public();
        return TargetPrivate(as_TargetPrivate().target.clone());
    }

    auto retained_bytes() const noexcept -> usize {
        if (! is_TargetPrivate()) return usize {};
        const auto& target = as_TargetPrivate().target;
        return target.package.capacity() + target.name.capacity();
    }
};

enum class HeaderIncludeKind
{
    User,
    System,
};

struct ResolvedHeaderRoot : DefaultInClass<ResolvedHeaderRoot, Clone> {
    PathBuf           root;
    HeaderOwner       owner;
    HeaderAccess      access;
    HeaderIncludeKind kind { HeaderIncludeKind::User };
    String            provenance;

    auto clone() const -> ResolvedHeaderRoot {
        return ResolvedHeaderRoot {
            .root       = root.clone(),
            .owner      = as<Clone>(owner).clone(),
            .access     = as<Clone>(access).clone(),
            .kind       = kind,
            .provenance = provenance.clone(),
        };
    }
};

struct HeaderClassification : DefaultInClass<HeaderClassification, Clone> {
    HeaderOwner  owner;
    HeaderAccess access;

    auto clone() const -> HeaderClassification {
        return HeaderClassification {
            .owner  = as<Clone>(owner).clone(),
            .access = as<Clone>(access).clone(),
        };
    }

    auto retained_bytes() const noexcept -> usize {
        return owner.retained_bytes() + access.retained_bytes();
    }
};

auto header_owner_identity(const HeaderOwner& owner) noexcept -> Option<ref<str>> {
    if (owner.is_ProjectPackage()) return Some(owner.as_ProjectPackage().identity.as_str());
    if (owner.is_ExternalTarget()) return Some(owner.as_ExternalTarget().identity.as_str());
    if (owner.is_Toolchain()) return Some(owner.as_Toolchain().identity.as_str());
    return None();
}

auto header_target_retention_domain(const lito::package::PackageTargetId& target) -> String {
    return rstd::format("target:{}", lito::package::package_target_id_text(target).as_str());
}

auto header_retention_domain(const HeaderClassification& classification) -> Option<String> {
    if (classification.owner.is_Unknown() || classification.owner.is_Toolchain() ||
        classification.owner.is_Ambiguous() || classification.access.is_Global()) {
        return None();
    }
    if (classification.access.is_TargetPrivate()) {
        return Some(
            header_target_retention_domain(classification.access.as_TargetPrivate().target));
    }
    if (classification.owner.is_ProjectPackage()) {
        return Some(
            rstd::format("package:{}", classification.owner.as_ProjectPackage().identity.as_str()));
    }
    if (classification.owner.is_ExternalTarget()) {
        return Some(rstd::format("external:{}",
                                 classification.owner.as_ExternalTarget().identity.as_str()));
    }
    return None();
}

auto merge_header_classification(HeaderClassification&       retained,
                                 const HeaderClassification& incoming) -> void;

class HeaderOwnershipIndex {
public:
    static auto make(Vec<ResolvedHeaderRoot> roots) -> HeaderOwnershipIndex {
        return HeaderOwnershipIndex(rstd::move(roots));
    }

    auto roots() const noexcept -> const Vec<ResolvedHeaderRoot>& { return roots_; }

    auto classify(ref<rstd::path::Path> path) const -> HeaderClassification;

private:
    explicit HeaderOwnershipIndex(Vec<ResolvedHeaderRoot> roots): roots_(rstd::move(roots)) {}

    Vec<ResolvedHeaderRoot> roots_;
};

} // namespace lito::cpp

namespace lito::cpp
{

auto same_owner(const HeaderOwner& left, const HeaderOwner& right) noexcept -> bool {
    if (left.is_Unknown() || right.is_Unknown()) return left.is_Unknown() && right.is_Unknown();
    if (left.is_ProjectPackage() && right.is_ProjectPackage()) {
        return left.as_ProjectPackage().identity == right.as_ProjectPackage().identity.as_str();
    }
    if (left.is_ExternalTarget() && right.is_ExternalTarget()) {
        return left.as_ExternalTarget().identity == right.as_ExternalTarget().identity.as_str();
    }
    if (left.is_Toolchain() && right.is_Toolchain()) {
        return left.as_Toolchain().identity == right.as_Toolchain().identity.as_str();
    }
    return false;
}

auto append_owner_identity(Vec<String>& identities, const HeaderOwner& owner) -> void {
    if (owner.is_Ambiguous()) {
        for (const auto& identity : owner.as_Ambiguous().identities) {
            auto repeated = false;
            for (const auto& existing : identities) {
                if (existing == identity.as_str()) repeated = true;
            }
            if (! repeated) identities.push(identity.clone());
        }
        return;
    }
    auto identity = header_owner_identity(owner);
    if (identity.is_none()) return;
    for (const auto& existing : identities) {
        if (existing == *identity) return;
    }
    identities.push(String::make(*identity));
}

auto merge_header_classification(HeaderClassification&       retained,
                                 const HeaderClassification& incoming) -> void {
    if (! same_owner(retained.owner, incoming.owner)) {
        auto identities = Vec<String>::make();
        append_owner_identity(identities, retained.owner);
        append_owner_identity(identities, incoming.owner);
        retained.owner  = HeaderOwner::Ambiguous(rstd::move(identities));
        retained.access = HeaderAccess::Global();
        return;
    }
    if (retained.access.is_Global() || incoming.access.is_Global()) {
        retained.access = HeaderAccess::Global();
    } else if (retained.access.is_Public() || incoming.access.is_Public()) {
        retained.access = HeaderAccess::Public();
    } else if (retained.access.as_TargetPrivate().target !=
               incoming.access.as_TargetPrivate().target) {
        retained.access = HeaderAccess::Public();
    }
}

auto HeaderOwnershipIndex::classify(ref<rstd::path::Path> path) const -> HeaderClassification {
    const ResolvedHeaderRoot* selected        = nullptr;
    auto                      selected_length = usize {};
    auto                      classification  = HeaderClassification {};
    for (const auto& root : roots_) {
        if (path.strip_prefix(root.root.as_path()).is_none()) continue;
        auto length = root.root.as_path().as_os_str().as_encoded_bytes().len();
        if (selected == nullptr || length > selected_length) {
            selected        = rstd::addressof(root);
            selected_length = length;
            classification  = HeaderClassification {
                .owner  = as<Clone>(root.owner).clone(),
                .access = as<Clone>(root.access).clone(),
            };
            continue;
        }
        if (length != selected_length) continue;
        auto incoming = HeaderClassification {
            .owner  = as<Clone>(root.owner).clone(),
            .access = as<Clone>(root.access).clone(),
        };
        merge_header_classification(classification, incoming);
    }
    if (selected == nullptr) return HeaderClassification {};
    return classification;
}

} // namespace lito::cpp
