add_library(lito.frontend STATIC)
target_compile_features(lito.frontend PUBLIC cxx_std_20)
target_sources(
  lito.frontend
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/static_name.cppm
         src/result.cppm
         src/lexical/token.cppm
         src/lexical/traits.cppm
         src/lexical/source.cppm
         src/lexical/error.cppm
         src/lexical/lexer.cppm
         src/lexical/mod.cppm
         src/preprocessor/builtin.cppm
         src/preprocessor/traits.cppm
         src/preprocessor/macro.cppm
         src/preprocessor/expression.cppm
         src/preprocessor/session.cppm
         src/preprocessor/directive.cppm
         src/preprocessor/mod.cppm
         src/parser/module_dependency.cppm
         src/parser/documentation.cppm
         src/parser/mod.cppm
         src/service.cppm
         src/lib.cppm)
target_link_libraries(lito.frontend PUBLIC rstd.std)
