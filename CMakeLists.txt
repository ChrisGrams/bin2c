# Root CMakeLists.txt file
cmake_minimum_required(VERSION 3.28)
project(bin2c LANGUAGES C)
set(_project_target ${CMAKE_PROJECT_NAME})

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
endif()

add_executable(${_project_target})
target_compile_features(${_project_target} PUBLIC c_std_99)

add_subdirectory(src)

