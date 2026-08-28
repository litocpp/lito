add_library(lito.pack STATIC)
target_compile_features(lito.pack PUBLIC cxx_std_20)
target_sources(
  lito.pack
  PRIVATE src/archive/zstd.cpp src/archive/tar.cpp src/archive/tar_zstd.cpp)
target_sources(
  lito.pack
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/archive/error.cppm
         src/archive/entry.cppm
         src/archive/zstd.cppm
         src/archive/tar.cppm
         src/archive/tar_zstd.cppm
         src/blob.cppm
         src/inspection_protocol.cppm
         src/package.cppm
         src/registry/archive.cppm
         src/lib.cppm)
target_link_libraries(
  lito.pack
  PUBLIC lito.core rstd.std
  PRIVATE lito.zstd licrypto rstd.json)

add_executable(lito-package-embed EXCLUDE_FROM_ALL)
target_compile_features(lito-package-embed PRIVATE cxx_std_20)
target_sources(
  lito-package-embed
  PRIVATE FILE_SET
          modules
          TYPE
          CXX_MODULES
          FILES
          src/package_embed.cppm)
target_link_libraries(lito-package-embed PRIVATE lito.pack rstd::nostd)
