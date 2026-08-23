add_library(lito.tools STATIC)
target_compile_features(lito.tools PUBLIC cxx_std_20)
target_sources(
  lito.tools
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/acquisition.cppm
         src/cargo/executor.cppm
         src/cargo/mod.cppm
         src/cargo/model.cppm
         src/cargo/request.cppm
         src/clang_format.cppm
         src/cmake/request.cppm
         src/cmake/executor.cppm
         src/cmake/file_api.cppm
         src/cmake/invocation.cppm
         src/cmake/mod.cppm
         src/cmake/model.cppm
         src/cmake/snapshot.cppm
         src/command.cppm
         src/error.cppm
         src/git.cppm
         src/lib.cppm
         src/model.cppm
         src/pkg_config.cppm
         src/resolver.cppm)
target_link_libraries(
  lito.tools
  PUBLIC lito.core rstd.std rstd.json)
