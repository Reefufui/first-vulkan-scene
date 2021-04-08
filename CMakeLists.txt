cmake_minimum_required (VERSION 3.8)
project (vulkan_shadow_map)

find_package(Vulkan)

# get rid of annoying MSVC warnings.
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

set (CMAKE_CXX_STANDARD 20)

include_directories(${Vulkan_INCLUDE_DIR})

set(ALL_LIBS  ${Vulkan_LIBRARY} )

find_package(glfw3 REQUIRED)
set(ALL_LIBS ${ALL_LIBS} ${GLFW_LIBRARIES} )

include_directories(${GLFW_INCLUDE_DIRS}
    src/vendor/glm
    src/vendor/tinyobjloader
    src/vendor/cpptqdm
    src/vendor/stb_image
    )

#uncomment this to detect broken memory problems via gcc sanitizers
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer -fsanitize=leak -fsanitize=undefined -fsanitize=bounds-strict")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wpedantic")

add_executable(vulkan_shadow_map
    src/main.cpp
    src/vk_utils.h
    src/vk_utils.cpp
    src/Mesh.hpp
    src/Mesh.cpp
    src/Texture.cpp
    src/Texture.hpp
    src/ParticleSystem.hpp
    src/vendor/stb_image/stb_image.cpp
    )

set_target_properties(vulkan_shadow_map PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

target_link_libraries(vulkan_shadow_map ${ALL_LIBS} ${GLFW_LIBRARIES} glfw)
