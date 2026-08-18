export module lito.cpp:compiler.common;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito::compiler
{

struct TargetOptions {
    Option<String> target;
    Option<String> sysroot;

    auto clone() const -> TargetOptions {
        return TargetOptions {
            .target  = as<Clone>(target).clone(),
            .sysroot = as<Clone>(sysroot).clone(),
        };
    }
};

struct CodegenOptions {
    manifest::Optimization optimization { manifest::Optimization::Default };
    manifest::DebugInfo    debug_info { manifest::DebugInfo::None };
    manifest::Lto          lto { manifest::Lto::Off };
    bool                   position_independent_code { true };
};

struct CommonCompileOptions {
    TargetOptions  target;
    CodegenOptions codegen;
    bool           posix_threads { false };

    auto clone() const -> CommonCompileOptions {
        return CommonCompileOptions {
            .target        = target.clone(),
            .codegen       = codegen,
            .posix_threads = posix_threads,
        };
    }
};

} // namespace lito::compiler
