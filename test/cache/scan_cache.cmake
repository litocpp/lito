function(run_build root output report)
  execute_process(
    COMMAND "${TENON}" -C "${root}" build --out "${output}"
            --timing-file "${report}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR
            "tenon build failed for ${root}\n${standard_output}\n${standard_error}")
  endif()
endfunction()

function(require_metric report label expected)
  file(READ "${report}" contents)
  if(NOT contents MATCHES "${label}[ ]+${expected}")
    message(FATAL_ERROR
            "expected '${label}' to be ${expected} in ${report}\n${contents}")
  endif()
endfunction()

set(fixture "${WORK}/scan")
set(output "${OUTPUT}/scan")
set(record
    "${output}/tenon-cache/scans/fixture-scan-cache/src/lib.cppm.json")
file(REMOVE_RECURSE "${fixture}" "${output}")
file(COPY "${SOURCE}/scan/" DESTINATION "${fixture}")

run_build("${fixture}" "${output}" "${output}/cold.txt")
require_metric("${output}/cold.txt" "persistent scan misses" 1)
require_metric("${output}/cold.txt" "analyzed sources" 1)
require_metric("${output}/cold.txt" "scan miss refresh" 1)

run_build("${fixture}" "${output}" "${output}/warm.txt")
require_metric("${output}/warm.txt" "persistent scan hits" 1)
require_metric("${output}/warm.txt" "analyzed sources" 0)
require_metric("${output}/warm.txt" "build.compile \\|" 0)

file(REMOVE "${output}/obj/fixture-scan-cache/src/lib.cppm.o")
run_build("${fixture}" "${output}" "${output}/missing-object.txt")
require_metric("${output}/missing-object.txt" "persistent scan hits" 1)
require_metric("${output}/missing-object.txt" "analyzed sources" 0)
require_metric("${output}/missing-object.txt" "build.compile \\|" 1)

file(TOUCH "${fixture}/low/choice.hpp")
run_build("${fixture}" "${output}" "${output}/touch.txt")
require_metric("${output}/touch.txt" "persistent scan hits" 1)
require_metric("${output}/touch.txt" "analyzed sources" 0)

file(COPY_FILE "${fixture}/staged/optional.hpp"
     "${fixture}/high/optional.hpp")
run_build("${fixture}" "${output}" "${output}/optional.txt")
require_metric("${output}/optional.txt" "scan miss include lookup" 1)
require_metric("${output}/optional.txt" "analyzed sources" 1)
require_metric("${output}/optional.txt" "build.compile \\|" 1)

file(COPY_FILE "${fixture}/staged/choice.hpp"
     "${fixture}/high/choice.hpp")
run_build("${fixture}" "${output}" "${output}/priority.txt")
require_metric("${output}/priority.txt" "scan miss include lookup" 1)
require_metric("${output}/priority.txt" "analyzed sources" 1)
require_metric("${output}/priority.txt" "build.compile \\|" 1)

file(COPY_FILE "${fixture}/staged/choice-low.hpp"
     "${fixture}/low/choice.hpp")
run_build("${fixture}" "${output}" "${output}/header.txt")
require_metric("${output}/header.txt" "scan miss file dependency" 1)
require_metric("${output}/header.txt" "analyzed sources" 1)
require_metric("${output}/header.txt" "build.compile \\|" 1)

file(WRITE "${record}" "{\n")
run_build("${fixture}" "${output}" "${output}/corrupt.txt")
require_metric("${output}/corrupt.txt" "scan miss corrupt" 1)
require_metric("${output}/corrupt.txt" "analyzed sources" 1)
require_metric("${output}/corrupt.txt" "build.compile \\|" 0)

file(COPY_FILE "${fixture}/staged/lib.cppm" "${fixture}/src/lib.cppm")
run_build("${fixture}" "${output}" "${output}/source.txt")
require_metric("${output}/source.txt" "scan miss source" 1)
require_metric("${output}/source.txt" "analyzed sources" 1)
require_metric("${output}/source.txt" "build.compile \\|" 1)

file(READ "${record}" contents)
string(REGEX REPLACE "\"version\": [0-9]+" "\"version\": 999" contents
                     "${contents}")
file(WRITE "${record}" "${contents}")
run_build("${fixture}" "${output}" "${output}/version.txt")
require_metric("${output}/version.txt" "scan miss version" 1)
require_metric("${output}/version.txt" "analyzed sources" 1)
require_metric("${output}/version.txt" "build.compile \\|" 0)

set(dynamic_fixture "${WORK}/dynamic")
set(dynamic_output "${OUTPUT}/dynamic")
file(REMOVE_RECURSE "${dynamic_fixture}" "${dynamic_output}")
file(COPY "${SOURCE}/dynamic/" DESTINATION "${dynamic_fixture}")
run_build("${dynamic_fixture}" "${dynamic_output}"
          "${dynamic_output}/cold.txt")
run_build("${dynamic_fixture}" "${dynamic_output}"
          "${dynamic_output}/warm.txt")
require_metric("${dynamic_output}/warm.txt" "persistent scan hits" 0)
require_metric("${dynamic_output}/warm.txt" "persistent scan misses" 1)
require_metric("${dynamic_output}/warm.txt" "persistent scan uncacheable" 1)
require_metric("${dynamic_output}/warm.txt" "analyzed sources" 1)
if(EXISTS "${dynamic_output}/tenon-cache/scans/fixture-scan-cache-dynamic/src/lib.cppm.json")
  message(FATAL_ERROR "dynamic builtin analysis published a scan cache record")
endif()
