










CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinc -Ibuild -MMD -MP
LDFLAGS  :=
FLEX     := flex
BISON    := bison

SRCDIR   := src
OBJDIR   := build

OBJECT_SRC  := $(SRCDIR)/object.cpp
IO_SRC      := $(SRCDIR)/file_utils.cpp
LINKER_SRC  := $(SRCDIR)/linker.cpp $(SRCDIR)/linker_main.cpp
ASM_SRC     := $(wildcard $(SRCDIR)/asembler*.cpp)
EMU_SRC     := $(wildcard $(SRCDIR)/emulator*.cpp)

OBJECT_OBJ  := $(OBJECT_SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
IO_OBJ      := $(IO_SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
LINKER_OBJ  := $(LINKER_SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
ASM_OBJ     := $(ASM_SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
ASM_GENERATED_OBJ := $(OBJDIR)/asembler_parser.o $(OBJDIR)/asembler_lexer.o
EMU_OBJ     := $(EMU_SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

TARGETS := linker
ifneq ($(strip $(ASM_SRC)),)
TARGETS += assembler
endif
ifneq ($(strip $(EMU_SRC)),)
TARGETS += emulator
endif

.PHONY: all clean
all: $(TARGETS)

linker: $(OBJECT_OBJ) $(IO_OBJ) $(LINKER_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

assembler: $(OBJECT_OBJ) $(IO_OBJ) $(ASM_OBJ) $(ASM_GENERATED_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^





$(ASM_OBJ) $(ASM_GENERATED_OBJ): CXXFLAGS += -DSS_USE_FLEX_BISON

emulator: $(EMU_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJDIR)/asembler_parser.cpp: misc/asembler_parser.y | $(OBJDIR)
	$(BISON) --defines=$(OBJDIR)/asembler_parser.hpp \
	  --output=$(OBJDIR)/asembler_parser.cpp $<

$(OBJDIR)/asembler_parser.hpp: $(OBJDIR)/asembler_parser.cpp
	@:

$(OBJDIR)/asembler_lexer.cpp: misc/asembler_lexer.l $(OBJDIR)/asembler_parser.hpp | $(OBJDIR)
	$(FLEX) --outfile=$(OBJDIR)/asembler_lexer.cpp $<

$(OBJDIR)/asembler_lexer.hpp: $(OBJDIR)/asembler_lexer.cpp
	@:

$(OBJDIR)/asembler_parser.o: $(OBJDIR)/asembler_parser.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJDIR)/asembler_lexer.o: $(OBJDIR)/asembler_lexer.cpp $(OBJDIR)/asembler_lexer.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJDIR)/asembler_frontend.o: $(OBJDIR)/asembler_parser.hpp $(OBJDIR)/asembler_lexer.hpp

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) linker assembler emulator
	rm -f *.tmp

-include $(wildcard $(OBJDIR)/*.d)
