cmake_minimum_required(VERSION 3.20)
project(Mythbreaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/glm
    ${CMAKE_SOURCE_DIR}/third_party/vma
    ${CMAKE_SOURCE_DIR}/third_party/stb
    ${CMAKE_SOURCE_DIR}/third_party/tinyobjloader
    ${Vulkan_INCLUDE_DIRS}
)

# Source files
set(ENGINE_SOURCES
    src/engine/Logger.cpp
    src/engine/Timer.cpp
    src/engine/Input.cpp
    src/engine/vulkan/VulkanContext.cpp
    src/engine/vulkan/VulkanSwapchain.cpp
    src/engine/vulkan/VulkanPipeline.cpp
    src/engine/vulkan/VulkanBuffer.cpp
    src/engine/vulkan/VulkanDescriptors.cpp
    src/engine/vulkan/VulkanTexture.cpp
)

set(APP_SOURCES
    src/app/main.cpp
)

# Executable
add_executable(Mythbreaker ${ENGINE_SOURCES} ${APP_SOURCES})

target_link_libraries(Mythbreaker
    Vulkan::Vulkan
    glfw
)

# Copy shaders check
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/shaders/bin/basic.vert.spv")
    message(WARNING "Shaders not compiled! Run scripts/compile_shaders.ps1")
endif()
