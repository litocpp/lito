module;
#include <rstd/enum.hpp>

export module lito.cpp:build.scan;

import rstd;
import lito.frontend;
import :build.unit;

using namespace rstd::prelude;

export namespace lito::cpp
{

struct RequiredModule {
    String                            logical_name;
    bool                              imported { false };
    bool                              implementation { false };
    Vec<frontend::DependencyLocation> import_locations;
    bool                              exported { false };
};

struct CommonScanResult {
    PathBuf                                     source;
    Vec<PathBuf>                                header_inputs;
    Vec<frontend::EmbeddedInput>                embedded_inputs;
    Vec<frontend::ExternalMacroMaterialization> external_macros;
    String                                      preprocessor_environment;
    usize                                       input_bytes {};
};

struct CScanResult {
    CommonScanResult common;
};

struct CppScanResult {
    CommonScanResult                 common;
    Option<frontend::ProvidedModule> provided;
    Option<String>                   implementation_module;
    Vec<RequiredModule>              required_modules;
};

class LanguageScanResult {
    RSTD_ENUM_DEFAULT(LanguageScanResult,
                      (Cpp),
                      (C, (CScanResult facts;)),
                      (Cpp, (CppScanResult facts;)))
};

struct ScanResult {
    UnitId             unit {};
    LanguageScanResult language;
};

auto scan_common(const ScanResult& result) noexcept -> const CommonScanResult& {
    return result.language.is_C() ? result.language.as_C().facts.common
                                  : result.language.as_Cpp().facts.common;
}

} // namespace lito::cpp
