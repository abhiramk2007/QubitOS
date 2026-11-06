CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -Iengine

ENGINE_SRCS = engine/dag.cpp engine/qasm_parser.cpp engine/hardware.cpp engine/allocator.cpp engine/scheduler.cpp
MAIN_SRC = engine/main.cpp

all: qubitos test_units test_acceptance

qubitos: $(MAIN_SRC) $(ENGINE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_units: tests/test_units.cpp $(ENGINE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_acceptance: tests/test_acceptance.cpp $(ENGINE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

test: test_units test_acceptance
	./test_units
	./test_acceptance

clean:
	rm -f qubitos test_units test_acceptance
	rm -rf out/*.json

.PHONY: all test clean

