# EchoXR's tests. They are Windows programs: CI runs them on its Windows runner; elsewhere
# they build, and run under Wine if you point ctest at it.

# Pure decisions from xr/src/echoxr_policy.h.
add_executable(runtime_units runtime_units.cpp)
target_include_directories(runtime_units PRIVATE ${CMAKE_SOURCE_DIR}/xr/src)
set_target_properties(runtime_units PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
add_test(NAME runtime_units COMMAND runtime_units)

# The built runtime, with no OpenXR runtime to talk to: it must fail cleanly, not crash.
add_executable(smoke_no_runtime smoke_no_runtime.cpp)
add_dependencies(smoke_no_runtime echoxr_runtime)
target_include_directories(smoke_no_runtime PRIVATE ${ovr_sdk_SOURCE_DIR}/LibOVR/Include)
set_target_properties(smoke_no_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
add_test(NAME smoke_no_runtime COMMAND smoke_no_runtime $<TARGET_FILE:echoxr_runtime>)
set_tests_properties(smoke_no_runtime PROPERTIES
    ENVIRONMENT "XR_RUNTIME_JSON=${CMAKE_BINARY_DIR}/tests/no-such-runtime.json")

# EchoXR's Platform SDK stand-in, asked as the game's pnsovr.dll asks it.
add_executable(platform_signin platform_signin.cpp)
add_dependencies(platform_signin echoxr_platform)
target_compile_definitions(platform_signin PRIVATE UNICODE _UNICODE)
set_target_properties(platform_signin PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
add_test(NAME platform_signin COMMAND platform_signin $<TARGET_FILE:echoxr_platform>)
