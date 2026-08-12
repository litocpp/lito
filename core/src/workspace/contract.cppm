export module lito.workspace.contract;

import rstd;
import lito.error;

using namespace rstd::literals;

export namespace lito
{

enum class PackageSelectionPurpose
{
    All,
    Production,
    Install,
    Test,
    Benchmark,
};

enum class ProjectRootRole
{
    PrimaryPackage,
    WorkspaceMember,
    AssociatedTest,
};

struct PackageSelection {
    PathBuf     root;
    Vec<String> packages;
};

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::PackageSelectionPurpose> : ImplBase<lito::PackageSelectionPurpose> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::PackageSelectionPurpose::All: name = "all"_str; break;
        case lito::PackageSelectionPurpose::Production: name = "production"_str; break;
        case lito::PackageSelectionPurpose::Install: name = "install"_str; break;
        case lito::PackageSelectionPurpose::Test: name = "test"_str; break;
        case lito::PackageSelectionPurpose::Benchmark: name = "benchmark"_str; break;
        }
        return formatter.write_str(name);
    }
};

template<>
struct Impl<fmt::Display, lito::ProjectRootRole> : ImplBase<lito::ProjectRootRole> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::ProjectRootRole::PrimaryPackage: name = "primary package"_str; break;
        case lito::ProjectRootRole::WorkspaceMember: name = "workspace member"_str; break;
        case lito::ProjectRootRole::AssociatedTest: name = "test"_str; break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd
