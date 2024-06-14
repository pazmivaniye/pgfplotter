PROJNAME := pgfplotter
CXXVER := 17
MINMACOSVER := 10.15

LIBNAME := $(shell echo $(PROJNAME) | sed 's/_//g' | tr '[:upper:]' '[:lower:]')
ifeq ($(OS), Windows_NT)
    LIBPATH := /mingw64/lib
    INCLPATH := /mingw64/include
    OSPRETTY := Windows
else
    ifeq ($(shell uname -s), Darwin)
        OSPRETTY := macOS
    else
        OSPRETTY := Linux
    endif
    LIBPATH := /usr/local/lib
    INCLPATH := /usr/local/include
endif
OPTIM := 3
CFLAGS := -O$(OPTIM) -Wall -Wextra -Wno-missing-braces
ifeq ($(OSPRETTY), macOS)
    CFLAGS += -mmacosx-version-min=$(MINMACOSVER) -Wunguarded-availability
else
    ifeq ($(OSPRETTY), Windows)
        CFLAGS += -Wno-cast-function-type
    endif
endif
CXXFLAGS := -std=c++$(CXXVER) $(CFLAGS) -Wold-style-cast
ifeq ($(OSPRETTY), macOS)
    CXXFLAGS += -Wno-string-plus-int
else
    ifeq ($(OSPRETTY), Windows)
        CXXFLAGS += -Wno-deprecated-copy
    endif
endif
ARFLAGS := -rcs

SRC := $(wildcard *.cpp)
ifeq ($(OSPRETTY), macOS)
    ARMOBJ := $(SRC:.cpp=_arm64.o)
    INTOBJ := $(SRC:.cpp=_x86_64.o)
else
    OBJ := $(SRC:.cpp=.o)
endif

print-% : ; @echo $* = $($*)

.PHONY: test
default: test

ifeq ($(OSPRETTY), macOS)
lib$(LIBNAME).a: lib$(LIBNAME)_arm64.a lib$(LIBNAME)_x86_64.a
	lipo -create -output $@ $^

lib$(LIBNAME)_arm64.a: $(ARMOBJ)
	$(RM) $@
	ar $(ARFLAGS) $@ $^

lib$(LIBNAME)_x86_64.a: $(INTOBJ)
	$(RM) $@
	ar $(ARFLAGS) $@ $^
else
lib$(LIBNAME).a: $(OBJ)
	$(RM) $@
	ar $(ARFLAGS) $@ $^
endif

install: $(PROJNAME) lib$(LIBNAME).a
	cmp -s $(PROJNAME) $(INCLPATH)/$(PROJNAME) || cp $(PROJNAME) $(INCLPATH)/
	cmp -s lib$(LIBNAME).a $(LIBPATH)/lib$(LIBNAME).a || cp lib$(LIBNAME).a $(LIBPATH)/

test: lib$(LIBNAME).a
	$(MAKE) -C test
	test/test

%_arm64.o: %.cpp
	$(CXX) -arch arm64 -c -o $@ $< $(CXXFLAGS)

%_x86_64.o: %.cpp
	$(CXX) -arch x86_64 -c -o $@ $< $(CXXFLAGS)

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

clean:
	$(RM) *.o *.a
	$(MAKE) -C test clean
