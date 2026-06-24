CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I include
LDFLAGS =

WIN_CXX = x86_64-w64-mingw32-g++
WIN_CXXFLAGS = -std=c++17 -O2 -Wall -I include
WIN_LDFLAGS = -static

SRCS = src/main.cpp src/midi_file.cpp src/wav_writer.cpp src/soundfont.cpp src/sf_synth.cpp src/converter.cpp
OBJS = $(SRCS:.cpp=.o)
WIN_OBJS = $(SRCS:.cpp=.win.o)
TARGET = Mid2Wav
WIN_TARGET = Mid2Wav.exe

all: $(TARGET)

linux: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

windows: $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS)
	$(WIN_CXX) $(WIN_OBJS) -o $(WIN_TARGET) $(WIN_LDFLAGS)

src/%.win.o: src/%.cpp
	$(WIN_CXX) $(WIN_CXXFLAGS) -c $< -o $@

both: linux windows

clean:
	rm -f $(OBJS) $(WIN_OBJS) $(TARGET) $(WIN_TARGET) test/test_all.o test/test_all

test: test/test_all
	./test/test_all

TEST_SRCS = test/test_all.cpp src/wav_writer.cpp src/sf_synth.cpp src/midi_file.cpp src/soundfont.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

test/test_all: $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) -o test/test_all -lm

test/%.o: test/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: all linux windows both clean test
