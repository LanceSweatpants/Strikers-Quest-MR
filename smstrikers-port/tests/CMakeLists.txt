# The port's own tests: small, no Aurora, no game data, so they run in every configuration.
add_executable(chunk_bounds_test
    chunk_bounds_test.c
    ../src/platform/cam_endian.c
    ../src/platform/skin_endian.c
    ../src/platform/wld_endian.c
    ../src/platform/bmd_endian.c)
target_include_directories(chunk_bounds_test PRIVATE ../include)
add_test(NAME chunk_bounds COMMAND chunk_bounds_test)

add_executable(prompts_resolve_test
    prompts_resolve_test.cpp
    ../src/platform/prompts_resolve.cpp)
target_include_directories(prompts_resolve_test PRIVATE ../include)
add_test(NAME prompts_resolve COMMAND prompts_resolve_test)

# The two tool tests, pure Python, so they are one `ctest` away.
add_executable(guest_validation_test guest_validation_test.cpp)
target_include_directories(guest_validation_test PRIVATE ../include)
add_test(NAME guest_validation COMMAND guest_validation_test)

add_test(NAME tool_genstubs
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_genstubs.py")
add_test(NAME tool_extract_disc
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_extract_disc.py")
