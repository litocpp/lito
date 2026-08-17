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
target_link_libraries(lito PRIVATE lito.driver rstd.argparse rstd::nostd)
target_compile_definitions(lito PRIVATE "LITO_PKG_VERSION=\"${PROJECT_VERSION}\"")

install(TARGETS lito RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
