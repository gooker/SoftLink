cmake_minimum_required(VERSION 3.16)
project(SoftLink VERSION 1.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Build with the Windows MSVC toolchain.")
endif()

add_executable(SoftLink WIN32
    src/main.cpp
    src/platform.cpp
    src/migration.cpp
    src/history.cpp
    src/SoftLink.rc
)
target_compile_features(SoftLink PRIVATE cxx_std_17)
target_compile_definitions(SoftLink PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_include_directories(SoftLink PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(SoftLink PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/permissive-;/EHsc;/Gy;/Gw>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:/O1>"
)
target_link_libraries(SoftLink PRIVATE comctl32 shell32 ole32 uuid advapi32 user32 gdi32)
target_link_options(SoftLink PRIVATE /MANIFEST:NO /OPT:REF /OPT:ICF /INCREMENTAL:NO)
set_target_properties(SoftLink PROPERTIES
    OUTPUT_NAME SoftLink
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/dist>"
)
add_custom_command(TARGET SoftLink POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DEXECUTABLE=$<TARGET_FILE:SoftLink>"
            "-DBUILD_CONFIG=$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/src/check_size.cmake"
)
