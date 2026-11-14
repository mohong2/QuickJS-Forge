cmake_minimum_required(VERSION 3.20)
project(QuickJSForge VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# CRT: hxcpp on Windows links with /MT, so the embedded QuickJS must too.
# quickjs-ng still declares cmake_minimum_required(3.10), which resets CMP0091
# to OLD inside its directory; both lines below are required.
# ---------------------------------------------------------------------------
if(MSVC)
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW CACHE STRING "" FORCE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(native/quickjs EXCLUDE_FROM_ALL)

if(MSVC)
    set_property(TARGET qjs PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Bridge: flat C ABI on top of QuickJS (see README.md, section 设计要点)
# ---------------------------------------------------------------------------
add_library(qjs_bridge STATIC native/bridge/qjs_bridge.cpp)
target_compile_features(qjs_bridge PRIVATE cxx_std_17)
target_include_directories(qjs_bridge
    PUBLIC  native/bridge
    PRIVATE native/quickjs)
target_link_libraries(qjs_bridge PUBLIC qjs)

if(MSVC)
    target_compile_options(qjs_bridge PRIVATE /W3 /EHsc)
else()
    target_compile_options(qjs_bridge PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Evidence programs (also the CI test suite)
# ---------------------------------------------------------------------------
add_executable(feature_probe tests/c/feature_probe.c)
target_link_libraries(feature_probe PRIVATE qjs)
if(MSVC)
    target_compile_options(feature_probe PRIVATE /W3)
endif()

add_executable(bridge_smoke tests/c/bridge_smoke.cpp)
target_link_libraries(bridge_smoke PRIVATE qjs_bridge)
target_compile_features(bridge_smoke PRIVATE cxx_std_17)

add_executable(abi_compare tests/c/abi_compare.cpp)
target_link_libraries(abi_compare PRIVATE qjs_bridge)
target_compile_features(abi_compare PRIVATE cxx_std_17)

enable_testing()
add_test(NAME syntax_probe  COMMAND feature_probe)
add_test(NAME bridge_smoke  COMMAND bridge_smoke)
add_test(NAME abi_compare   COMMAND abi_compare)

# ---------------------------------------------------------------------------
# Packaging helper: install the static runtime + headers for hxcpp projects
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS qjs qjs_bridge ARCHIVE DESTINATION lib)
install(FILES native/bridge/qjs_bridge.h DESTINATION include)
install(FILES native/quickjs/quickjs.h DESTINATION include)
