module;
#include <rstd/enum.hpp>

export module lito.cpp:build.unit;

import rstd;
import lito.frontend;
import :bmi.artifact;
import :package.spec;
import :package.target;
import :standard_library.model;

using namespace rstd::prelude;

export namespace lito::cpp
{

using UnitId = usize;

enum class CppCompileDisposition
{
    ObjectOnly,
    ObjectAndBmi,
    BmiOnly,
};

class LanguageSourceUnit {
    RSTD_ENUM_DEFAULT(LanguageSourceUnit, (Cpp), (C), (Cpp, (Option<BmiArtifact> bmi;)))
};

struct UnitSpec {
    UnitId                         id {};
    CompileUnitOwner               owner;
    PathBuf                        relative_source;
    String                         source_origin_identity;
    PathBuf                        source;
    PathBuf                        object;
    PathBuf                        cache_record;
    Option<PathBuf>                compile_test_record;
    LanguageSourceUnit             language;
    const CompileContext*          context {};
    const PackageCompileMetadata*  compile_metadata {};
    const ResolvedCompileTestCase* compile_test {};
    String                         standard_library_context_identity;
};

auto project_target(const UnitSpec& unit) noexcept -> Option<TargetId> {
    return unit.owner.is_Project() ? Some<TargetId>(unit.owner.as_Project().target) : None();
}

auto standard_library_module(const UnitSpec& unit) noexcept
    -> Option<ref<StandardLibraryModuleUnit>> {
    return unit.owner.is_StandardLibrary()
               ? Some(ref<StandardLibraryModuleUnit>::from_raw_parts(
                     rstd::addressof(unit.owner.as_StandardLibrary().module)))
               : None();
}

struct PreparedUnit {
    UnitSpec                           unit;
    PathBuf                            working_directory;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

constexpr auto source_language(const UnitSpec& unit) noexcept -> lito::manifest::PackageLanguage {
    return unit.language.is_C() ? lito::manifest::PackageLanguage::C
                                : lito::manifest::PackageLanguage::Cpp;
}

auto unit_bmi(const UnitSpec& unit) noexcept -> const BmiArtifact* {
    if (! unit.language.is_Cpp() || unit.language.as_Cpp().bmi.is_none()) return nullptr;
    return rstd::addressof(*unit.language.as_Cpp().bmi);
}

auto unit_bmi(UnitSpec& unit) noexcept -> BmiArtifact* {
    if (! unit.language.is_Cpp() || unit.language.as_Cpp().bmi.is_none()) return nullptr;
    return rstd::addressof(*unit.language.as_Cpp().bmi);
}

auto assign_unit_bmi(UnitSpec& unit, BmiArtifact artifact) -> bool {
    if (! unit.language.is_Cpp()) return false;
    unit.language.as_Cpp().bmi = Some(rstd::move(artifact));
    return true;
}

} // namespace lito::cpp
