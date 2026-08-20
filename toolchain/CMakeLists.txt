add_library(lito.toolchain STATIC)
target_compile_features(lito.toolchain PUBLIC cxx_std_20)
target_sources(
  lito.toolchain
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/android/certification.cppm
         src/android/mod.cppm
         src/android/ndk.cppm
         src/android/sdk_catalog.cppm
         src/clang/arguments.cppm
         src/clang/compile_executor.cppm
         src/clang/mod.cppm
         src/clang/options.cppm
         src/clang/preprocessor_environment.cppm
         src/clang/preprocessor_model.cppm
         src/clang/preprocessor_probe.cppm
         src/clang/preprocessor_provider.cppm
         src/clang/preprocessor_query.cppm
         src/clang/sdk.cppm
         src/clang/sdk_catalog.cppm
         src/clang/support.cppm
         src/clang/strip.cppm
         src/clang/standard_library_module.cppm
         src/clang/toolchain.cppm
         src/common/command.cppm
         src/common/compiler.cppm
         src/common/error.cppm
         src/common/invocation.cppm
         src/common/link.cppm
         src/common/mod.cppm
         src/common/statistics.cppm
         src/lib.cppm)
target_link_libraries(
  lito.toolchain
  PUBLIC lito.core lito.cpp lito.frontend lito.tools rstd.std
  PRIVATE rstd.json)
