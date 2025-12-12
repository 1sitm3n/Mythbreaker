cmake_minimum_required(VERSION 3.20)
project(Mythbreaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
endforeach()

find_package(Vulkan REQUIRED)
message(STATUS "Vulkan: ${Vulkan_INCLUDE_DIRS}")

find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found!")
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_SOURCE_DIR}/third_party/glm)
target_compile_definitions(glm INTERFACE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_ENABLE_EXPERIMENTAL)

add_library(vma INTERFACE)
target_include_directories(vma INTERFACE ${CMAKE_SOURCE_DIR}/third_party/vma)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_SOURCE_DIR}/third_party/stb)

add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE ${CMAKE_SOURCE_DIR}/third_party/tinyobjloader)

set(SHADER_BIN_DIR ${CMAKE_SOURCE_DIR}/shaders/bin)
if(NOT EXISTS ${SHADER_BIN_DIR}/basic.vert.spv OR NOT EXISTS ${SHADER_BIN_DIR}/basic.frag.spv)
    message(FATAL_ERROR "Shaders not compiled! Run: .\\scripts\\compile_shaders.ps1")
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

add_executable(Mythbreaker ${SOURCES})

target_include_directories(Mythbreaker PRIVATE ${CMAKE_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})

target_link_libraries(Mythbreaker PRIVATE Vulkan::Vulkan glfw glm vma stb tinyobjloader)

if(WIN32)
    target_compile_definitions(Mythbreaker PRIVATE VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_definitions(Mythbreaker PRIVATE $<$<CONFIG:Debug>:MYTHBREAKER_DEBUG MYTHBREAKER_VALIDATION_LAYERS>)

add_custom_command(TARGET Mythbreaker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Mythbreaker>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_BIN_DIR} $<TARGET_FILE_DIR:Mythbreaker>/shaders
)

message(STATUS "Mythbreaker configured!")
