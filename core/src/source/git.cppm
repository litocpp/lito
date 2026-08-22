export module lito.core:source.git;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::source
{

enum class GitReferenceKind
{
    DefaultBranch,
    Branch,
    Tag,
    Rev,
    Commit,
};

struct GitReference {
    GitReferenceKind kind { GitReferenceKind::DefaultBranch };
    String           value;

    auto clone() const -> GitReference {
        return GitReference { .kind = kind, .value = value.clone() };
    }
};

auto git_reference_kind_name(GitReferenceKind kind) noexcept -> ref<str> {
    switch (kind) {
    case GitReferenceKind::DefaultBranch: return "default"_str;
    case GitReferenceKind::Branch: return "branch"_str;
    case GitReferenceKind::Tag: return "tag"_str;
    case GitReferenceKind::Rev: return "rev"_str;
    case GitReferenceKind::Commit: return "commit"_str;
    }
    return "default"_str;
}

auto git_references_equal(const GitReference& left, const GitReference& right) noexcept -> bool {
    return left.kind == right.kind && left.value == right.value;
}

auto git_commit_is_valid(ref<str> value) noexcept -> bool {
    if (value.len() != usize(40)) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= '0' && ascii <= '9') || (ascii >= 'a' && ascii <= 'f') ||
               (ascii >= 'A' && ascii <= 'F'))) {
            return false;
        }
    }
    return true;
}

enum class GitResolutionMode
{
    ReuseLocked,
    Refresh,
};

struct GitSourcePin {
    String git;
    String commit;
};

} // namespace lito::source
