export module lito.frontend.result;

import rstd;
import lito.frontend.lexical;

using namespace rstd::prelude;

using Clone = rstd::clone::Clone;

export namespace lito::frontend
{

enum class DocumentationCommentKind
{
    Outer,
    Inner,
};

enum class DeclarationKind
{
    Module,
    Namespace,
    Record,
    Enum,
    Concept,
    Alias,
    Function,
    Variable,
    Field,
};

enum class DeclarationAccess
{
    Public,
    Protected,
    Private,
};

enum class DocumentationSeverity
{
    Warning,
    Error,
};

struct DocumentationSpan {
    rstd::path::PathBuf path;
    usize               begin_line {};
    usize               begin_column {};
    usize               end_line {};
    usize               end_column {};
};

struct DocumentationComment {
    DocumentationCommentKind kind { DocumentationCommentKind::Outer };
    String                   text;
    DocumentationSpan        span;
};

struct DeclarationOutline {
    DeclarationKind              kind { DeclarationKind::Variable };
    String                       name;
    String                       qualified_name;
    String                       namespace_name;
    String                       signature;
    bool                         is_definition { false };
    bool                         exported { false };
    DeclarationAccess            access { DeclarationAccess::Public };
    Option<usize>                parent;
    Option<String>               group;
    Option<DocumentationComment> comment;
    DocumentationSpan            span;
};

struct DocumentationReexport {
    String            logical_module;
    DocumentationSpan span;
};

struct DocumentationDiagnostic {
    DocumentationSeverity severity { DocumentationSeverity::Warning };
    String                code;
    String                message;
    DocumentationSpan     span;
};

struct DocumentationUnit {
    rstd::path::PathBuf           source;
    lexical::SharedSourceSnapshot source_snapshot;
    String                        logical_module;
    bool                          is_interface { false };
    Vec<DeclarationOutline>       declarations;
    Vec<DocumentationReexport>    reexports;
    Option<DocumentationComment>  module_comment;
    Vec<DocumentationDiagnostic>  diagnostics;
    usize                         documented {};
    usize                         undocumented {};
    usize                         unsupported {};
};

struct ProvidedModule : DefaultInClass<ProvidedModule, Clone> {
    String logical_name;
    bool   is_interface { false };

    auto clone() const -> ProvidedModule;
};

struct DependencyLocation : DefaultInClass<DependencyLocation, Clone> {
    rstd::path::PathBuf path;
    usize               line {};

    auto clone() const -> DependencyLocation;
};

struct ModuleImport : DefaultInClass<ModuleImport, Clone> {
    String             logical_name;
    DependencyLocation location;
    bool               exported { false };

    auto clone() const -> ModuleImport;
};

enum class IncludeLookupKind
{
    Quoted,
    Angled,
    NextQuoted,
    NextAngled,
};

struct ResolvedIncludeCandidate {
    rstd::path::PathBuf requested_path;
    rstd::path::PathBuf canonical_path;
    usize               search_index {};
};

struct IncludeLookupDependency {
    IncludeLookupKind                kind { IncludeLookupKind::Quoted };
    String                           name;
    rstd::path::PathBuf              including_path;
    Option<usize>                    previous_search_index;
    Vec<rstd::path::PathBuf>         missing_candidates;
    Option<ResolvedIncludeCandidate> resolved;
};

struct FrontendResult : DefaultInClass<FrontendResult, Clone> {
    rstd::path::PathBuf      source;
    Option<ProvidedModule>   provided;
    Option<String>           implementation_module;
    Vec<ModuleImport>        imports;
    Vec<rstd::path::PathBuf> header_inputs;
    String                   preprocessor_environment;
    usize                    input_bytes {};

    auto clone() const -> FrontendResult;
};

struct FrontendSnapshot : DefaultInClass<FrontendSnapshot, Clone> {
    rstd::path::PathBuf      source;
    Option<ProvidedModule>   provided;
    Option<String>           implementation_module;
    Vec<ModuleImport>        imports;
    Vec<rstd::path::PathBuf> header_inputs;
    String                   preprocessor_environment;
    usize                    input_bytes {};

    auto clone() const -> FrontendSnapshot;
};

struct UncachedFrontendAnalysis {
    FrontendResult               result;
    Vec<IncludeLookupDependency> include_lookups;
};

enum class FrontendAnalysisOrigin
{
    Native,
    PersistentCache,
    Uncacheable,
};

struct FrontendAnalysis : DefaultInClass<FrontendAnalysis, Clone> {
    FrontendResult         result;
    String                 context_identity;
    String                 receipt;
    FrontendAnalysisOrigin origin { FrontendAnalysisOrigin::Native };

