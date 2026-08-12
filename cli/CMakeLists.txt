add_executable(lito)
target_compile_features(lito PRIVATE cxx_std_20)
target_sources(
  lito
  PRIVATE FILE_SET
          modules
          TYPE
          CXX_MODULES
          FILES
          src/cli.cppm
          src/main.cppm)
target_link_libraries(lito PRIVATE lito.core rstd.argparse rstd::nostd)

install(TARGETS lito RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
