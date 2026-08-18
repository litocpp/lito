export module lito.cpp:build.scan;

import rstd;
import lito.frontend;
import :build.unit;

using namespace rstd::prelude;

export namespace lito::cpp
{

struct RequiredModule {
    String logical_name;
    bool   exported { false };
};

struct ScanResult {
    UnitId                                      unit {};
    Option<frontend::ProvidedModule>            provided;
    Option<String>                              implementation_module;
    Vec<RequiredModule>                         required_modules;
    Vec<PathBuf>                                header_inputs;
    Vec<frontend::EmbeddedInput>                embedded_inputs;
    Vec<frontend::ExternalMacroMaterialization> external_macros;
    String                                      preprocessor_environment;
};

} // namespace lito::cpp
