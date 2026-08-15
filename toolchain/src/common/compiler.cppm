export module lito.toolchain.common:compiler;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

struct CompilerIdentity {
    PathBuf path;
    String  version;
    String  target;
    PathBuf resource_directory;
    String  build_identity;
    u64     size {};
    i64     modified_seconds {};
    u32     modified_nanoseconds {};

    auto clone() const -> CompilerIdentity {
        return CompilerIdentity {
            .path                 = path.clone(),
            .version              = version.clone(),
            .target               = target.clone(),
            .resource_directory   = resource_directory.clone(),
            .build_identity       = build_identity.clone(),
            .size                 = size,
            .modified_seconds     = modified_seconds,
            .modified_nanoseconds = modified_nanoseconds,
        };
    }
};

} // namespace lito
