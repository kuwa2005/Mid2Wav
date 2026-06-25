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

# ── Tests ──────────────────────────────────────────────────────────

TEST_SRCS = test/test_runner.cpp test/test_wav.cpp test/test_midi.cpp \
            test/test_soundfont.cpp test/test_synth.cpp test/test_fx.cpp \
            test/test_converter.cpp test/test_integration.cpp test/test_audio_quality.cpp
TEST_LIB_SRCS = src/wav_writer.cpp src/sf_synth.cpp src/midi_file.cpp \
                src/soundfont.cpp src/converter.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o) $(TEST_LIB_SRCS:.cpp=.o)
TEST_BIN = test/test_runner

test: $(TEST_BIN)
	./$(TEST_BIN)

test-unit: $(TEST_BIN)
	./$(TEST_BIN) unit

test-integration: $(TEST_BIN)
	./$(TEST_BIN) integration

test-quality: $(TEST_BIN)
	./$(TEST_BIN) quality

test-all: test

test-wav: $(TEST_BIN)
	./$(TEST_BIN) wav

test-midi: $(TEST_BIN)
	./$(TEST_BIN) midi

test-soundfont: $(TEST_BIN)
	./$(TEST_BIN) soundfont

test-synth: $(TEST_BIN)
	./$(TEST_BIN) synth

test-fx: $(TEST_BIN)
	./$(TEST_BIN) fx

test-converter: $(TEST_BIN)
	./$(TEST_BIN) converter

$(TEST_BIN): $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) -o $(TEST_BIN) -lm

test/%.o: test/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(WIN_OBJS) $(TARGET) $(WIN_TARGET) \
	      test/test_runner.o test/test_wav.o test/test_midi.o \
	      test/test_soundfont.o test/test_synth.o test/test_fx.o \
	      test/test_converter.o test/test_integration.o test/test_audio_quality.o test/test_runner \
	      test/test_all.o test/test_all

.PHONY: all linux windows both clean test test-all test-unit test-integration test-quality \
        test-wav test-midi test-soundfont test-synth test-fx test-converter
