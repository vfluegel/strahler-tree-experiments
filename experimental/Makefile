CXX      := g++-15
CXXFLAGS := -std=c++20 -Wall

TARGET   := str-simd
SRC      := str_simd.cpp

all: $(TARGET)

debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

.PHONY: all debug clean