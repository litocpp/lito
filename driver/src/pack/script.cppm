module;
#include <rstd/enum.hpp>

export module lito.driver:pack.script;

import rstd;
import lito.core;
import lito.system;
import :command.error;
import :package.module_catalog;

using namespace rstd::prelude;

export namespace lito
{

class PackArgument {
    RSTD_ENUM(PackArgument,
              (Literal, (String value;)),
              (Artifact, (lito::package::PackageTargetId target;)),
              (AndroidAbi, ()),
              (AndroidMinApi, ()),
              (Output, ()))
};

struct PackRecipe {
    String                              name;
    String                              tool;
    Vec<PackArgument>                   arguments;
    PathBuf                             output;
    Vec<lito::package::PackageTargetId> inputs;
    bool                                lua_callback {};
};

struct PackCallbackArtifact {
    String  selector;
    PathBuf path;
};

struct PackCallbackContext {
    PathBuf                   package_root;
    PathBuf                   work_dir;
    PathBuf                   output;
    String                    target;
    String                    android_abi;
    u32                       android_min_api {};
    Vec<PackCallbackArtifact> artifacts;
};

class PackScriptSession {
public:
    PackScriptSession(PackScriptSession&&) noexcept;
    ~PackScriptSession();

    static auto load(ref<rstd::path::Path>                     package_root,
                     ref<str>                                  package_name,
                     ref<str>                                  recipe_name,
                     PackScriptDependencies                    dependencies,
                     const system::ResolvedProcessEnvironment& environment)
        -> CommandResult<PackScriptSession>;

    auto recipe() const -> const PackRecipe&;
    auto execute(const PackCallbackContext& context) -> CommandResult<empty>;

private:
    struct Impl;
    Box<Impl> impl_;

    explicit PackScriptSession(Box<Impl> implementation);
};

auto reserve_pack_directory(ref<rstd::path::Path> parent) -> CommandResult<PathBuf>;

} // namespace lito
