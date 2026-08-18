add_library(lito.c STATIC)
target_compile_features(lito.c PUBLIC cxx_std_20)
target_sources(
  lito.c
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/compiler.cppm
         src/lexical.cppm
         src/lib.cppm)
target_link_libraries(lito.c PUBLIC lito.core lito.frontend rstd.std)
