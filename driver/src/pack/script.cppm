module;
#include <rstd/enum.hpp>

export module lito.driver:pack.script;

import rstd;
import lito.core;
import :command.error;

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
    String            name;
    String            tool;
    Vec<PackArgument> arguments;
    PathBuf           output;
};

auto load_pack_recipe(ref<rstd::path::Path> package_root,
                      ref<str>              package_name,
                      ref<str>              recipe_name) -> CommandResult<PackRecipe>;

} // namespace lito
