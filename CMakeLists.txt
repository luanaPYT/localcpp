cmake_minimum_required(VERSION 3.16)
project(LocalDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(localdb
    src/storage.cpp
    src/table.cpp
    src/index.cpp
    src/query.cpp
    src/database.cpp
)

target_include_directories(localdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(localdb_cli src/main.cpp)
target_link_libraries(localdb_cli PRIVATE localdb)

add_executable(localdb_tests tests/tests.cpp)
target_link_libraries(localdb_tests PRIVATE localdb)

add_executable(localdb_import src/import.cpp)
target_link_libraries(localdb_import PRIVATE localdb)

enable_testing()
add_test(NAME localdb_tests COMMAND localdb_tests)
