cmake_minimum_required(VERSION 3.16)
project(pub_pin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Output binary name ────────────────────────────────────────────────────────
add_executable(pub_pin main.cpp)

# ── Release build optimisations ───────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

target_compile_options(pub_pin PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2 -Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/O2 /W3>
)

# ── Static runtime linking (MinGW) ────────────────────────────────────────────
if(MINGW)
    target_link_options(pub_pin PRIVATE
            -static-libgcc
            -static-libstdc++
            -static
    )
endif()

# ── Link filesystem library (needed on some GCC < 9 toolchains) ───────────────
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(pub_pin stdc++fs)
endif()

# ── Windows: prefer a console subsystem so stdout is visible in terminal ──────
if(WIN32)
    set_target_properties(pub_pin PROPERTIES
            WIN32_EXECUTABLE FALSE
    )
endif()

# ── Install target (optional: `cmake --install .`) ────────────────────────────
install(TARGETS pub_pin DESTINATION bin)