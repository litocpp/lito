add_library(tenon.lib STATIC)
add_library(tenon.core ALIAS tenon.lib)
target_compile_features(tenon.lib PUBLIC cxx_std_20)
target_sources(
  tenon.lib
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/profiling.cppm
         src/cpp.cppm
         src/bmi.cppm
         src/model.cppm
         src/build_profile.cppm
         src/process.cppm
         src/manifest/locator.cppm
         src/manifest/schema.cppm
         src/manifest/mod.cppm
         src/config/schema.cppm
         src/config/mod.cppm
         src/modules/convention.cppm
         src/modules/graph.cppm
         src/modules/frontend.cppm
         src/modules/mod.cppm
         src/source_discovery.cppm
         src/workspace/member.cppm
         src/workspace/catalog.cppm
         src/workspace/mod.cppm
         src/package/plan.cppm
         src/package/adapter.cppm
         src/package/source.cppm
         src/package/resolver.cppm
         src/package/mod.cppm
         src/workspace_resolver.cppm
         src/lock_store.cppm
         src/project.cppm
         src/toolchain/command.cppm
         src/toolchain/clang_options.cppm
         src/toolchain/clang_format_options.cppm
         src/toolchain/clang_format.cppm
         src/toolchain/clang_preprocessor_environment.cppm
         src/toolchain/clang.cppm
         src/toolchain/mod.cppm
         src/build_layout.cppm
         src/cache/hash.cppm
         src/cache/mod.cppm
         src/frontend_analysis.cppm
         src/frontend_observer.cppm
         src/compile_test.cppm
         src/builder.cppm
         src/test_runner.cppm
         src/scanner.cppm
         src/formatter.cppm
         src/reporting.cppm
         src/documentation.cppm
         src/lib.cppm)
target_link_libraries(
  tenon.lib
  PUBLIC tenon.frontend rstd.std
  PRIVATE tenon.doc rstd.json rstd.toml rstd.bench)