    auto clone() const -> FrontendAnalysis;
};

auto ProvidedModule::clone() const -> ProvidedModule {
    return ProvidedModule {
        .logical_name = logical_name.clone(),
        .is_interface = is_interface,
    };
}

auto DependencyLocation::clone() const -> DependencyLocation {
    return DependencyLocation {
        .path = path.clone(),
        .line = line,
    };
}

auto ModuleImport::clone() const -> ModuleImport {
    return ModuleImport {
        .logical_name = logical_name.clone(),
        .location     = as<Clone>(location).clone(),
        .exported     = exported,
    };
}

auto FrontendResult::clone() const -> FrontendResult {
    return FrontendResult {
        .source                   = source.clone(),
        .provided                 = as<Clone>(provided).clone(),
        .implementation_module    = as<Clone>(implementation_module).clone(),
        .imports                  = as<Clone>(imports).clone(),
        .header_inputs            = as<Clone>(header_inputs).clone(),
        .preprocessor_environment = preprocessor_environment.clone(),
        .input_bytes              = input_bytes,
    };
}

auto FrontendSnapshot::clone() const -> FrontendSnapshot {
    return FrontendSnapshot {
        .source                   = source.clone(),
        .provided                 = as<Clone>(provided).clone(),
        .implementation_module    = as<Clone>(implementation_module).clone(),
        .imports                  = as<Clone>(imports).clone(),
        .header_inputs            = as<Clone>(header_inputs).clone(),
        .preprocessor_environment = preprocessor_environment.clone(),
        .input_bytes              = input_bytes,
    };
}

auto snapshot(const FrontendResult& result) -> FrontendSnapshot {
    return FrontendSnapshot {
        .source                   = result.source.clone(),
        .provided                 = as<Clone>(result.provided).clone(),
        .implementation_module    = as<Clone>(result.implementation_module).clone(),
        .imports                  = as<Clone>(result.imports).clone(),
        .header_inputs            = as<Clone>(result.header_inputs).clone(),
        .preprocessor_environment = result.preprocessor_environment.clone(),
        .input_bytes              = result.input_bytes,
    };
}

auto restore(FrontendSnapshot value) -> Option<FrontendResult> {
    if (value.source.is_empty() || value.preprocessor_environment.is_empty()) return None();
    if (value.provided.is_some() && value.provided->logical_name.is_empty()) return None();
    if (value.implementation_module.is_some() && value.implementation_module->is_empty())
        return None();
    for (const auto& imported : value.imports) {
        if (imported.logical_name.is_empty() || imported.location.path.is_empty()) return None();
    }
    return Some(FrontendResult {
        .source                   = rstd::move(value.source),
        .provided                 = rstd::move(value.provided),
        .implementation_module    = rstd::move(value.implementation_module),
        .imports                  = rstd::move(value.imports),
        .header_inputs            = rstd::move(value.header_inputs),
        .preprocessor_environment = rstd::move(value.preprocessor_environment),
        .input_bytes              = value.input_bytes,
    });
}

auto validate(const IncludeLookupDependency& lookup) -> rstd::Result<bool, String> {
    for (const auto& candidate : lookup.missing_candidates) {
        auto exists = rstd::fs::exists(candidate.as_path());
        if (exists.is_err()) {
            return Err(rstd::format("cannot inspect include candidate '{}': {}",
                                    candidate.as_path(),
                                    rstd::move(exists).unwrap_err()));
        }
        if (*exists) return Ok(false);
    }
    if (lookup.resolved.is_none()) return Ok(true);
    auto exists = rstd::fs::exists(lookup.resolved->requested_path.as_path());
    if (exists.is_err()) {
        return Err(rstd::format("cannot inspect include candidate '{}': {}",
                                lookup.resolved->requested_path.as_path(),
                                rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(false);
    auto canonical = rstd::fs::canonicalize(lookup.resolved->requested_path.as_path());
    if (canonical.is_err()) {
        return Err(rstd::format("cannot resolve include candidate '{}': {}",
                                lookup.resolved->requested_path.as_path(),
                                rstd::move(canonical).unwrap_err()));
    }
    return Ok(canonical->as_path() == lookup.resolved->canonical_path.as_path());
}

auto FrontendAnalysis::clone() const -> FrontendAnalysis {
    return FrontendAnalysis {
        .result           = as<Clone>(result).clone(),
        .context_identity = context_identity.clone(),
        .receipt          = receipt.clone(),
        .origin           = origin,
    };
}

} // namespace lito::frontend
