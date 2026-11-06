# embedbench (embedbench.cpp) — standalone, without Qt.
#   Desktop, against an installed llama.cpp (the dev shell's):  cmake -S . -B build && cmake --build build
#   Bundled llama.cpp sources (Android, nix/android/embedbench.nix):  -DLLAMA_SRC=<llama.cpp source>
cmake_minimum_required(VERSION 3.21)
project(embedbench CXX C)
set(CMAKE_CXX_STANDARD 17)

if(LLAMA_SRC)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${LLAMA_SRC} llama.cpp EXCLUDE_FROM_ALL)
else()
    find_package(llama CONFIG REQUIRED)
endif()

add_executable(embedbench embedbench.cpp)
target_link_libraries(embedbench PRIVATE llama ggml)
install(TARGETS embedbench RUNTIME DESTINATION bin)
