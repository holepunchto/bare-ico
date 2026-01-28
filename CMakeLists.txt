cmake_minimum_required(VERSION 3.31)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)

project(bare_ico C)

# Fetch stb_image.h for robust image decoding
include(FetchContent)
FetchContent_Declare(
  stb
  URL https://github.com/nothings/stb/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(stb)

add_bare_module(bare_ico)

target_sources(
  ${bare_ico}
  PRIVATE
    binding.c
)

target_include_directories(
  ${bare_ico}
  PRIVATE
    ${stb_SOURCE_DIR}
)
