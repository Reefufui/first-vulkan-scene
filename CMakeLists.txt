cmake_minimum_required (VERSION 3.8)
project (vulkan_minimal_compute)

find_package(Vulkan)

# get rid of annoying MSVC warnings.
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

set (CMAKE_CXX_STANDARD 11)

include_directories(${Vulkan_INCLUDE_DIR})

set(ALL_LIBS  ${Vulkan_LIBRARY} )

#uncomment this to detect broken memory problems via gcc sanitizers
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer -fsanitize=leak -fsanitize=undefined -fsanitize=bounds-strict")

add_executable(vulkan_minimal_compute src/main.cpp src/vk_utils.h src/vk_utils.cpp src/Bitmap.h src/Bitmap.cpp)

set_target_properties(vulkan_minimal_compute PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

target_link_libraries(vulkan_minimal_compute ${ALL_LIBS} )