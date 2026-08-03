cmake_minimum_required(VERSION 3.28)

project(
  tenon
  VERSION 0.1.0
  DESCRIPTION "Module-first C++ builder"
  LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "tenon only supports clang++")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(
  "$<$<COMPILE_LANGUAGE:CXX>:-fmodules-reduced-bmi>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wno-gnu-statement-expression>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Wunknown-attributes>"
  "$<$<COMPILE_LANGUAGE:CXX>:-fsized-deallocation>")
add_link_options(
  "$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>"
  "$<$<LINK_LANGUAGE:CXX>:-nostdlib++>")

set(RSTD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(
  rstd
  GIT_REPOSITORY https://github.com/litocpp/rstd.git
  GIT_TAG 41e1a9dc7711ccd305ac795c012e0bd67253e4b9)

set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -stdlib=libc++")
FetchContent_MakeAvailable(rstd)

add_library(tenon.lib STATIC)
target_compile_features(tenon.lib PUBLIC cxx_std_20)
target_sources(
  tenon.lib
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/frontend/result.cppm
         src/frontend/lexical/token.cppm
         src/frontend/lexical/source.cppm
         src/frontend/lexical/error.cppm
         src/frontend/lexical/lexer.cppm
         src/frontend/lexical/mod.cppm
         src/frontend/preprocessor/traits.cppm
         src/frontend/preprocessor/macro.cppm
         src/frontend/preprocessor/expression.cppm
         src/frontend/preprocessor/session.cppm
         src/frontend/preprocessor/directive.cppm
         src/frontend/preprocessor/mod.cppm
         src/frontend/parser/module_dependency.cppm
         src/frontend/parser/mod.cppm
         src/frontend/service.cppm
         src/frontend/mod.cppm
         src/profiling.cppm
         src/model.cppm
         src/build_profile.cppm
         src/process.cppm
         src/manifest/locator.cppm
         src/manifest/schema.cppm
         src/manifest/mod.cppm
         src/config/schema.cppm
         src/config/mod.cppm
         src/modules/convention.cppm
         src/modules/graph.cppm
         src/modules/frontend.cppm
         src/modules/mod.cppm
         src/source_discovery.cppm
         src/workspace/member.cppm
         src/workspace/catalog.cppm
         src/workspace/mod.cppm
         src/package/plan.cppm
         src/package/adapter.cppm
         src/package/source.cppm
         src/package/resolver.cppm
         src/package/mod.cppm
         src/workspace_resolver.cppm
         src/lock_store.cppm
         src/project.cppm
         src/toolchain/command.cppm
         src/toolchain/clang_options.cppm
         src/toolchain/clang_format_options.cppm
         src/toolchain/clang_format.cppm
         src/toolchain/clang_preprocessor_environment.cppm
         src/toolchain/clang.cppm
         src/toolchain/mod.cppm
         src/build_layout.cppm
         src/cache/hash.cppm
         src/cache/mod.cppm
         src/frontend_analysis.cppm
         src/compile_test.cppm
         src/builder.cppm
         src/test_runner.cppm
         src/scanner.cppm
         src/formatter.cppm
         src/mod.cppm)
target_link_libraries(tenon.lib PUBLIC rstd.std rstd.json rstd.toml rstd.bench)

add_executable(tenon)
target_sources(
  tenon
  PRIVATE FILE_SET
          modules
          TYPE
          CXX_MODULES
          FILES
          src/executable/cli.cppm
          src/executable/timing.cppm
          src/main.cppm)
target_link_libraries(tenon PRIVATE tenon.lib rstd.argparse rstd::nostd)

include(CTest)

if(BUILD_TESTING)
  add_executable(tenon.profiling.test)
  target_sources(tenon.profiling.test PRIVATE test/profiling.cpp)
  target_link_libraries(tenon.profiling.test PRIVATE tenon.lib rstd::nostd)
  add_test(NAME tenon.profiling COMMAND tenon.profiling.test)
  add_executable(tenon.preprocessor.test)
  target_sources(tenon.preprocessor.test PRIVATE test/preprocessor/core.cpp)
  target_link_libraries(tenon.preprocessor.test PRIVATE tenon.lib rstd::nostd)
  add_test(NAME tenon.preprocessor.core COMMAND tenon.preprocessor.test)
  add_executable(tenon.clang-builtin-context.test)
  target_sources(tenon.clang-builtin-context.test
                 PRIVATE test/toolchain/clang_builtin_context.cpp)
  target_link_libraries(tenon.clang-builtin-context.test PRIVATE tenon.lib rstd::nostd)
  add_test(NAME tenon.clang-builtin-context COMMAND tenon.clang-builtin-context.test)
  add_test(
    NAME tenon.scan.native
    COMMAND tenon -C ${CMAKE_CURRENT_SOURCE_DIR}/test/preprocessor/native scan
            src/lib.cppm)
  set_tests_properties(
    tenon.scan.native
    PROPERTIES PASS_REGULAR_EXPRESSION
               "\"logical-name\": \"fixture.preprocessor.native:dependency\""
               FAIL_REGULAR_EXPRESSION
               "pragma_operator_failure;builtin_catalog_failure;native_builtin_failure")
  add_test(
    NAME tenon.scan.definitions
    COMMAND tenon -C ${CMAKE_CURRENT_SOURCE_DIR}/test/scan/definitions scan
            src/lib.cppm)
  set_tests_properties(
    tenon.scan.definitions
    PROPERTIES PASS_REGULAR_EXPRESSION
               "\"logical-name\": \"fixture.scan.definitions:defined\""
               FAIL_REGULAR_EXPRESSION
               "fixture.scan.definitions:missing;command_line_undef_failure")

  set(TENON_TEST_COMMAND_FIXTURE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/test-command/workspace)
  set(TENON_TEST_COMMAND_OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/test-command-build)
  set(TENON_TIMING_REPORT ${CMAKE_CURRENT_BINARY_DIR}/timing-report.txt)
  add_test(
    NAME tenon.test-command.build-production
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} build --out
            ${TENON_TEST_COMMAND_OUTPUT})
  set_tests_properties(
    tenon.test-command.build-production
    PROPERTIES PASS_REGULAR_EXPRESSION
               "1 archives, 1 executables, 0 tests")
  add_test(
    NAME tenon.timing.scan
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} build --out
            ${TENON_TEST_COMMAND_OUTPUT} --verbose)
  set_tests_properties(
    tenon.timing.scan
    PROPERTIES DEPENDS tenon.test-command.build-production
               PASS_REGULAR_EXPRESSION
               "timing summary;scan.orchestration;scan.environment;scan.preprocessor;scan.source;scan.total;build.compile;build.archive;build.link;sources: 2.*dropped: 0.*diagnostics: 0")
  add_test(
    NAME tenon.timing.file
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} build --out
            ${TENON_TEST_COMMAND_OUTPUT} --verbose --no-timing --timing-file
            ${TENON_TIMING_REPORT})
  set_tests_properties(
    tenon.timing.file
    PROPERTIES DEPENDS tenon.timing.scan FAIL_REGULAR_EXPRESSION
               "timing summary")
  add_test(NAME tenon.timing.file-contents COMMAND ${CMAKE_COMMAND} -E cat
                                                   ${TENON_TIMING_REPORT})
  set_tests_properties(
    tenon.timing.file-contents
    PROPERTIES DEPENDS tenon.timing.file PASS_REGULAR_EXPRESSION
               "frontend;toolchain;preprocessor;build timing;build.compile;build.archive;build.link;aggregate timing;median_us.*p95_us.*max_us;scan.preprocessor;slow preprocessor sources")
  set(TENON_ENVIRONMENT_TIMING_REPORT
      ${CMAKE_CURRENT_BINARY_DIR}/environment-timing-report.txt)
  add_test(
    NAME tenon.environment-cache
    COMMAND tenon -C ${CMAKE_CURRENT_SOURCE_DIR}/test/environment-cache build
            --out ${CMAKE_CURRENT_BINARY_DIR}/test/environment-cache --timing-file
            ${TENON_ENVIRONMENT_TIMING_REPORT})
  add_test(NAME tenon.environment-cache-report COMMAND ${CMAKE_COMMAND} -E cat
                                                       ${TENON_ENVIRONMENT_TIMING_REPORT})
  set_tests_properties(
    tenon.environment-cache-report
    PROPERTIES DEPENDS tenon.environment-cache PASS_REGULAR_EXPRESSION
               "preprocessor environment entries.*1;preprocessor environment queries.*1;preprocessor environment hits.*1")

  add_test(
    NAME tenon.scan-cache
    COMMAND ${CMAKE_COMMAND} -DTENON=$<TARGET_FILE:tenon>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/test/cache
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/test/scan-cache-fixtures
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test/scan-cache-build
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test/cache/scan_cache.cmake)
  add_test(
    NAME tenon.test-command.no-run
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} test --out
            ${TENON_TEST_COMMAND_OUTPUT} --no-run)
  set_tests_properties(
    tenon.test-command.no-run
    PROPERTIES DEPENDS tenon.test-command.build-production
               PASS_REGULAR_EXPRESSION "built 3 tests")
  add_test(
    NAME tenon.test-command.pass
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} test --package
            fixture-test-pass --out ${TENON_TEST_COMMAND_OUTPUT} --locked --
            expected-argument)
  set_tests_properties(
    tenon.test-command.pass
    PROPERTIES DEPENDS tenon.test-command.no-run
               PASS_REGULAR_EXPRESSION
               "fixture pass executed;test result: ok. 1 passed; 0 failed")
  add_test(
    NAME tenon.test-command.failure
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} test --out
            ${TENON_TEST_COMMAND_OUTPUT})
  set_tests_properties(
    tenon.test-command.failure
    PROPERTIES DEPENDS tenon.test-command.pass WILL_FAIL TRUE)
  add_test(
    NAME tenon.test-command.signal
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} test --package
            fixture-test-signal --out ${TENON_TEST_COMMAND_OUTPUT} --locked)
  set_tests_properties(
    tenon.test-command.signal
    PROPERTIES DEPENDS tenon.test-command.pass WILL_FAIL TRUE)
  add_test(
    NAME tenon.test-command.release-no-run
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} test --profile release
            --out ${TENON_TEST_COMMAND_OUTPUT}-release --no-run --locked)
  set_tests_properties(
    tenon.test-command.release-no-run
    PROPERTIES DEPENDS tenon.test-command.build-production
               PASS_REGULAR_EXPRESSION "built 3 tests.*release")
  add_test(
    NAME tenon.test-command.reject-production-selection
    COMMAND tenon -C ${TENON_TEST_COMMAND_FIXTURE} test --package
            fixture-test-app --out ${TENON_TEST_COMMAND_OUTPUT})
  set_tests_properties(
    tenon.test-command.reject-production-selection
    PROPERTIES WILL_FAIL TRUE)
  add_test(
    NAME tenon.test-command.reject-multiple-artifacts
    COMMAND tenon -C
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test-command/invalid-artifact build)
  set_tests_properties(
    tenon.test-command.reject-multiple-artifacts
    PROPERTIES WILL_FAIL TRUE)
  add_test(
    NAME tenon.test-command.reject-test-dependency
    COMMAND tenon -C
            ${CMAKE_CURRENT_SOURCE_DIR}/test/test-command/invalid-dependency build)
  set_tests_properties(
    tenon.test-command.reject-test-dependency
    PROPERTIES WILL_FAIL TRUE)

  set(TENON_COMPILE_TEST_FIXTURE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/compile-test/workspace)
  set(TENON_COMPILE_TEST_OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/compile-test-build)
  add_test(
    NAME tenon.compile-test.pass
    COMMAND tenon -C ${TENON_COMPILE_TEST_FIXTURE} test --package
            fixture-compile-pass --out ${TENON_COMPILE_TEST_OUTPUT} --verbose)
  set_tests_properties(
    tenon.compile-test.pass
    PROPERTIES PASS_REGULAR_EXPRESSION
               "test result: ok. 3 passed; 0 failed"
               FAIL_REGULAR_EXPRESSION "windows.cpp")
  add_test(
    NAME tenon.compile-test.reuse
    COMMAND tenon -C ${TENON_COMPILE_TEST_FIXTURE} test --package
            fixture-compile-pass --out ${TENON_COMPILE_TEST_OUTPUT} --verbose)
  set_tests_properties(
    tenon.compile-test.reuse
    PROPERTIES DEPENDS tenon.compile-test.pass
               PASS_REGULAR_EXPRESSION "1 compiled, 4 reused")
  add_test(
    NAME tenon.compile-test.diagnostic-mismatch
    COMMAND tenon -C ${TENON_COMPILE_TEST_FIXTURE} test --package
            fixture-compile-mismatch --out ${TENON_COMPILE_TEST_OUTPUT})
  set_tests_properties(
    tenon.compile-test.diagnostic-mismatch
    PROPERTIES WILL_FAIL TRUE)
  add_test(
    NAME tenon.target-condition.reject-explicit
    COMMAND tenon -C ${TENON_COMPILE_TEST_FIXTURE} build --package
            fixture-windows-only --out ${TENON_COMPILE_TEST_OUTPUT})
  set_tests_properties(
    tenon.target-condition.reject-explicit
    PROPERTIES WILL_FAIL TRUE)
endif()
