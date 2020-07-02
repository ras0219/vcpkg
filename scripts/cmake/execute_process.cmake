## # execute_process
##
## Intercepts all calls to execute_process() inside portfiles and fails when Download Mode
## is enabled. Additionally adds inter-cmake locking to enable parallel package builds without
## excessive machine oversubscription.
##
## In order to execute a process in Download Mode set `_EXECUTE_PROCESS_IN_DOWNLOAD_MODE` around the call to `execute_process()`.
##
## In order to execute a process concurrently with other package builds, set `_EXECUTE_PROCESS_IN_PARALLEL` around the call to `execute_process()`.
##
if (NOT DEFINED OVERRIDEN_EXECUTE_PROCESS)
  set(OVERRIDEN_EXECUTE_PROCESS ON)

  macro(execute_process)
    if (DEFINED VCPKG_DOWNLOAD_MODE AND NOT _EXECUTE_PROCESS_IN_DOWNLOAD_MODE)
      message(FATAL_ERROR "This command cannot be executed in Download Mode.\nHalting portfile execution.\n")
    endif()
    if (DEFINED _VCPKG_CONCURRENCY_LOCK AND NOT _EXECUTE_PROCESS_IN_PARALLEL)
      message("TAKING LOCK")
      file(LOCK "${_VCPKG_CONCURRENCY_LOCK}")
    endif()
    _execute_process(${ARGV})
    if (DEFINED _VCPKG_CONCURRENCY_LOCK AND NOT _EXECUTE_PROCESS_IN_PARALLEL)
      message("RELEASING LOCK")
      file(LOCK "${_VCPKG_CONCURRENCY_LOCK}" RELEASE)
    endif()
  endmacro()
endif()