add_library(lito.archive STATIC)
target_compile_features(lito.archive PUBLIC cxx_std_20)
target_sources(lito.archive PRIVATE src/zstd.cpp src/tar.cpp src/tar_zstd.cpp)
target_sources(
  lito.archive
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/error.cppm
         src/entry.cppm
         src/zstd.cppm
         src/tar.cppm
         src/tar_zstd.cppm
         src/lib.cppm)
target_link_libraries(lito.archive PUBLIC rstd.std PRIVATE lito.zstd)
