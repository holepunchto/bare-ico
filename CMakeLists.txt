cmake_minimum_required(VERSION 3.31)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)

project(bare_ico C)

fetch_package("github:nothings/stb#802cd45" SOURCE_DIR stb)

add_bare_module(bare_ico)

target_sources(
  ${bare_ico}
  PRIVATE
    binding.c
)

target_include_directories(
  ${bare_ico}
  PRIVATE
    ${stb}
)
