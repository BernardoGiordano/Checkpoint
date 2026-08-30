# Host-side tests. These build and run on the build machine with the system
# compiler, not with devkitPro, so they can link platform sources directly and
# be run on every change without hardware.
#
# To add a test: drop the source in this directory, append its name to TESTS,
# and define <name>_SRC with every translation unit it needs.

ROOT		:=	..
BUILD		:=	build
CXX		?=	g++
CXXFLAGS	:=	-std=c++23 -Wall -Wextra -Werror -I$(ROOT)/3ds/include

TESTS			:=	ntrcard-probe
ntrcard-probe_SRC	:=	ntrcard-probe-test.cpp $(ROOT)/3ds/source/ntrcard.cpp

all: $(addsuffix .run,$(TESTS))

# Keep the built binaries around so a rerun only re-executes them.
.SECONDARY:

.SECONDEXPANSION:
$(BUILD)/%: $$($$*_SRC) | $(BUILD)
	@$(CXX) $(CXXFLAGS) $^ -o $@

%.run: $(BUILD)/%
	@$<
	@echo "$* host test passed"

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)

# Note: the .run targets are deliberately not .PHONY — make skips implicit-rule
# search for phony targets, which would leave the %.run pattern rule unused.
.PHONY: all clean
