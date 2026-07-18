cmake_minimum_required(VERSION 3.16)
project(AxiomJamesDSPConsole LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "AxiomJamesDSPConsole requires a Windows target toolchain")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(JDSP_PATH "${REPO_ROOT}/JDSP4Linux/libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp")
set(WIN_APP "${REPO_ROOT}/JamesDSP-Windows")

file(GLOB_RECURSE JDSP_SOURCES "${JDSP_PATH}/*.c")
list(FILTER JDSP_SOURCES EXCLUDE REGEX ".*eel2/cpthread.c")

add_library(AxiomJamesDSPCore STATIC
    ${JDSP_SOURCES}
)

target_include_directories(AxiomJamesDSPCore PUBLIC
    "${WIN_APP}/src"
    "${WIN_APP}/src/compat"
    "${JDSP_PATH}"
    "${JDSP_PATH}/jdsp"
    "${JDSP_PATH}/jdsp/Effects/eel2"
    "${REPO_ROOT}/JDSP4Linux/libjamesdsp"
)

target_compile_definitions(AxiomJamesDSPCore PUBLIC
    WIN32
    _WIN32
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

add_executable(AxiomJamesDSPConsole
    "${WIN_APP}/src/main.cpp"
    "${WIN_APP}/src/DspConfig.cpp"
)
target_link_libraries(AxiomJamesDSPConsole PRIVATE
    AxiomJamesDSPCore
    avrt
    ole32
    winmm
    mmdevapi
)
if(MSVC)
    target_compile_options(AxiomJamesDSPConsole PRIVATE /EHsc /bigobj)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(AxiomJamesDSPNativeTests
        "${CMAKE_CURRENT_LIST_DIR}/tests/liveprog_native_tests.cpp"
    )
    target_link_libraries(AxiomJamesDSPNativeTests PRIVATE AxiomJamesDSPCore winmm)
    if(MSVC)
        target_compile_options(AxiomJamesDSPNativeTests PRIVATE /EHsc /bigobj)
    endif()
    add_test(NAME liveprog_native COMMAND AxiomJamesDSPNativeTests)
endif()
