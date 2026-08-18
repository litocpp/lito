add_library(lito.cpp STATIC)
target_compile_features(lito.cpp PUBLIC cxx_std_20)
target_sources(
  lito.cpp
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/build/mod.cppm
         src/build/plan.cppm
         src/build/scan.cppm
         src/build/unit.cppm
         src/compiler/binding.cppm
         src/bmi/artifact.cppm
         src/bmi/compatibility.cppm
         src/bmi/mod.cppm
         src/compiler/argument.cppm
         src/compiler/error.cppm
         src/build/configuration.cppm
         src/compiler/option.cppm
         src/compiler/identity.cppm
         src/compiler/mod.cppm
         src/lib.cppm
         src/compiler/parser.cppm
         src/compiler/policy.cppm
         src/build/profile.cppm
         src/lexical/mod.cppm
         src/lexical/scanner.cppm
         src/source/mod.cppm
         src/lexical/token.cppm
         src/usage.cppm
         src/modules/convention.cppm
         src/modules/error.cppm
         src/modules/frontend.cppm
         src/modules/graph.cppm
         src/modules/mod.cppm
         src/package/adapter.cppm
         src/package/metadata.cppm
         src/package/mod.cppm
         src/package/plan.cppm
         src/package/spec.cppm
         src/package/target.cppm
         src/source/discovery.cppm)
target_link_libraries(lito.cpp PUBLIC lito.core lito.c lito.frontend rstd.std)
