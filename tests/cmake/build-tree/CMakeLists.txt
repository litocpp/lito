cmake_minimum_required(VERSION 3.28)

project(LitoBuildTree VERSION 4.5.6 LANGUAGES CXX)

add_library(lito_build_tree STATIC src/fixture.cpp)
target_compile_features(lito_build_tree PUBLIC cxx_std_20)
target_compile_definitions(lito_build_tree INTERFACE LITO_CMAKE_BUILD_TREE_USAGE=1)
target_include_directories(lito_build_tree PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
