export module lito.driver:build.documentation;

import rstd;
import lito.core;
import lito.toolchain.common;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class DocumentationUnitKind
{
    TranslationUnit,
    ModuleInterface,
    ModulePartition,
    ModuleImplementation,
};

struct DocumentationBmiDependency {
    String  logical_name;
    String  artifact_identity;
    PathBuf path;
};

struct DocumentationBuildUnit {
    lito::package::PackageTargetId  target;
    PathBuf                         package_root;
    PathBuf                         source;
    PathBuf                         relative_source;
    DocumentationUnitKind           kind { DocumentationUnitKind::TranslationUnit };
    bool                            is_interface { false };
    Option<String>                  logical_module;
    Option<String>                  root_module;
    String                          source_identity;
    CompileInvocation               invocation;
    Vec<DocumentationBmiDependency> bmi_dependencies;
};

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::DocumentationUnitKind> : ImplBase<lito::DocumentationUnitKind> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        switch (this->self()) {
        case lito::DocumentationUnitKind::TranslationUnit:
            return formatter.write_str("translation-unit"_str);
        case lito::DocumentationUnitKind::ModuleInterface:
            return formatter.write_str("module-interface"_str);
        case lito::DocumentationUnitKind::ModulePartition:
            return formatter.write_str("module-partition"_str);
        case lito::DocumentationUnitKind::ModuleImplementation:
            return formatter.write_str("module-implementation"_str);
        }
        rstd::unreachable();
    }
};

} // namespace rstd
