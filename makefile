# Makefile for Winter ultra-low latency framework
CXX = g++
CXXFLAGS = -std=c++20 -O3 -march=native -mtune=native -ffast-math -funroll-loops
INCLUDES = -I./include -I.
LDFLAGS = -pthread -lzmq -Wl,--subsystem,console

# Source directories
SRC_DIR = src/winter
SIMULATE_DIR = src/simulate
DATA_PUB_DIR = src/data_publisher

# Build directory
BUILD_DIR = build

# Source files
CORE_SOURCES = $(wildcard $(SRC_DIR)/core/*.cpp)
UTILS_SOURCES = $(wildcard $(SRC_DIR)/utils/*.cpp)
STRATEGY_SOURCES = $(wildcard $(SRC_DIR)/strategy/*.cpp)
BACKTEST_SOURCES = $(wildcard $(SRC_DIR)/backtest/*.cpp)
SIMULATE_SOURCES = $(SIMULATE_DIR)/simulate.cpp

# Object files
CORE_OBJECTS = $(patsubst $(SRC_DIR)/core/%.cpp,$(BUILD_DIR)/core/%.o,$(CORE_SOURCES))
UTILS_OBJECTS = $(patsubst $(SRC_DIR)/utils/%.cpp,$(BUILD_DIR)/utils/%.o,$(UTILS_SOURCES))
STRATEGY_OBJECTS = $(patsubst $(SRC_DIR)/strategy/%.cpp,$(BUILD_DIR)/strategy/%.o,$(STRATEGY_SOURCES))
BACKTEST_OBJECTS = $(patsubst $(SRC_DIR)/backtest/%.cpp,$(BUILD_DIR)/backtest/%.o,$(BACKTEST_SOURCES))
SIMULATE_OBJECTS = $(BUILD_DIR)/simulate.o

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

# Build the Winter library
$(WINTER_LIB): $(CORE_OBJECTS) $(UTILS_OBJECTS) $(STRATEGY_OBJECTS) $(BACKTEST_OBJECTS)
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
