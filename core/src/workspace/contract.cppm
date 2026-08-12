export module lito.workspace.contract;

import rstd;
import lito.error;

export namespace lito
{

enum class PackageSelectionPurpose
{
    All,
    Production,
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
