if(NOT DEFINED FLEET_BUILD_DIR
   OR NOT DEFINED FLEET_LIBDIR
   OR NOT DEFINED FLEET_BINDIR)
  message(FATAL_ERROR "Fleet install test requires its build and library paths")
endif()

string(
  RANDOM
  LENGTH 16
  ALPHABET 0123456789abcdef suffix)
set(prefix "${FLEET_BUILD_DIR}/fleet-install-${suffix}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${FLEET_BUILD_DIR}" --prefix "${prefix}"
          --component keystone
  RESULT_VARIABLE installed
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT installed EQUAL 0)
  message(
    FATAL_ERROR "Runtime install failed: ${install_output}${install_error}")
endif()

file(GLOB dependencies "${prefix}/${FLEET_LIBDIR}/libnats*.so*"
     "${prefix}/${FLEET_LIBDIR}/libnats*.dylib")
if(NOT dependencies)
  message(
    FATAL_ERROR "Runtime component does not contain its shared NATS dependency")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH --unset=DYLD_LIBRARY_PATH
    --unset=DYLD_FALLBACK_LIBRARY_PATH
    "${prefix}/${FLEET_BINDIR}/keystone-fleet-gateway" --help
  WORKING_DIRECTORY "${prefix}"
  RESULT_VARIABLE started
  OUTPUT_VARIABLE help
  ERROR_VARIABLE start_error)
if(NOT started EQUAL 0 OR NOT help MATCHES "keystone-fleet-gateway")
  message(FATAL_ERROR "Installed gateway did not start: ${help}${start_error}")
endif()
file(REMOVE_RECURSE "${prefix}")
