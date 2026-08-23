if(LITO_USE_SYSTEM_ZSTD)
  find_package(zstd CONFIG REQUIRED)
  if(NOT TARGET zstd::libzstd)
    message(FATAL_ERROR "The system zstd package does not provide zstd::libzstd")
  endif()

  add_library(lito.zstd INTERFACE)
  target_link_libraries(lito.zstd INTERFACE zstd::libzstd)
  return()
endif()

find_package(Threads REQUIRED)

set(LITO_ZSTD_LIBRARY_DIRECTORY "${zstd_SOURCE_DIR}/lib")
set(LITO_ZSTD_SOURCES
    common/debug.c
    common/entropy_common.c
    common/error_private.c
    common/fse_decompress.c
    common/pool.c
    common/threading.c
    common/xxhash.c
    common/zstd_common.c
    compress/fse_compress.c
    compress/hist.c
    compress/huf_compress.c
    compress/zstd_compress.c
    compress/zstd_compress_literals.c
    compress/zstd_compress_sequences.c
    compress/zstd_compress_superblock.c
    compress/zstd_double_fast.c
    compress/zstd_fast.c
    compress/zstd_lazy.c
    compress/zstd_ldm.c
    compress/zstd_opt.c
    compress/zstd_preSplit.c
    compress/zstdmt_compress.c
    decompress/huf_decompress.c
    decompress/zstd_ddict.c
    decompress/zstd_decompress.c
    decompress/zstd_decompress_block.c)
list(TRANSFORM LITO_ZSTD_SOURCES PREPEND "${LITO_ZSTD_LIBRARY_DIRECTORY}/")

add_library(lito.zstd STATIC ${LITO_ZSTD_SOURCES})
target_compile_features(lito.zstd PRIVATE c_std_17)
target_compile_definitions(
  lito.zstd
  PRIVATE ZSTD_MULTITHREAD ZSTD_LEGACY_SUPPORT=0 ZSTD_LIB_DICTBUILDER=0
          ZSTD_LIB_DEPRECATED=0 ZSTD_DISABLE_ASM)
target_include_directories(
  lito.zstd
  PUBLIC "${LITO_ZSTD_LIBRARY_DIRECTORY}"
  PRIVATE "${LITO_ZSTD_LIBRARY_DIRECTORY}/common"
          "${LITO_ZSTD_LIBRARY_DIRECTORY}/compress"
          "${LITO_ZSTD_LIBRARY_DIRECTORY}/decompress")
target_link_libraries(lito.zstd PRIVATE Threads::Threads)
