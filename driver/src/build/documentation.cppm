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

    auto retained_bytes() const noexcept -> usize {
        return logical_name.capacity() + artifact_identity.capacity() + path.capacity();
    }
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

    auto retained_bytes() const noexcept -> usize {
        auto result = target.package.capacity() + target.name.capacity() + package_root.capacity() +
                      source.capacity() + relative_source.capacity() + source_identity.capacity() +
                      invocation.retained_bytes() +
                      bmi_dependencies.capacity() * usize(sizeof(DocumentationBmiDependency));
        if (logical_module.is_some()) result += logical_module->capacity();
        if (root_module.is_some()) result += root_module->capacity();
        for (const auto& dependency : bmi_dependencies) result += dependency.retained_bytes();
        return result;
    }
};

auto documentation_retained_bytes(const Vec<DocumentationBuildUnit>& units) noexcept -> usize {
    auto result = units.capacity() * usize(sizeof(DocumentationBuildUnit));
    for (const auto& unit : units) result += unit.retained_bytes();
    return result;
}

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
