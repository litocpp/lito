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
  OUTPUT "${lito_builtin_directory}/packages.cpp"
  CONTENT
    "module lito.executable;\n\nimport rstd;\nimport lito.core;\nimport :builtin_packages;\n\nusing namespace rstd::prelude;\nusing namespace rstd::literals;\n\nnamespace\n{\n\nstruct PackageInput {\n    ref<str> id;\n    lito::registry::EmbeddedPackageInput input;\n};\n\ntemplate<rstd::size_t ArchiveSize, rstd::size_t DescriptorSize>\nauto package_input(ref<str> id,\n                   const unsigned char (&archive)[ArchiveSize],\n                   const unsigned char (&descriptor)[DescriptorSize]) -> PackageInput {\n    return PackageInput {\n        .id = id,\n        .input = lito::registry::EmbeddedPackageInput {\n            .archive = slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(archive), usize(ArchiveSize)),\n            .descriptor = slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(descriptor), usize(DescriptorSize)),\n        },\n    };\n}\n\n${lito_builtin_definitions}\nconst PackageInput PACKAGES[] = {\n${lito_builtin_entries}};\n\nauto resolve_package(void*, ref<str> id) noexcept -> Option<lito::registry::EmbeddedPackageInput> {\n    for (const auto& package : PACKAGES) {\n        if (package.id == id) {\n            return Some(lito::registry::EmbeddedPackageInput {\n                .archive = package.input.archive,\n                .descriptor = package.input.descriptor,\n            });\n        }\n    }\n    return None();\n}\n\n} // namespace\n\nauto lito_embedded_packages() noexcept -> lito::registry::EmbeddedPackageProvider {\n    return lito::registry::EmbeddedPackageProvider { .resolve = resolve_package };\n}\n"
)
target_sources(lito PRIVATE "${lito_builtin_directory}/packages.cpp")
add_custom_target(lito-builtin-packages DEPENDS ${lito_builtin_outputs})
add_dependencies(lito lito-builtin-packages)

install(TARGETS lito RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
