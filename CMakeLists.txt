cmake_minimum_required(VERSION 3.16)
project(Paradox C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# dependencies

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3
)
FetchContent_MakeAvailable(freetype)

# glad

add_library(glad STATIC third_party/glad/src/gl.c) 

target_include_directories(glad PUBLIC third_party/glad/include)

# source (every .c / .h anywhere under src/)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.h")

# assets

file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/*")

# embedded stuff

enable_language(RC)

set(RC_TEXT "")
set(PATHS_TEXT "")
set(IDS_TEXT "")
set(ASSET_ID 100)

foreach(f ${ASSET_FILES})
    if(NOT IS_DIRECTORY "${f}")
        file(RELATIVE_PATH rel "${CMAKE_SOURCE_DIR}" "${f}")
        math(EXPR ASSET_ID "${ASSET_ID} + 1")
        string(APPEND RC_TEXT "${ASSET_ID} RCDATA \"${f}\"\n")
        string(APPEND PATHS_TEXT "\"${rel}\",\n")
        string(APPEND IDS_TEXT "${ASSET_ID},\n")
    endif()

endforeach()

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/assets.rc" CONTENT "${RC_TEXT}" )

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/asset_table.c" CONTENT

"const char* const asset_paths[] = { ${PATHS_TEXT}};
const int asset_ids[] = { ${IDS_TEXT}};
const int asset_count = ${ASSET_ID} - 100;")

set_source_files_properties("${CMAKE_BINARY_DIR}/assets.rc" PROPERTIES GENERATED TRUE)

set_source_files_properties("${CMAKE_BINARY_DIR}/asset_table.c" PROPERTIES GENERATED TRUE)

set_source_files_properties(${ASSET_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

# executable

add_executable(Paradox
    ${SRC_FILES}
    ${ASSET_FILES}
    "${CMAKE_BINARY_DIR}/assets.rc"
    "${CMAKE_BINARY_DIR}/asset_table.c") 

# includes: every folder under src/ that contains a header is added automatically

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.h")

set(INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/src")

foreach(h ${HEADER_FILES})
    get_filename_component(dir "${h}" DIRECTORY)
    list(APPEND INCLUDE_DIRS "${dir}")
endforeach()

list(REMOVE_DUPLICATES INCLUDE_DIRS)

target_include_directories(Paradox PRIVATE
    ${INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/third_party/glad/include")

# linking

target_link_libraries(Paradox PRIVATE glad glfw freetype)

# windows & msvc

if(MSVC)

    target_link_options(Paradox PRIVATE
        "/SUBSYSTEM:WINDOWS"
        "/ENTRY:mainCRTStartup"
    )

    target_compile_options(Paradox PRIVATE /W4)

    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Paradox)

    set_target_properties(Paradox PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Paradox>")

else()

    target_compile_options(Paradox PRIVATE -Wall -Wextra)

    target_link_libraries(Paradox PRIVATE m)

endif()