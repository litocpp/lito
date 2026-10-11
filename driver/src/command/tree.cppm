export module lito.driver:command.tree;

import rstd;
import lito.core;
import lito.tools;
import lito.system;
import :config.project;
import :config.registry;
import :package.selection;
import :command.error;
import :build.event;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

enum class TreeCharset
{
    Utf8,
    Ascii
};

struct TreeRenderOptions {
    Option<usize> depth;
    bool          no_dedupe { false };
    TreeCharset   charset { TreeCharset::Utf8 };
};

struct TreeRequest {
    package::PackageSelection           selection;
    package::PackageSelectionPurpose    purpose { package::PackageSelectionPurpose::Production };
    Option<manifest::BuildProfileName>  profile;
    config::BuildConfigurationRequest   configuration;
    ProcessEnvironmentSpec              environment;
    tools::ToolSpec                     tools;
    Option<config::LitoBootstrapConfig> registries;
    lock::LockConfig                    lock;
    source::PackageSourceConfig         sources;
    bool                                locked { false };
    Option<BuildEventSink>              observer;
    Option<lito::tools::HostToolResolutionSink> tool_reporter;
};

struct TreeReport {
    package::ResolvedPackageSelection selection;
    lock::LockStatus                  lock { lock::LockStatus::Unchanged };
};

auto tree_purpose_name(package::PackageSelectionPurpose purpose) noexcept -> ref<str>;
auto parse_tree_purpose(ref<str> name) -> CommandResult<package::PackageSelectionPurpose>;
auto parse_tree_charset(ref<str> name) -> CommandResult<TreeCharset>;
auto tree(const TreeRequest& request) -> CommandResult<TreeReport>;
auto render_tree(const TreeReport& report, const TreeRenderOptions& options = {})
    -> CommandResult<String>;

} // namespace lito
