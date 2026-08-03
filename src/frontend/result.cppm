export module tenon.frontend.result;

import rstd;

using namespace rstd::prelude;

export namespace tenon::frontend {

struct ProvidedModule {
  String logical_name;
  bool is_interface{false};
};

struct DependencyLocation {
  rstd::path::PathBuf path;
  usize line{};
};

struct ModuleImport {
  String logical_name;
  DependencyLocation location;
};

enum class IncludeLookupKind {
  Quoted,
  Angled,
  NextQuoted,
  NextAngled,
};

struct ResolvedIncludeCandidate {
  rstd::path::PathBuf requested_path;
  rstd::path::PathBuf canonical_path;
  usize search_index{};
};

struct IncludeLookupDependency {
  IncludeLookupKind kind{IncludeLookupKind::Quoted};
  String name;
  rstd::path::PathBuf including_path;
  Option<usize> previous_search_index;
  Vec<rstd::path::PathBuf> missing_candidates;
  Option<ResolvedIncludeCandidate> resolved;
};

struct FrontendResult {
  rstd::path::PathBuf source;
  Option<ProvidedModule> provided;
  Option<String> implementation_module;
  Vec<ModuleImport> imports;
  Vec<rstd::path::PathBuf> header_inputs;
  String preprocessor_environment;
  usize input_bytes{};
};

struct FrontendSnapshot {
  rstd::path::PathBuf source;
  Option<ProvidedModule> provided;
  Option<String> implementation_module;
  Vec<ModuleImport> imports;
  Vec<rstd::path::PathBuf> header_inputs;
  String preprocessor_environment;
  usize input_bytes{};
};

struct UncachedFrontendAnalysis {
  FrontendResult result;
  Vec<IncludeLookupDependency> include_lookups;
};

enum class FrontendAnalysisOrigin {
  Native,
  PersistentCache,
  Uncacheable,
};

struct FrontendAnalysis {
  FrontendResult result;
  String context_identity;
  String receipt;
  FrontendAnalysisOrigin origin{FrontendAnalysisOrigin::Native};
};

auto clone_provided(const Option<ProvidedModule> &provided)
    -> Option<ProvidedModule> {
  if (provided.is_none())
    return None();
  return Some(ProvidedModule{
      .logical_name = provided->logical_name.clone(),
      .is_interface = provided->is_interface,
  });
}

auto clone_imports(const Vec<ModuleImport> &imports) -> Vec<ModuleImport> {
  auto result = Vec<ModuleImport>::with_capacity(imports.len());
  for (const auto &imported : imports) {
    result.push(ModuleImport{
        .logical_name = imported.logical_name.clone(),
        .location =
            DependencyLocation{
                .path = imported.location.path.clone(),
                .line = imported.location.line,
            },
    });
  }
  return result;
}

auto clone_paths(const Vec<rstd::path::PathBuf> &paths)
    -> Vec<rstd::path::PathBuf> {
  auto result = Vec<rstd::path::PathBuf>::with_capacity(paths.len());
  for (const auto &path : paths)
    result.push(path.clone());
  return result;
}

auto snapshot(const FrontendResult &result) -> FrontendSnapshot {
  return FrontendSnapshot{
      .source = result.source.clone(),
      .provided = clone_provided(result.provided),
      .implementation_module = result.implementation_module.is_some()
                                   ? Some(result.implementation_module->clone())
                                   : Option<String>{},
      .imports = clone_imports(result.imports),
      .header_inputs = clone_paths(result.header_inputs),
      .preprocessor_environment = result.preprocessor_environment.clone(),
      .input_bytes = result.input_bytes,
  };
}

auto restore(FrontendSnapshot value) -> Option<FrontendResult> {
  if (value.source.is_empty() || value.preprocessor_environment.is_empty())
    return None();
  if (value.provided.is_some() && value.provided->logical_name.is_empty())
    return None();
  if (value.implementation_module.is_some() &&
      value.implementation_module->is_empty())
    return None();
  for (const auto &imported : value.imports) {
    if (imported.logical_name.is_empty() || imported.location.path.is_empty())
      return None();
  }
  return Some(FrontendResult{
      .source = rstd::move(value.source),
      .provided = rstd::move(value.provided),
      .implementation_module = rstd::move(value.implementation_module),
      .imports = rstd::move(value.imports),
      .header_inputs = rstd::move(value.header_inputs),
      .preprocessor_environment = rstd::move(value.preprocessor_environment),
      .input_bytes = value.input_bytes,
  });
}

auto validate(const IncludeLookupDependency &lookup)
    -> rstd::Result<bool, String> {
  for (const auto &candidate : lookup.missing_candidates) {
    auto exists = rstd::fs::exists(candidate.as_path());
    if (exists.is_err()) {
      return Err(rstd::format("cannot inspect include candidate '{}': {}",
                              candidate.as_path(),
                              rstd::move(exists).unwrap_err()));
    }
    if (*exists)
      return Ok(false);
  }
  if (lookup.resolved.is_none())
    return Ok(true);
  auto exists = rstd::fs::exists(lookup.resolved->requested_path.as_path());
  if (exists.is_err()) {
    return Err(rstd::format("cannot inspect include candidate '{}': {}",
                            lookup.resolved->requested_path.as_path(),
                            rstd::move(exists).unwrap_err()));
  }
  if (!*exists)
    return Ok(false);
  auto canonical =
      rstd::fs::canonicalize(lookup.resolved->requested_path.as_path());
  if (canonical.is_err()) {
    return Err(rstd::format("cannot resolve include candidate '{}': {}",
                            lookup.resolved->requested_path.as_path(),
                            rstd::move(canonical).unwrap_err()));
  }
  return Ok(canonical->as_path() == lookup.resolved->canonical_path.as_path());
}

auto clone_analysis(const FrontendAnalysis &analysis) -> FrontendAnalysis {
  return FrontendAnalysis{
      .result =
          rstd::move(restore(snapshot(analysis.result))).unwrap_unchecked(),
      .context_identity = analysis.context_identity.clone(),
      .receipt = analysis.receipt.clone(),
      .origin = analysis.origin,
  };
}

} // namespace tenon::frontend
