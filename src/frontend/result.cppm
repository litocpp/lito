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

struct FrontendResult {
  rstd::path::PathBuf source;
  Option<ProvidedModule> provided;
  Option<String> implementation_module;
  Vec<ModuleImport> imports;
  Vec<rstd::path::PathBuf> header_inputs;
  String preprocessor_environment;
  usize input_bytes{};
};

} // namespace tenon::frontend
