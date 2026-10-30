add_executable(lito)
target_compile_features(lito PRIVATE cxx_std_20)
target_sources(lito PRIVATE src/builtin_packages.cpp)
target_sources(
  lito
  PRIVATE FILE_SET
          modules
          TYPE
          CXX_MODULES
          FILES
          src/cli.cppm
          src/builtin_packages.cppm
          src/main.cppm)
target_link_libraries(
  lito PRIVATE lito.driver lito.pack liresto rstd.argparse rstd::nostd)
target_compile_definitions(lito PRIVATE "LITO_PKG_VERSION=\"${PROJECT_VERSION}\"")

set(lito_builtin_root "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
set(lito_builtin_directory "${lito_builtin_root}/builtin")
set(lito_builtin_outputs)

function(lito_embed_package id source)
  set(archive "${lito_builtin_directory}/${id}.tar.zst")
  set(descriptor "${lito_builtin_directory}/${id}.json")
  set(resource_source "${lito_builtin_directory}/${id}.cpp")
  set(blob "${lito_builtin_directory}/${id}.bin")
  set(depfile "${lito_builtin_directory}/${id}.d")
  add_custom_command(
    OUTPUT "${archive}" "${descriptor}" "${resource_source}" "${blob}" "${depfile}"
    COMMAND
      $<TARGET_FILE:lito-package-embed> --root "${source}" --registry
      https://registry.litocpp.org/ --archive "${archive}" --descriptor
      "${descriptor}" --source "${resource_source}" --blob "${blob}" --depfile
      "${depfile}" --module lito.executable --namespace lito::builtin --function
      "${id}_resources"
    DEPENDS lito-package-embed "${source}/lito.toml"
    DEPFILE "${depfile}"
    VERBATIM)
  target_sources(lito PRIVATE "${resource_source}")
  list(APPEND lito_builtin_outputs "${archive}" "${descriptor}" "${resource_source}"
       "${blob}" "${depfile}")
  set(lito_builtin_outputs "${lito_builtin_outputs}" PARENT_SCOPE)
endfunction()

lito_embed_package(pmacro "${PROJECT_SOURCE_DIR}/plugins/pmacro")
lito_embed_package(
  libxml2 "${PROJECT_SOURCE_DIR}/data/sdk/libxml2/2.13.8-minimal-elf-v1")
add_custom_target(lito-builtin-packages DEPENDS ${lito_builtin_outputs})
add_dependencies(lito lito-builtin-packages)

install(TARGETS lito RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
