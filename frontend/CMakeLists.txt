add_library(lito.frontend STATIC)
target_compile_features(lito.frontend PUBLIC cxx_std_20)
target_sources(
  lito.frontend
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/memory.cppm
         src/static_name.cppm
         src/result.cppm
         src/lexical/symbol.cppm
         src/lexical/token.cppm
         src/lexical/traits.cppm
         src/lexical/source.cppm
         src/lexical/directive.cppm
         src/lexical/error.cppm
         src/lexical/scanner.cppm
         src/lexical/lexer.cppm
         src/lexical/mod.cppm
         src/format/style.cppm
         src/format/config.cppm
         src/format/model.cppm
         src/format/syntax.cppm
         src/format/lines.cppm
         src/format/alignment.cppm
         src/format/layout.cppm
         src/format/mod.cppm
         src/preprocessor/builtin.cppm
         src/preprocessor/traits.cppm
         src/preprocessor/macro.cppm
         src/preprocessor/expression.cppm
         src/preprocessor/session.cppm
         src/preprocessor/directive.cppm
         src/preprocessor/mod.cppm
         src/parser/attribute.cppm
         src/parser/module_dependency.cppm
         src/parser/module_name.cppm
         src/parser/header_dependency.cppm
         src/parser/mod.cppm
         src/service.cppm
         src/lib.cppm)
target_link_libraries(lito.frontend PUBLIC rstd.std lito.core)
target_link_libraries(lito.frontend PRIVATE rstd.parse)
