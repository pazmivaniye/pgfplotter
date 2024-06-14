CXXVER := 17
MINMACOSVER := 10.15

ifeq ($(OS), Windows_NT)
    LIBPATH := /mingw64/lib
    OSPRETTY := Windows
else
    ifeq ($(shell uname -s), Darwin)
        OSPRETTY := macOS
    else
        OSPRETTY := Linux
    endif
    LIBPATH := /usr/local/lib
endif
CXXFLAGS := -std=c++$(CXXVER) -O3 -Wall -Wextra -Wno-missing-braces -Wold-style-cast
ifeq ($(OSPRETTY), macOS)
    CXXFLAGS += -mmacosx-version-min=$(MINMACOSVER) -Wunguarded-availability -Wno-string-plus-int
else
    ifeq ($(OSPRETTY), Windows)
        CXXFLAGS += -Wno-deprecated-copy -static -mwindows
    endif
    CXXFLAGS += -pthread
endif
CXXFLAGS += -I..
LDLIBS := ../libpgfplotter.a
ifeq ($(OSPRETTY), Windows)
    LDFLAGS += -static-libgcc -static-libstdc++
endif

SRC := $(wildcard *.cpp)
ifeq ($(OSPRETTY), macOS)
    ARMOBJ := $(SRC:%.cpp=build/%_arm64.o)
    INTOBJ := $(SRC:%.cpp=build/%_x86_64.o)
else
    OBJ := $(SRC:%.cpp=build/%.o)
endif

print-% : ; @echo $* = $($*)

ifeq ($(OSPRETTY), macOS)
.PHONY: build/test_arm64 build/test_x86_64
test: build/test_arm64 build/test_x86_64
	lipo -create -output $@ $^

build/test_arm64: build $(ARMOBJ)
	$(CXX) -arch arm64 $(CXXFLAGS) -o $@ $(ARMOBJ) $(LDFLAGS) $(LDLIBS)

build/test_x86_64: build $(INTOBJ)
	$(CXX) -arch x86_64 $(CXXFLAGS) -o $@ $(INTOBJ) $(LDFLAGS) $(LDLIBS)
else
.PHONY: test
test: build $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)
endif

build/%_arm64.o: %.cpp
	$(CXX) -arch arm64 $(CXXFLAGS) -c -o $@ $<

build/%_x86_64.o: %.cpp
	$(CXX) -arch x86_64 $(CXXFLAGS) -c -o $@ $<

build/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build:
	mkdir -p $@

clean:
	$(RM) -r test build output
