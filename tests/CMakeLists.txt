# tests/CMakeLists.txt — CTest target for SPSC ring buffer round-trip test.
# Links ring_buffer.c directly (no OBS dependency) via obs_shim.h macros.
# Note: cmake_minimum_required is NOT re-declared here — this is a subdirectory
# and the root CMakeLists.txt already declares cmake_minimum_required(VERSION 3.28...3.30).

add_executable(test_ring_spsc
    test_ring_spsc.c
    ${CMAKE_SOURCE_DIR}/src/ring_buffer.c
)

target_include_directories(test_ring_spsc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# C11 required for <stdatomic.h> in ring_buffer.c
set_property(TARGET test_ring_spsc PROPERTY C_STANDARD 11)
set_property(TARGET test_ring_spsc PROPERTY C_STANDARD_REQUIRED ON)

# MSVC requires explicit C17 + /experimental:c11atomics for <stdatomic.h> support.
if(MSVC)
    set_property(TARGET test_ring_spsc PROPERTY C_STANDARD 17)
    target_compile_options(test_ring_spsc PRIVATE /experimental:c11atomics)
endif()

# Link pthreads on POSIX platforms (not needed on Windows — test uses Win32 threads).
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(test_ring_spsc PRIVATE Threads::Threads)
endif()

# ThreadSanitizer — Linux/macOS only, opt-in via -DENABLE_TSAN=ON.
if(ENABLE_TSAN AND NOT WIN32)
    if(COMPILER_SUPPORTS_TSAN)
        target_compile_options(test_ring_spsc PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(test_ring_spsc PRIVATE -fsanitize=thread)
    endif()
endif()

add_test(NAME spsc_round_trip COMMAND test_ring_spsc)

# ── GCC-PHAT synthetic signal tests ──────────────────────────────────────────
add_executable(test_gcc_phat
    test_gcc_phat.cpp
    ${CMAKE_SOURCE_DIR}/src/gcc_phat.cpp
)

target_include_directories(test_gcc_phat PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_gcc_phat PRIVATE PFFFT)

set_property(TARGET test_gcc_phat PROPERTY CXX_STANDARD 17)
set_property(TARGET test_gcc_phat PROPERTY CXX_STANDARD_REQUIRED ON)

add_test(NAME gcc_phat_synthetic COMMAND test_gcc_phat)
