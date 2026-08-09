cmake_minimum_required(VERSION 3.28)

project(LitoFixture VERSION 1.2.3 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(DEFINED LITO_FIXTURE_CONFIGURE_COUNT)
  file(APPEND "${LITO_FIXTURE_CONFIGURE_COUNT}" "configure\n")
endif()

add_library(lito_fixture STATIC src/fixture.cpp)
set_target_properties(lito_fixture PROPERTIES EXPORT_NAME fixture)
target_compile_features(lito_fixture PUBLIC cxx_std_20)
target_compile_definitions(lito_fixture INTERFACE LITO_CMAKE_USAGE=1)
target_include_directories(lito_fixture
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(lito_fixture_headers INTERFACE)
set_target_properties(lito_fixture_headers PROPERTIES EXPORT_NAME headers)
target_compile_definitions(lito_fixture_headers INTERFACE LITO_CMAKE_HEADERS=1)

add_library(lito_fixture_order INTERFACE)
set_target_properties(lito_fixture_order PROPERTIES EXPORT_NAME order)
target_link_options(lito_fixture_order INTERFACE "LINKER:--as-needed")

install(TARGETS lito_fixture lito_fixture_headers lito_fixture_order
  EXPORT LitoFixtureTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT LitoFixtureTargets
  FILE LitoFixtureTargets.cmake
  NAMESPACE LitoFixture::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LitoFixture)

configure_package_config_file(
  cmake/LitoFixtureConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LitoFixture)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/LitoFixtureConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LitoFixture)
