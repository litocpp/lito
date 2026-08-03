add_executable(tenon)
target_compile_features(tenon PRIVATE cxx_std_20)
target_sources(
  tenon
  PRIVATE FILE_SET
          modules
          TYPE
          CXX_MODULES
          FILES
          src/cli.cppm
          src/main.cppm)
target_link_libraries(tenon PRIVATE tenon.core rstd.argparse rstd::nostd)
