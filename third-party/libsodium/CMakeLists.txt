set(LITO_SODIUM_SOURCE_DIRECTORY "${sodium_SOURCE_DIR}/src/libsodium")
set(LITO_SODIUM_SOURCES
    crypto_hash/sha256/cp/hash_sha256_cp.c
    crypto_verify/verify.c)
list(TRANSFORM LITO_SODIUM_SOURCES PREPEND "${LITO_SODIUM_SOURCE_DIRECTORY}/")

add_library(lito.sodium STATIC ${LITO_SODIUM_SOURCES} support.c)
target_compile_features(lito.sodium PRIVATE c_std_17)
target_compile_definitions(
  lito.sodium
  PUBLIC SODIUM_STATIC
  PRIVATE CONFIGURED=1 HAVE_GCC_MEMORY_FENCES=1)
target_include_directories(
  lito.sodium
  PUBLIC "${LITO_SODIUM_SOURCE_DIRECTORY}/include"
  PRIVATE "${LITO_SODIUM_SOURCE_DIRECTORY}"
          "${LITO_SODIUM_SOURCE_DIRECTORY}/include/sodium")
