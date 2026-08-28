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
          src/builtin_packages.cppm
          src/main.cppm)
target_link_libraries(lito PRIVATE lito.driver lito.pack rstd.argparse rstd::nostd)
target_compile_definitions(lito PRIVATE "LITO_PKG_VERSION=\"${PROJECT_VERSION}\"")

set(lito_builtin_root "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
set(lito_builtin_directory "${lito_builtin_root}/builtin")
set(lito_builtin_outputs)
set(lito_builtin_definitions)
set(lito_builtin_entries)

function(lito_embed_package id source)
  set(archive "${lito_builtin_directory}/${id}.tar.zst")
  set(descriptor "${lito_builtin_directory}/${id}.json")
  set(depfile "${lito_builtin_directory}/${id}.d")
  add_custom_command(
    OUTPUT "${archive}" "${descriptor}"
    COMMAND
      $<TARGET_FILE:lito-package-embed> --root "${source}" --registry
      https://registry.litocpp.org/ --archive "${archive}" --descriptor
      "${descriptor}" --depfile "${depfile}"
    DEPENDS lito-package-embed "${source}/lito.toml"
    DEPFILE "${depfile}"
    VERBATIM)
  string(MAKE_C_IDENTIFIER "${id}" symbol)
  string(TOUPPER "${symbol}" symbol)
  string(
    APPEND
    lito_builtin_definitions
    "static constexpr unsigned char LITO_BUILTIN_${symbol}_ARCHIVE[] = {\n"
    "#embed \"${archive}\"\n"
    "};\n"
    "static constexpr unsigned char LITO_BUILTIN_${symbol}_DESCRIPTOR[] = {\n"
    "#embed \"${descriptor}\"\n"
    "};\n")
  string(
    APPEND
    lito_builtin_entries
    "    package_input(\"${id}\"_str, LITO_BUILTIN_${symbol}_ARCHIVE, LITO_BUILTIN_${symbol}_DESCRIPTOR),\n"
  )
  list(APPEND lito_builtin_outputs "${archive}" "${descriptor}")
  set(lito_builtin_definitions "${lito_builtin_definitions}" PARENT_SCOPE)
  set(lito_builtin_entries "${lito_builtin_entries}" PARENT_SCOPE)
  set(lito_builtin_outputs "${lito_builtin_outputs}" PARENT_SCOPE)
endfunction()

lito_embed_package(pmacro "${PROJECT_SOURCE_DIR}/plugins/pmacro")
lito_embed_package(qt "${PROJECT_SOURCE_DIR}/data/script-packages/qt")
lito_embed_package(
  libxml2 "${PROJECT_SOURCE_DIR}/data/sdk/libxml2/2.13.8-minimal-elf-v1")
file(
  GENERATE
  OUTPUT "${lito_builtin_directory}/packages.inc"
  CONTENT
    "${lito_builtin_definitions}\nconst PackageInput PACKAGES[] = {\n${lito_builtin_entries}};\n"
)
add_custom_target(lito-builtin-packages DEPENDS ${lito_builtin_outputs})
add_dependencies(lito lito-builtin-packages)
target_include_directories(lito PRIVATE "${lito_builtin_root}")

install(TARGETS lito RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
