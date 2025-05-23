# Makefile for Winter ultra-low latency framework
CXX = g++
CXXFLAGS = -std=c++20 -O3 -march=native -mtune=native -ffast-math -funroll-loops
INCLUDES = -I./include -I.

# Detect operating system
ifeq ($(OS),Windows_NT)
    # Windows-specific flags
    LDFLAGS = -pthread -lzmq -Wl,--subsystem,console
else
    # Linux/Unix-specific flags
    LDFLAGS = -pthread -lzmq -ldl
endif

# Source directories
SRC_DIR = src/winter
SIMULATE_DIR = src/simulate
EXAMPLES_DIR = examples
TESTS_DIR = tests
PLUGINS_DIR = plugins
APPS_DIR = applications

# Build directory
BUILD_DIR = build

# Source files
# Add to UTILS_SOURCES or similar section

CORE_SOURCES = $(wildcard $(SRC_DIR)/core/*.cpp)
UTILS_SOURCES = $(wildcard $(SRC_DIR)/utils/*.cpp)
STRATEGY_SOURCES = $(wildcard $(SRC_DIR)/strategy/*.cpp)
BACKTEST_SOURCES = $(wildcard $(SRC_DIR)/backtest/*.cpp)
SIMULATE_SOURCES = $(SIMULATE_DIR)/simulate.cpp

# New source files
CONFIG_SOURCES = $(SRC_DIR)/utils/config.cpp
PLUGIN_LOADER_SOURCES = $(SRC_DIR)/utils/plugin_loader.cpp
STRATEGY_FACTORY_SOURCES = $(SRC_DIR)/strategy/strategy_factory.cpp

# Object files
CORE_OBJECTS = $(patsubst $(SRC_DIR)/core/%.cpp,$(BUILD_DIR)/core/%.o,$(CORE_SOURCES))
UTILS_OBJECTS = $(patsubst $(SRC_DIR)/utils/%.cpp,$(BUILD_DIR)/utils/%.o,$(UTILS_SOURCES))
STRATEGY_OBJECTS = $(patsubst $(SRC_DIR)/strategy/%.cpp,$(BUILD_DIR)/strategy/%.o,$(STRATEGY_SOURCES))
BACKTEST_OBJECTS = $(patsubst $(SRC_DIR)/backtest/%.cpp,$(BUILD_DIR)/backtest/%.o,$(BACKTEST_SOURCES))
SIMULATE_OBJECTS = $(BUILD_DIR)/simulate.o

# New object files
CONFIG_OBJECTS = $(BUILD_DIR)/utils/config.o
PLUGIN_LOADER_OBJECTS = $(BUILD_DIR)/utils/plugin_loader.o
STRATEGY_FACTORY_OBJECTS = $(BUILD_DIR)/strategy/strategy_factory.o

# Library target
WINTER_LIB = $(BUILD_DIR)/libwinter.a

# Executable targets
SIMULATE_EXE = $(BUILD_DIR)/simulate

# Default target
all: directories $(WINTER_LIB) $(SIMULATE_EXE)

# Create build directories
directories:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/core
	mkdir -p $(BUILD_DIR)/utils
	mkdir -p $(BUILD_DIR)/strategy
	mkdir -p $(BUILD_DIR)/backtest
	mkdir -p $(BUILD_DIR)/examples
	mkdir -p $(BUILD_DIR)/tests
	mkdir -p $(BUILD_DIR)/apps
	mkdir -p $(BUILD_DIR)/plugins

# Build the Winter library
$(WINTER_LIB): $(CORE_OBJECTS) $(UTILS_OBJECTS) $(STRATEGY_OBJECTS) $(BACKTEST_OBJECTS) $(CONFIG_OBJECTS) $(PLUGIN_LOADER_OBJECTS) $(STRATEGY_FACTORY_OBJECTS)
	ar rcs $@ $^

# Build the simulate executable
$(SIMULATE_EXE): $(SIMULATE_OBJECTS) $(WINTER_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Compile core source files
$(BUILD_DIR)/core/%.o: $(SRC_DIR)/core/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile utils source files
$(BUILD_DIR)/utils/%.o: $(SRC_DIR)/utils/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile strategy source files
$(BUILD_DIR)/strategy/%.o: $(SRC_DIR)/strategy/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile backtest source files
$(BUILD_DIR)/backtest/%.o: $(SRC_DIR)/backtest/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile simulate source file
$(BUILD_DIR)/simulate.o: $(SIMULATE_DIR)/simulate.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile config source file
$(BUILD_DIR)/utils/config.o: $(SRC_DIR)/utils/config.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile plugin loader source file
$(BUILD_DIR)/utils/plugin_loader.o: $(SRC_DIR)/utils/plugin_loader.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile strategy factory source file
$(BUILD_DIR)/strategy/strategy_factory.o: $(SRC_DIR)/strategy/strategy_factory.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean build files
clean:
	rm -rf $(BUILD_DIR)

# Run the simulation
run: $(SIMULATE_EXE)
	./$(SIMULATE_EXE)

# Run backtest
backtest: $(SIMULATE_EXE)
	./$(SIMULATE_EXE) --backtest historical_data.csv

# Run trade simulation
trade: $(SIMULATE_EXE)
	./$(SIMULATE_EXE) --trade 2021_Market_Data_RAW.csv

.PHONY: all directories clean run backtest trade
