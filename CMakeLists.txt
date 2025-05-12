cmake_minimum_required(VERSION 3.14)
project(winter-framework VERSION 0.1.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for optimization
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math -funroll-loops")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add library sources
file(GLOB_RECURSE CORE_SOURCES 
    "src/winter/core/*.cpp"
)

file(GLOB_RECURSE UTILS_SOURCES 
    "src/winter/utils/*.cpp"
)

file(GLOB_RECURSE STRATEGY_SOURCES 
    "src/winter/strategy/*.cpp"
)

# Create the winter library
add_library(winter STATIC 
    ${CORE_SOURCES}
    ${UTILS_SOURCES}
    ${STRATEGY_SOURCES}
)

target_link_libraries(winter PUBLIC Threads::Threads)

# Add the simulate executable
add_executable(simulate src/simulate/simulate.cpp)
target_link_libraries(simulate PRIVATE winter)

# Add benchmark executables
add_executable(latency_benchmark tests/performance/latency_benchmark.cpp)
target_link_libraries(latency_benchmark PRIVATE winter)

add_executable(throughput_benchmark tests/performance/throughput_benchmark.cpp)
target_link_libraries(throughput_benchmark PRIVATE winter)

# Add unit tests
enable_testing()
add_executable(core_tests tests/unit/core_tests.cpp)
target_link_libraries(core_tests PRIVATE winter)
add_test(NAME CoreTests COMMAND core_tests)

add_executable(strategy_tests tests/unit/strategy_tests.cpp)
target_link_libraries(strategy_tests PRIVATE winter)
add_test(NAME StrategyTests COMMAND strategy_tests)

add_executable(unit_tests tests/unit/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE winter)
add_test(NAME UnitTests COMMAND unit_tests)

# Install targets
install(TARGETS winter simulate
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
