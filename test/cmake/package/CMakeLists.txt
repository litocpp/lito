cmake_minimum_required(VERSION 3.28)

project(TenonFixture VERSION 1.2.3 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(tenon_fixture STATIC src/fixture.cpp)
set_target_properties(tenon_fixture PROPERTIES EXPORT_NAME fixture)
target_compile_features(tenon_fixture PUBLIC cxx_std_20)
target_compile_definitions(tenon_fixture INTERFACE TENON_CMAKE_USAGE=1)
target_include_directories(tenon_fixture
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(TARGETS tenon_fixture EXPORT TenonFixtureTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT TenonFixtureTargets
  FILE TenonFixtureTargets.cmake
  NAMESPACE TenonFixture::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TenonFixture)

configure_package_config_file(
  cmake/TenonFixtureConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/TenonFixtureConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TenonFixture)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/TenonFixtureConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/TenonFixtureConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/TenonFixtureConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TenonFixture)
