SUITE := ghoti.io
PROJECT := chron

BUILD ?= release
# The version of this library. MINOR_VERSION carries the minor and the patch as
# one dotted string; the two are split out below for the places that need three
# separate integers. See CONVENTIONS.md section 4.
MAJOR_VERSION := 0
MINOR_VERSION := 0.0
VERSION_MINOR_ONLY := $(word 1,$(subst ., ,$(MINOR_VERSION)))
VERSION_PATCH_ONLY := $(or $(word 2,$(subst ., ,$(MINOR_VERSION))),0)
# Substituted into the .pc file; an empty Version: field makes every
# pkg-config version constraint fail.
VERSION := $(MAJOR_VERSION).$(MINOR_VERSION)

# Names this build everywhere: the .pc file, the install directory, the soname
# and the symbol token. It defaults to the major version, so an ordinary build
# of 1.x is "-1" and two majors cannot be loaded into one process by mistake.
# Override it for a build that wants its own identity:  make BRANCH=-dev
BRANCH ?= -$(MAJOR_VERSION)

# What the library reports as its version. The branch is appended only when it
# is not the default, so an ordinary build says "1.2.3" and an overridden one
# says "1.2.3-dev". Computed before BUILD=debug rewrites BRANCH below.
ifeq ($(BRANCH),-$(MAJOR_VERSION))
VERSION_STRING := $(VERSION)
else
VERSION_STRING := $(VERSION)$(BRANCH)
endif

# If BUILD is debug, append -debug.
#
# "override" because BRANCH may have come from the command line, and a
# command-line variable otherwise wins over a plain assignment here: without it
# `make BRANCH=-dev BUILD=debug` produced a debug build carrying the release
# token, whose symbols collide with the release build's.
ifeq ($(BUILD),debug)
    override BRANCH := $(BRANCH)-debug
    override VERSION_STRING := $(VERSION_STRING)-debug
endif

BASE_NAME := lib$(SUITE)-$(PROJECT)$(BRANCH).so
# The symbol namespace token, from BRANCH, so that the token inside every
# exported symbol is the same one that names the .pc file, the install directory
# and the shared library. See CONVENTIONS.md section 4.
LIBVER_SYMBOL := $(shell echo "ghotiio_$(PROJECT)$(BRANCH)" | sed 's/[.-]/_/g')

BASE_NAME_PREFIX := lib$(SUITE)-$(PROJECT)$(BRANCH)
SO_NAME := $(BASE_NAME).$(MAJOR_VERSION)
STATIC_TARGET := $(BASE_NAME_PREFIX).a
ENV_VARS :=

# PKG_CONFIG_PATH names where this project's own .pc file is installed, and the
# platform block below overwrites it to say so. Remember what the environment
# asked for first, so dependency lookup can still honour it further down.
PKG_CONFIG_PATH_ENV := $(PKG_CONFIG_PATH)

# `override` on each of those: BUILD may arrive on the command line, and a
# command-line variable beats a plain makefile assignment, so without it
# `make BUILD=debug` skips the rewrite and builds into ./build/debug --
# outside the platform tree, and a different tree from the one plain `make`
# uses. The platform segment exists to keep linux/mac/win builds apart.

# Detect OS
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	OS_NAME := Linux
	LIB_EXTENSION := so
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-soname,$(SO_NAME)
	TARGET := $(SO_NAME).$(MINOR_VERSION)
	EXE_EXTENSION :=
	# Additional Linux-specific variables
	PKG_CONFIG_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	override BUILD := linux/$(BUILD)

else ifeq ($(UNAME_S), Darwin)
	OS_NAME := Mac
	LIB_EXTENSION := dylib
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,-install_name,$(BASE_NAME_PREFIX).dylib
	TARGET := $(BASE_NAME_PREFIX).dylib
	EXE_EXTENSION :=
	# Additional macOS-specific variables
	PKG_CONFIG_PATH := /usr/local/share/pkgconfig
	INCLUDE_INSTALL_PATH := /usr/local/include
	LIB_INSTALL_PATH := /usr/local/lib
	PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
	override BUILD := mac/$(BUILD)

else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)  # 32-bit Windows
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	# Additional Windows-specific variables
	# This is the path to the pkg-config files on MSYS2
	PKG_CONFIG_PATH := /mingw32/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw32/include
	LIB_INSTALL_PATH := /mingw32/lib
	BIN_INSTALL_PATH := /mingw32/bin
	# Windows paths for .pc so gcc invoked by mingw can resolve -I/-L (cygpath for MSYS2)
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	override BUILD := win32/$(BUILD)

# TODO(windows): the Windows branches in this file were adapted from image's
# and have never been run, nor has GCHRON_API's dllexport/dllimport switching.
# See the workspace's notes/suite/WINDOWS-TODO.md item 6.
else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)  # 64-bit Windows
	OS_NAME := Windows
	LIB_EXTENSION := dll
	OS_SPECIFIC_CXX_FLAGS := -shared
	OS_SPECIFIC_LIBRARY_NAME_FLAG := -Wl,--out-implib,$(APP_DIR)/$(BASE_NAME_PREFIX).dll.a
	TARGET := $(BASE_NAME_PREFIX).dll
	EXE_EXTENSION := .exe
	# Additional Windows-specific variables
	# This is the path to the pkg-config files on MSYS2
	PKG_CONFIG_PATH := /mingw64/lib/pkgconfig
	INCLUDE_INSTALL_PATH := /mingw64/include
	LIB_INSTALL_PATH := /mingw64/lib
	BIN_INSTALL_PATH := /mingw64/bin
	# Windows paths for .pc so gcc invoked by mingw can resolve -I/-L (cygpath for MSYS2)
	PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
	PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
	override BUILD := win64/$(BUILD)

else
    $(error Unsupported OS: $(UNAME_S))

endif

# ---------------------------------------------------------------------------
# Installation prefix
#
# Defaults to the system location chosen above. Override it to install
# somewhere else - the suite's bootstrap installs every library into a local
# prefix so that each build resolves its dependencies through pkg-config,
# exactly as a consumer would, rather than through a second code path that
# only in-tree builds exercise. See CONVENTIONS.md section 1.
#
#     make install PREFIX=/path/to/prefix
# ---------------------------------------------------------------------------
ifdef PREFIX
INCLUDE_INSTALL_PATH := $(PREFIX)/include
LIB_INSTALL_PATH := $(PREFIX)/lib
BIN_INSTALL_PATH := $(PREFIX)/bin
PKG_CONFIG_PATH := $(PREFIX)/share/pkgconfig
ifeq ($(OS_NAME), Windows)
PC_INCLUDE_DIR = $(shell cygpath -m $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH))
PC_LIB_DIR = $(shell cygpath -m $(LIB_INSTALL_PATH)/$(SUITE))
else
PC_INCLUDE_DIR := $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
PC_LIB_DIR := $(LIB_INSTALL_PATH)/$(SUITE)
endif
# A non-system prefix has no /etc/ld.so.conf.d, and writing to it would need
# root anyway. Everything built here carries an rpath to the prefix instead.
LDCONF_INSTALL_PATH :=
endif

# Dependencies are looked up along the inherited PKG_CONFIG_PATH as well as the
# install location chosen above, so that exporting PKG_CONFIG_PATH works as the
# errors below say it does. The inherited value comes first: it is an explicit
# request for this build, where the install location may be only a default.
PKG_CONFIG_LOOKUP_PATH := $(if $(PKG_CONFIG_PATH_ENV),$(PKG_CONFIG_PATH_ENV):)$(PKG_CONFIG_PATH)


# The optimization level is the one thing that distinguishes the two builds'
# compile flags. `release` is what gets installed and what anything linking
# against this library actually runs, so it is compiled for speed; `debug` is
# compiled for stepping through. -g stays in both, because a release build
# that cannot be read in a debugger is a release build nobody can diagnose,
# and the symbols cost only file size.
#
# The sanitizer builds put $(SAN_OPT_CFLAGS) after this one and `make
# coverage` its own -O0, both by appending, since the last -O wins. That the
# gate does not simply inherit this level is a decision with a reason, and the
# reason is written beside SAN_OPT_CFLAGS rather than here.
# The C++ level tracks it, for the tests and nothing else - no shipped object
# is compiled by $(CXX). It is a separate variable because the two answers
# differ: the release C++ is -O1, not -O2, because gtest at -O2 costs build
# time for test code whose speed nobody ships.
#
# Measured before choosing, on the exhaustive civil sweep (73,048,866 days,
# the dominant test in the suite):
#
#          runtime   breakpoint in the loop body   locals readable
#   -O0    17.39 s   fires                         5
#   -O1     6.66 s   set, never fires              -
#   -Og     6.93 s   set, never fires              -
#
# -Og is the level documented for debugging and it buys nothing here: same
# speed as -O1 and the same dead breakpoint. The line tables are near-identical
# at all three (375/377/377 distinct lines), so the usual "you cannot set a
# breakpoint" is not the failure - the failure is subtler and worse, that gdb
# accepts the breakpoint and the address it picks never executes.
#
# So -O0 for debug, paying 2.6x on the one build whose entire purpose is
# stepping through something, and nothing on the release build everybody runs.
# GCHRON_CIVIL_SWEEP_YEARS narrows the sweep when even that is too slow.
ifeq ($(BUILD),debug)
OPT_CFLAGS := -O0
OPT_CXXFLAGS := -O0
else
OPT_CFLAGS := -O2
OPT_CXXFLAGS := -O1
endif

# Strict aliasing, named rather than inherited from the -O level, and armed in
# every build rather than swept for separately.
#
# Measured on gcc 14.2, planted violation, counting findings:
#
#   -fsyntax-only                 0     -O1 -c                        0
#   -O0 -c                        0     -O1 -fstrict-aliasing -c      1
#   -O0 -fstrict-aliasing -c      1     -O2 -c                        1
#
# What arms the warning is -fstrict-aliasing, which gcc turns on from -O2 and
# not before. "It needs the optimizer" describes gcc's default, not the
# mechanism, and a gate resting on that proxy goes silent the moment somebody
# changes an -O for an unrelated reason. Named explicitly, the level stops
# mattering: it warns at -O0 too.
#
# Level 1 rather than the level 3 -Wall implies: on libs/model's eleven real
# violations, level 3 found ZERO and levels 1 and 2 found all eleven, with
# level 1 additionally catching a minimal pair level 2 misses. Level 1 or
# nothing. A future false positive is a finding to explain here, not a level
# to lower quietly.
#
# In CFLAGS rather than in a sweep of its own, which is libs/model's placement
# and better than the one this library had: a flag under -Werror has no
# separate green light to give. A sweep can fail to look - a broken include
# path yields errors rather than warnings, and finding no warnings then reads
# as finding no violations - and every such hole has to be tested for
# separately. Here a source that will not compile fails the build. What is
# left for check-aliasing is the one thing CFLAGS cannot prove about itself:
# that the flags are still armed.
#
# This is the one undefined-behaviour class with no runtime gate at all: ASan
# and UBSan catch a strict-aliasing violation at NO optimization level, so the
# sanitizer build and the fuzzers are blind to it however they are compiled.
# A static warning is the only instrument there is, and it is partial - it
# will not follow a violation laundered through a function boundary.
# Split, because the two halves travel to different places. The assumption
# belongs in every tree that compiles the library; the diagnostic belongs where
# somebody will read it. The fuzz line carries -w and is not a warning gate.
#
# Naming the assumption is worth doing even where it changes nothing, because
# the DEFAULT differs between the two compilers these Makefiles drive, and it
# appeared on no command line:
#
#   gcc 14.2     -O0 off   -O1 off   -O2 ON
#   clang 19.1.7 -O0 off   -O1 ON    -O2 ON
#
# Measured by diffing emitted code for a minimal pair, not read off a manual
# page. So the ASan build - gcc at -O1 - was compiling the library WITHOUT the
# assumption the shipped -O2 library is built under, and now does not. The
# fuzz tree is clang at -O1 and was already under it: naming the flag there
# changed 0 of 40 objects, where disabling it changes 24. FUZZ_CC is
# overridable, so a fuzz tree driven by gcc would have differed; the name is
# what makes that not matter.
#
# Those two figures are clang's, and the ASan tree is gcc, so here is that
# one. Naming the flag, gcc, this library's 41 objects, comparing disassembly:
#
#   -O0   off vs on                      0 of 41
#   -O1   off vs on                     11 of 41     <- the ASan and gcc-fuzz trees
#   -O2   off vs on                      0 of 41     <- already the default
#   -O2   -fno- vs -f  (control)        11 of 41
#   -O1   -fno- vs -f  (control)        11 of 41
#
# So both shipped configurations are free - release is -O2 where it is the
# default, debug is -O0 where it changes nothing - and the flag is load-bearing
# codegen only at -O1, which is exactly where the sanitizer and fuzz trees sit.
# That is deliberate and is the reason they name it, but it had no number
# against it until a peer measured the same thing in another library and got
# 30 of 76. Their figure does not transfer and neither does mine: measure it
# per library, in the compiler the tree actually uses.
#
# Two ways to get this comparison wrong, both hit here first. Compare
# DISASSEMBLY, not object bytes - debug info records the command line, so any
# flag change makes every object differ. And objdump prints the file name in
# its header, so comparing its raw output reports 41 of 41 three times, which
# reads like a finding rather than like a broken instrument; drop the header.
# The -O2 -fno-/-f row is not decoration - without a comparison demonstrably
# able to see a change, the two zeros above are indistinguishable from a
# comparison that sees nothing.
ALIASING_ASSUME_CFLAGS := -fstrict-aliasing
#
# The level is named because -Wall already sets one: `gcc -Q --help=warnings
# -Wall` reports -Wstrict-aliasing=3, and level 3 is silent on a plain
# type-punned dereference that level 1 rejects. So -Wall at -O2 gives
# -fstrict-aliasing the optimisation with no warning behind it - the
# assumption armed and the guard absent.
#
# Precedence is not positional against -Wall. An explicit level beats -Wall's
# implicit 3 from either side, so where $(ALIASING_CFLAGS) sits relative to
# -Wall does not matter; "last one wins" holds only between two EXPLICIT
# levels. Measured both ways, -Q and a planted violation at -O2:
#
#   -Wall                                     3   silent
#   -Wall -Wstrict-aliasing=1                 1   warns
#   -Wstrict-aliasing=1 -Wall                 1   warns
#   -Wall -Wstrict-aliasing=1 ...=3           3   silent
#   -Wall -Wstrict-aliasing=3 ...=1           1   warns
#
# So the disarm vector is a later explicit level, and CFLAGS ends with
# $(EXTRA_CFLAGS): `make EXTRA_CFLAGS=-Wstrict-aliasing=3` builds at level 3
# with every flag still present and every sentence describing them still
# true. check-aliasing catches exactly that, because it compiles its planted
# violation with the real $(CFLAGS) and so sees the resolved level rather
# than the spelling - verified by running it that way. Read the level with
# -Q; the flag list cannot be interpreted by eye.
#
# Level 1 rather than 3 costs nothing here only because chron does not build
# on a common first member: all 41 objects compile clean at level 1 under
# -Werror. libs/ctang measured 669 diagnostics across 48 of 62 TUs at the
# same level, every one a downcast to a struct's initial member that
# C17 6.7.2.1p15 makes well defined. Where that is the architecture this gate
# cannot be coverage, only a statement that gcc's level 1 still works.
ALIASING_WARN_CFLAGS := -Wstrict-aliasing=1
ALIASING_CFLAGS := $(ALIASING_ASSUME_CFLAGS) $(ALIASING_WARN_CFLAGS)

CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c++20 $(OPT_CXXFLAGS) -g $(EXTRA_CXXFLAGS)
CC := cc
CFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c17 $(OPT_CFLAGS) $(ALIASING_CFLAGS) -g $(EXTRA_CFLAGS)
# Library-specific compile flags (export symbols on Windows, PIC on Linux)
# GCHRON_BUILD enables DLL export on Windows (checked by GCHRON_API macro)
# GCHRON_TEST_BUILD enables export of internal functions for testing (checked by GCHRON_INTERNAL_API macro)
# No -DGCHRON_TEST_BUILD: the shipped library exports its public API and nothing
# else. Tests reach the internals by linking the static archive, which a static
# link can do even for hidden symbols.
LIB_CFLAGS := $(CFLAGS) -fvisibility=hidden -DGCHRON_BUILD $(EXTRA_CFLAGS)
LDFLAGS := -L /usr/lib -lstdc++ -lm $(EXTRA_LDFLAGS)

# Repeated *after* the static archive on every link that uses one. Archive
# order matters where shared-object order does not: -lm in LDFLAGS sits before
# the archive, so an object pulled out of the archive afterwards - interop.c's
# floor() - has nothing left to resolve against. The shared library does not
# have the problem, because its own link records libm as a dependency, which
# is why this only ever shows up in a test, an example or an oracle driver.
STATIC_LINK_LIBS := -lm
ifdef PREFIX
# So that a library, a test or an example finds its Ghoti.io dependencies in the
# prefix at run time without LD_LIBRARY_PATH.
LDFLAGS += -Wl,-rpath,$(LIB_INSTALL_PATH)/$(SUITE)
endif

BUILD_DIR := ./build/$(BUILD)
OBJ_DIR := $(BUILD_DIR)/objects
FLAGS_STAMP := $(OBJ_DIR)/.flags
LINK_FLAGS_STAMP := $(OBJ_DIR)/.linkflags
GEN_DIR := $(BUILD_DIR)/generated
APP_DIR := $(BUILD_DIR)/apps


# Add OS-specific flags
ifeq ($(UNAME_S), Linux)
	LIB_CFLAGS += -fPIC

else ifeq ($(UNAME_S), Darwin)

else ifeq ($(findstring MINGW32_NT,$(UNAME_S)),MINGW32_NT)  # 32-bit Windows

else ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)  # 64-bit Windows

else
	$(error Unsupported OS: $(UNAME_S))

endif

# The standard include directories for the project.
INCLUDE := -I include/ -I $(GEN_DIR)/

# Goals that compile and link nothing.  A missing sibling library must not stop
# them: `make docs` needs doxygen and the tracked sources, not cutil, and it
# was failing at parse time - before doxygen was ever reached - on any machine
# where the suite is not installed.  Every other goal still gets the hard
# error below, which is the point of having no fallback.
DEPLESS_GOALS := docs docs-pdf clean fuzz-clean cloc help vectors vectors-jsonschema \
	check-layering
ifeq ($(filter-out $(DEPLESS_GOALS),$(or $(MAKECMDGOALS),all)),)
SKIP_DEP_CHECK := 1
endif

# ghoti.io-cutil, for the allocator vtable, the growable array, and the
# overflow-checked size math. Prefer pkg-config; fall back to a sibling
# checkout. The name must carry $(BRANCH): cutil installs its .pc as
# ghoti.io-cutil-dev.pc, so asking for "ghoti.io-cutil" never matches.
CUTIL_PC ?= ghoti.io-cutil$(BRANCH)
CUTIL_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags $(CUTIL_PC) 2>/dev/null)
CUTIL_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs $(CUTIL_PC) 2>/dev/null)
# Use the sibling path when pkg-config failed (empty) or returned an
# unsubstituted placeholder from the .pc template.
ifeq ($(strip $(CUTIL_CFLAGS)),)
ifndef SKIP_DEP_CHECK
$(error ghoti.io-cutil was not found by pkg-config. Run ./bootstrap.sh in the parent folder to build and install the suite into a local prefix, then pass the same PREFIX here - or point PKG_CONFIG_PATH at the directory holding its .pc file. There is deliberately no sibling-checkout fallback: a second resolution path that only in-tree builds exercise is one that silently rots.)
endif
endif
INCLUDE += $(CUTIL_CFLAGS)

# Automatically collect all .c source files under the src directory.
#
# The embedded time-zone table is generated, not committed: it is 1.9MB of
# TZif images written out as C, and regenerating it takes under a second on
# any machine that has a zoneinfo directory. `find` runs when this file is
# read, which is before the generator has run, so it is named explicitly here
# rather than discovered.
#
EMBEDDED_TZDATA := src/zone/tzdata_embedded.c

#
# The Windows zone mapping is generated from CLDR's windowsZones.xml, and
# unlike the tzdata table it *is* committed: it is ten kilobytes, it changes
# about as often as Windows adds a time zone, and regenerating it needs the
# network - so a build that had to fetch it would be a build that fails
# without one. If it is missing, the build says what to run rather than
# quietly producing a library whose Windows branch cannot work.
#
WINDOWS_ZONES := src/zone/windows_zones.c

# Where tools/tzdata/fetch-cldr.sh leaves the mapping it downloads. Not
# committed: it is CLDR's data, reproducible from a URL and tools/tzdata/CLDR_TAG.
CLDR_ZONES_XML := third_party/cldr/windowsZones.xml
#
# Guarded on the goal, because this is evaluated when the Makefile is read and
# an unguarded $(error) would break `make clean` and `make help` too - the two
# things someone whose tree is in a strange state is most likely to reach for.
#
ifeq ($(wildcard $(WINDOWS_ZONES)),)
ifeq ($(filter clean help embed-windows-zones,$(MAKECMDGOALS)),)
$(error $(WINDOWS_ZONES) is missing. Run tools/tzdata/fetch-cldr.sh and then \
	make embed-windows-zones)
endif
endif

SOURCES := $(sort $(shell find src -type f -name '*.c') $(EMBEDDED_TZDATA))

#
# TZDATA_DIR is where the generator reads from. A machine with no zoneinfo
# directory - which on Windows is every machine - passes one:
#     make TZDATA_DIR=/path/to/unpacked/tzdata
# The generator fails rather than writing an empty table (CONVENTIONS.md
# section 6): an embedded database with no zones answers every lookup with
# GCHRON_ERR_UNSUPPORTED and is indistinguishable from a working one that was
# asked for a zone it does not have.
#
TZDATA_DIR ?=


# Convert each source file path to an object file path.
LIBOBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

TESTFLAGS := `PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs --cflags gtest`

# TESTFLAGS is backticks the *shell* expands when a recipe runs, so a stamp
# recording $(TESTFLAGS) records that fixed string and nothing else: upgrade
# gtest and the flags move while the stamp stays byte for byte identical.
# Ask pkg-config at make time for the value the stamp is meant to watch.
TESTFLAGS_RESOLVED := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs --cflags gtest 2>/dev/null)

# The checks `make test` runs besides the tests themselves. Named in a
# variable so that a build which cannot satisfy them can clear it: the
# coverage target does, because --coverage links the gcov runtime, whose
# mangle_path check-symbols is right to reject in a shipping library and
# wrong to reject in an instrumented one. Spelled as text's TEST_GATES is.
#
# check-generated is here rather than left as a target somebody remembers to
# type, because a gate wired to a target nobody types is one that does not
# run. It costs about a second and reports rather than fails when the input
# a generator needs is not on the machine.
TEST_GATES ?= check-symbols check-layering check-aliasing check-stamps \
	check-generated check-docs check-license check-counts

#
# A check whose tool is missing prints a line and passes, which on one
# machine where everything is installed is a convenience. It stops being one
# the moment there is a second machine: a Windows box has no `doxygen` and may
# have no `python3`, and would report a clean run with no way to tell "passes
# there" from "never asked there". The default stays lenient; `make test-full`
# sets this, and a release is measured with it.
#
# notes/suite/SUITE-TODO.md item 17.
#
REQUIRE_ORACLES ?=
SKIP_EXIT := $(if $(REQUIRE_ORACLES),1,0)
SKIP_NOTE := $(if $(REQUIRE_ORACLES),REQUIRE_ORACLES is set - a check that cannot run is a failure.,)

####################################################################
# How wide the exhaustive civil sweep runs
####################################################################
#
# tests/unit/test_civil.cpp sweeps every epoch day over +/-100,000 years -
# 73 million days - checking that the Gregorian labelling round-trips and
# steps exactly one day at a time. That is six seconds in an ordinary build
# and design.md section 12 asks for it, so `make test` runs it in full, and
# so does `make test-asan`: UBSan watching 73 million conversions is the gate
# that proves there is no signed overflow in them.
#
# Under Valgrind the same sweep runs for the better part of an hour, which is
# not a gate anybody runs. Valgrind is looking for memory errors, and this
# test touches no heap at all - the same code paths are exercised by a far
# narrower sweep - so the Valgrind targets narrow it, deliberately and in one
# visible place. The test *prints how far it swept* in its own output, so a
# narrowed run says so rather than passing quietly at full width.
VALGRIND_SWEEP_YEARS ?= 2000

# Valgrind flags (exclude "still reachable" as it's not a leak)
VALGRIND_FLAGS := --leak-check=full --show-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=1

####################################################################
# Test discovery
####################################################################

# Optional shared test helper (if present).
TEST_HELPER_SRC := $(wildcard tests/test_helpers.cpp)
TEST_HELPER_OBJ := $(patsubst tests/%.cpp,$(OBJ_DIR)/tests/%.o,$(TEST_HELPER_SRC))

# The static archive, not -l: a static link resolves hidden symbols, so the
# tests can exercise internals the shared library does not export.
# --whole-archive because anything registering itself from a constructor is
# otherwise dropped - a plain archive link only pulls in object files that
# something references by name.
CHRONLIBRARY := -Wl,--whole-archive $(APP_DIR)/$(STATIC_TARGET) -Wl,--no-whole-archive $(CUTIL_LIBS)

# Discover test sources and compute an executable name for each, as
# "path|name" pairs. test_foo.cpp -> testFoo.
TEST_PAIRS := $(shell find tests -type f -name 'test_*.cpp' 2>/dev/null | sort | grep -v test_helpers | while read f; do \
	echo "$$f|$$(basename "$$f" .cpp | sed 's/test_/test/; s/^test\([a-z]\)/test\U\1/')"; done)
TEST_SOURCES := $(foreach pair,$(TEST_PAIRS),$(word 1,$(subst |, ,$(pair))))
TEST_NAMES := $(foreach pair,$(TEST_PAIRS),$(word 2,$(subst |, ,$(pair))))
TEST_EXECUTABLES := $(addprefix $(APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))

# Automatically collect all example .c files.
EXAMPLE_SOURCES := $(shell find examples -type f -name '*.c' 2>/dev/null)
EXAMPLES := $(patsubst examples/%.c,$(APP_DIR)/examples/%$(EXE_EXTENSION),$(EXAMPLE_SOURCES))

# Where the test fixtures live. Tests run from build/.../apps, so the path is
# baked in at compile time.
CHRON_ROOT := $(CURDIR)
TEST_DATA := $(CURDIR)/tests/data

all: $(APP_DIR)/$(TARGET) $(APP_DIR)/$(STATIC_TARGET) ## Build shared + static libraries

$(EMBEDDED_TZDATA): tools/tzdata/embed.py
	@printf "\n### Generating the embedded time-zone table ###\n"
	python3 tools/tzdata/embed.py $(TZDATA_DIR) -o $@

embed-tzdata: ## Regenerate the embedded time-zone table
embed-tzdata:
	@rm -f $(EMBEDDED_TZDATA)
	@$(MAKE) --no-print-directory $(EMBEDDED_TZDATA)

#
# The leap-second table is generated too, but committed, and refreshed only
# when someone runs this. It is three kilobytes, it changes about once a
# decade, and it carries an expiry date past which every conversion through it
# reports GCHRON_ERR_EXPIRED - so regenerating it as a side effect of building
# would move that date to whatever the build machine happened to have, which
# is exactly the sort of change that should be looked at rather than absorbed.
#
embed-leap: ## Refresh the built-in leap-second table from leap-seconds.list
embed-leap:
	python3 tools/leap/embed.py

#
# The Windows zone mapping needs CLDR data the build never fetches. Run
# tools/tzdata/fetch-cldr.sh once, then this.
#
embed-windows-zones: ## Regenerate the Windows zone mapping from fetched CLDR data
embed-windows-zones:
	python3 tools/tzdata/windows_zones.py $(CLDR_ZONES_XML)

#
# A generated source and its generator can drift apart, and nothing notices:
# the file compiles, the tests pass, and the committed bytes are simply no
# longer what the tool would produce. CONVENTIONS.md section 8 names this as
# the gap here - `regex` and `text` each have a check-*-tables target and this
# library had none - and the licensing work made it sharper, because a
# generated source now carries its SPDX block from its generator too. A header
# edited by hand in the file rather than in the tool disappears at the next
# regeneration.
#
# Only the two *committed* generated sources are checkable.
# src/zone/tzdata_embedded.c is gitignored and rebuilt by the build itself, so
# there are no committed bytes for it to drift from.
#
# Each generator needs an input the repository does not carry, so each half
# can be checked only where that input is present. A missing input is reported
# and counted rather than passed over, and the summary says how many of the
# two were actually compared - "0 of 2" should not look like success.
#
check-generated: ## Fail if a committed generated source is not what its generator produces
	@if ! command -v python3 >/dev/null 2>&1; then \
		printf "check-generated: skipped entirely (no python3). $(SKIP_NOTE)\n" >&2; \
		exit $(SKIP_EXIT); \
	fi; \
	tmp=$$(mktemp -d) || exit 1; \
	trap 'rm -rf "$$tmp"' EXIT; \
	checked=0; \
	stale=""; \
	skipped=""; \
	leap_source="$${TZDIR:-/usr/share/zoneinfo}/leap-seconds.list"; \
	if [ ! -f "$$leap_source" ]; then \
		printf "  src/leap/leap_builtin.c      not checked: no $$leap_source (it ships with tzdata)\n"; \
		skipped="$$skipped src/leap/leap_builtin.c"; \
	else \
		if ! python3 tools/leap/embed.py "$$leap_source" -o "$$tmp/leap_builtin.c" \
				>/dev/null 2>"$$tmp/err"; then \
			printf "\033[0;31m\n### tools/leap/embed.py failed ###\033[0m\n" >&2; \
			cat "$$tmp/err" >&2; \
			exit 1; \
		fi; \
		checked=$$((checked + 1)); \
		if ! diff -u src/leap/leap_builtin.c "$$tmp/leap_builtin.c" \
				>"$$tmp/leap.diff" 2>&1; then \
			stale="$$stale src/leap/leap_builtin.c"; \
			printf "\033[0;31m\n### src/leap/leap_builtin.c is not what tools/leap/embed.py produces ###\033[0m\n" >&2; \
			head -40 "$$tmp/leap.diff" >&2; \
		else \
			printf "  src/leap/leap_builtin.c      matches tools/leap/embed.py\n"; \
		fi; \
	fi; \
	if [ ! -f "$(CLDR_ZONES_XML)" ]; then \
		printf "  src/zone/windows_zones.c     not checked: no $(CLDR_ZONES_XML) (run tools/tzdata/fetch-cldr.sh)\n"; \
		skipped="$$skipped src/zone/windows_zones.c"; \
	else \
		if ! python3 tools/tzdata/windows_zones.py "$(CLDR_ZONES_XML)" \
				-o "$$tmp/windows_zones.c" >/dev/null 2>"$$tmp/err"; then \
			printf "\033[0;31m\n### tools/tzdata/windows_zones.py failed ###\033[0m\n" >&2; \
			cat "$$tmp/err" >&2; \
			exit 1; \
		fi; \
		checked=$$((checked + 1)); \
		if ! diff -u src/zone/windows_zones.c "$$tmp/windows_zones.c" \
				>"$$tmp/zones.diff" 2>&1; then \
			stale="$$stale src/zone/windows_zones.c"; \
			printf "\033[0;31m\n### src/zone/windows_zones.c is not what tools/tzdata/windows_zones.py produces ###\033[0m\n" >&2; \
			head -40 "$$tmp/zones.diff" >&2; \
		else \
			printf "  src/zone/windows_zones.c     matches tools/tzdata/windows_zones.py\n"; \
		fi; \
	fi; \
	if [ -n "$$stale" ]; then \
		printf "\033[0;31m\nStale:$$stale. Regenerate with make embed-leap / make embed-windows-zones.\033[0m\n" >&2; \
		exit 1; \
	fi; \
	if [ -n "$$skipped" ] && [ "$(SKIP_EXIT)" != 0 ]; then \
		printf "\033[0;31mcheck-generated: not checked:$$skipped. $(SKIP_NOTE)\033[0m\n" >&2; \
		exit 1; \
	fi; \
	printf "\033[0;32m$$checked of 2 committed generated sources match their generators.\033[0m\n"

####################################################################
# Dependency Inclusion
####################################################################

TEST_DEPFILES := $(foreach pair,$(TEST_PAIRS),$(OBJ_DIR)/tests/$(basename $(notdir $(word 1,$(subst |, ,$(pair))))).d)
DEPFILES := $(LIBOBJECTS:.o=.d) $(TEST_HELPER_OBJ:.o=.d) $(TEST_DEPFILES)
-include $(DEPFILES)


####################################################################
# Object Files
####################################################################

####################################################################
# Generated version header
####################################################################

LIBVER_GEN := $(GEN_DIR)/ghoti.io/$(PROJECT)/libver_gen.h

# libver_gen.h is regenerated on every build and rewritten only when its content
# changes, so a variable given on the command line - make MAJOR_VERSION=2, or
# make BRANCH=-dev - takes effect. Keying the rule on the Makefile's timestamp
# alone left the previous token and version baked into the build, and nothing
# said so.
.PHONY: force-libver
force-libver:

$(LIBVER_GEN): force-libver
	@if [ -z "$(LIBVER_SYMBOL)" ]; then \
		printf "### LIBVER_SYMBOL is empty ###\n" >&2; \
		printf "Every exported symbol would lose its version namespace.\n" >&2; \
		exit 1; \
	fi
	@mkdir -p $(@D)
	@printf '%s\n' \
		'// Generated by the Makefile. Do not edit; see CONVENTIONS.md section 4.' \
		'#ifndef GHOTI_IO_GCHRON_LIBVER_GEN_H' \
		'#define GHOTI_IO_GCHRON_LIBVER_GEN_H' \
		'' \
		'/** The symbol namespace for this build, from the Makefile'"'"'s BRANCH. */' \
		'#define GHOTIIO_CHRON_NAME $(LIBVER_SYMBOL)' \
		'' \
		'/** Human-readable version of this build. */' \
		'#define GHOTIIO_CHRON_VERSION "$(VERSION_STRING)"' \
		'' \
		'/** The same version as three integers. */' \
		'#define GHOTIIO_CHRON_VERSION_MAJOR $(MAJOR_VERSION)' \
		'#define GHOTIIO_CHRON_VERSION_MINOR $(VERSION_MINOR_ONLY)' \
		'#define GHOTIIO_CHRON_VERSION_PATCH $(VERSION_PATCH_ONLY)' \
		'' \
		'#endif // GHOTI_IO_GCHRON_LIBVER_GEN_H' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(OBJ_DIR)/%.o: src/%.c $(FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	$(CC) $(LIB_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

####################################################################
# Shared Library
####################################################################

$(APP_DIR)/$(TARGET): $(LIBOBJECTS) $(LINK_FLAGS_STAMP)
	@printf "\n### Compiling Chron Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(LIBOBJECTS) $(LDFLAGS) $(CUTIL_LIBS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)

ifeq ($(OS_NAME), Linux)
	@ln -f -s $(TARGET) $(APP_DIR)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(APP_DIR)/$(BASE_NAME)
endif

####################################################################
# Static Library
####################################################################

$(APP_DIR)/$(STATIC_TARGET): $(LIBOBJECTS)
	@printf "\n### Archiving Static Chron Library ###\n"
	@mkdir -p $(@D)
	@rm -f $@
	ar rcs $@ $^

####################################################################
# Unit Tests
####################################################################

ifneq ($(TEST_HELPER_SRC),)
$(TEST_HELPER_OBJ): $(TEST_HELPER_SRC) $(FLAGS_STAMP)
	@printf "\n### Compiling Test Helper ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@
endif

# Test sources live in tests/ and tests/unit/; the object name comes from the
# basename either way, so the executable name matches.
$(OBJ_DIR)/tests/%.o: tests/%.cpp $(FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(OBJ_DIR)/tests/%.o: tests/unit/%.cpp $(FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(OBJ_DIR)/tests/%.o: tests/conformance/%.cpp $(FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# Build rule for one test executable. $1 = source path, $2 = executable name.
# Tests compile to .o first and link separately, so a library change relinks
# without recompiling every test.
#
# The archive is a normal prerequisite because that is what the link line
# uses, and a change to the library therefore relinks the tests. Naming only
# the shared library here - which is what this rule used to do - left nothing
# in the chain that builds the archive, so `make test` failed outright on a
# clean tree and raced under -j. The .so is order-only: it is not linked, but
# check-symbols and the test run both want it built.
define test-executable-rule
TEST_OBJ_$1 := $(OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(APP_DIR)/$2$(EXE_EXTENSION): $$(TEST_OBJ_$1) $(TEST_HELPER_OBJ) \
		$(APP_DIR)/$(STATIC_TARGET) $(LINK_FLAGS_STAMP) | $(APP_DIR)/$(TARGET)
	@printf "\n### Linking Test: $2 ###\n"
	@mkdir -p $$(@D)
	$(CXX) $(CXXFLAGS) -o $$@ $$(TEST_OBJ_$1) $(TEST_HELPER_OBJ) $(LDFLAGS) $(CHRONLIBRARY) $(CUTIL_LIBS) $(TESTFLAGS) $(STATIC_LINK_LIBS)
endef

$(foreach pair,$(TEST_PAIRS),\
	$(eval $(call test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

####################################################################
# Examples
####################################################################

# Links the archive, so it depends on the archive; see test-executable-rule.
$(APP_DIR)/examples/%$(EXE_EXTENSION): examples/%.c $(APP_DIR)/$(STATIC_TARGET) \
		$(LINK_FLAGS_STAMP) | $(APP_DIR)/$(TARGET)
	@printf "\n### Compiling Example: $* ###\n"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(CHRONLIBRARY) $(CUTIL_LIBS) $(STATIC_LINK_LIBS)

####################################################################
# Commands
####################################################################

# General commands
.PHONY: clean cloc docs docs-pdf examples coverage check-symbols check-layering
.PHONY: vectors vectors-jsonschema vectors-zones vectors-calendar vectors-calendars
.PHONY: tools check-oracle-zoneinfo check-oracle-ldml check-oracle-ldml-parse check-generated check-docs check-license check-counts
.PHONY: check-aliasing check-stamps
.PHONY: check-oracle-temporal check-oracles test-full
# Release build commands
.PHONY: all install test test-quiet test-asan test-valgrind test-valgrind-quiet test-watch uninstall watch
# Debug build commands
.PHONY: all-debug install-debug test-debug test-valgrind-debug test-watch-debug uninstall-debug watch-debug
# Fuzz commands
.PHONY: fuzz fuzz-clean fuzz-seed

watch: ## Watch the file directory for changes and compile the target
	@while true; do \
		make --no-print-directory all; \
		printf "\033[0;32m\n"; \
		printf "#########################\n"; \
		printf "# Waiting for changes.. #\n"; \
		printf "#########################\n"; \
		printf "\033[0m\n"; \
		inotifywait -qr -e modify -e create -e delete -e move src include tests Makefile --exclude '/\.'; \
		done

test-watch: ## Watch the file directory for changes and run the unit tests
	@while true; do \
		make --no-print-directory all; \
		make --no-print-directory test; \
		printf "\033[0;32m\n"; \
		printf "#########################\n"; \
		printf "# Waiting for changes.. #\n"; \
		printf "#########################\n"; \
		printf "\033[0m\n"; \
		inotifywait -qr -e modify -e create -e delete -e move src include tests Makefile --exclude '/\.'; \
		done

examples: ## Build all examples
examples: $(APP_DIR)/$(TARGET) $(EXAMPLES)
	@printf "\033[0;32m\n"
	@printf "############################\n"
	@printf "### Examples built       ###\n"
	@printf "############################\n"
	@printf "\033[0m\n"
	@printf "Examples are available in: $(APP_DIR)/examples/\n"
	@printf "\n"
	@printf "\033[0;33mTo run examples:\033[0m\n"
ifeq ($(OS_NAME), Linux)
	@printf "  Linux: Set LD_LIBRARY_PATH to include the library directory:\n"
	@printf "    export LD_LIBRARY_PATH=\"$(TEST_LD_PATH)\"\n"
	@printf "    $(APP_DIR)/examples/<example>\n"
else ifeq ($(OS_NAME), Mac)
	@printf "  macOS: Set DYLD_LIBRARY_PATH to include the library directory:\n"
	@printf "    export DYLD_LIBRARY_PATH=\"$(TEST_LD_PATH)\"\n"
	@printf "    $(APP_DIR)/examples/<example>\n"
else ifeq ($(OS_NAME), Windows)
	@printf "  Windows (MSYS2): The DLL must be in the same directory or in PATH.\n"
	@printf "    cd $(APP_DIR) && ./examples/<example>$(EXE_EXTENSION)\n"
endif
	@printf "\n"

# So the tests can load the chron library and its dependency on cutil. cutil's
# build tree has no release/debug component, so only the leading OS component
# of BUILD applies to it.
TEST_LD_PATH := $(APP_DIR):$(LIB_INSTALL_PATH)/$(SUITE)

####################################################################
# Symbol namespace check
####################################################################

check-symbols: ## Fail if any exported symbol lacks the version namespace
check-symbols: $(APP_DIR)/$(TARGET)
ifeq ($(OS_NAME), Linux)
	@leaked=$$(nm -D --defined-only $(APP_DIR)/$(TARGET) \
		| awk '$$2 ~ /^[TDBR]$$/ {print $$3}' \
		| grep -v '^$(LIBVER_SYMBOL)_' | grep -v '^_' || true); \
	if [ -n "$$leaked" ]; then \
		printf "\033[0;31m\n### Exported symbols missing the $(LIBVER_SYMBOL)_ namespace ###\033[0m\n" >&2; \
		printf "%s\n" "$$leaked" >&2; \
		printf "\nEach needs a '#define <name> GHOTIIO_CHRON(<name>)' line in namespace.h.\n" >&2; \
		printf "See CONVENTIONS.md section 4.\n" >&2; \
		exit 1; \
	fi
	@unexported=$$(find include -name '*.h' -exec awk '/^#if DOXYGEN/{d=1} d==0 && /^[a-z_][A-Za-z0-9_ ]*\**[[:space:]]*gchron_[a-z0-9_]+[[:space:]]*\(/{print FILENAME": "$$0} /^#endif/{d=0}' {} + \
		| grep -vE 'typedef|static inline' || true); \
	if [ -n "$$unexported" ]; then \
		printf "\033[0;31m\n### Public declarations without GCHRON_API ###\033[0m\n" >&2; \
		printf "%s\n" "$$unexported" >&2; \
		printf "\nThese are hidden in the shared library. The tests link the archive and\n" >&2; \
		printf "would not notice; a consumer gets an undefined reference.\n" >&2; \
		exit 1; \
	fi
	@split=$$(nm -D --undefined-only $(APP_DIR)/$(TARGET) \
		| awk '{print $$2}' | grep '^$(LIBVER_SYMBOL)_' || true); \
	if [ -n "$$split" ]; then \
		printf "\033[0;31m\n### Renamed but undefined - a split symbol ###\033[0m\n" >&2; \
		printf "%s\n" "$$split" >&2; \
		printf "\nA translation unit referenced the namespaced name while the one that\n" >&2; \
		printf "defines it did not see the rename - usually an internal header that\n" >&2; \
		printf "declares or defines something without including macros.h first.\n" >&2; \
		exit 1; \
	fi
	@nomacros=$$(find include src -name '*.h' \
		! -name 'libver.h' ! -name 'libver_gen.h' ! -name 'namespace.h' ! -name 'macros.h' \
		-exec grep -L '#include <ghoti.io/chron/macros.h>' {} + || true); \
	if [ -n "$$nomacros" ]; then \
		printf "\033[0;31m\n### Headers that do not include macros.h ###\033[0m\n" >&2; \
		printf "%s\n" "$$nomacros" >&2; \
		printf "\nEvery header must include <ghoti.io/chron/macros.h> before it declares\n" >&2; \
		printf "anything, so that the renames in namespace.h are already in effect. A\n" >&2; \
		printf "header that skips it can name a type before that type has been renamed,\n" >&2; \
		printf "producing two different types under one spelling.\n" >&2; \
		printf "See CONVENTIONS.md section 4.\n" >&2; \
		exit 1; \
	fi
	@badguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
		| awk '$$1 !~ /^GHOTI_IO_GCHRON_/ {print $$1}' || true); \
	if [ -n "$$badguards" ]; then \
		printf "\033[0;31m\n### Include guards with the wrong prefix ###\033[0m\n" >&2; \
		printf "%s\n" "$$badguards" >&2; \
		printf "\nGuards mirror the path: GHOTI_IO_GCHRON_<PATH>_H. A guard without the\n" >&2; \
		printf "library token is one rename away from colliding with another library's.\n" >&2; \
		exit 1; \
	fi
	@dupguards=$$(find include src -name '*.h' -exec awk 'FNR==1{d=0} !d && /^#ifndef/{print $$2; d=1}' {} + \
		| sort | uniq -d || true); \
	if [ -n "$$dupguards" ]; then \
		printf "\033[0;31m\n### Headers sharing an include guard ###\033[0m\n" >&2; \
		printf "%s\n" "$$dupguards" >&2; \
		printf "\nTwo headers with one guard means whichever is included second is\n" >&2; \
		printf "silently empty. Guards mirror the path: GHOTI_IO_GCHRON_<PATH>_H.\n" >&2; \
		exit 1; \
	fi
	@printf "\033[0;32mEvery exported symbol carries the $(LIBVER_SYMBOL)_ namespace.\033[0m\n"
	@printf "\033[0;32mEvery public declaration carries GCHRON_API.\033[0m\n"
	@printf "\033[0;32mEvery header includes macros.h.\033[0m\n"
	@printf "\033[0;32mEvery include guard is unique and correctly prefixed.\033[0m\n"
else
	@printf "check-symbols: skipped (Linux only)\n"
endif

####################################################################
# Tier layering check
####################################################################
#
# The tier boundary in documentation/design.md section 3 is a dependency
# boundary, and it is the reason `text` can use this library without pulling
# in a single data file:
#
#   tier 0  civil arithmetic and calendars      needs nothing
#   tier 1  instants, durations, offsets, text  needs nothing
#   tier 2  named zones and DST                 needs TZif files
#   tier 3  presentation: patterns and names    needs a names provider
#
# Nothing in tier n may include a header from tier n+1. Enforced rather than
# remembered, because the symptom of breaking it is not a compile error - it
# is a consumer discovering, at link time, that parsing a timestamp now drags
# in a zone database.
#
# The one exception is src/parse/rfc9557.c: RFC 9557's [Europe/Paris] suffix
# resolves against a zone database by definition, and design.md section 13
# marks parse.h "tier 1 (+2 for RFC 9557 zones)".

TIER0_FILES := include/ghoti.io/chron/core.h include/ghoti.io/chron/civil.h \
	include/ghoti.io/chron/calendar.h include/ghoti.io/chron/duration.h \
	src/core/*.c src/core/*.h src/civil/*.c src/civil/*.h
TIER0_FORBIDDEN := chron/(instant|offset|parse|clock|leap|interop|zone|zoned|format)\.h

TIER1_FILES := include/ghoti.io/chron/instant.h include/ghoti.io/chron/offset.h \
	include/ghoti.io/chron/parse.h include/ghoti.io/chron/clock.h \
	include/ghoti.io/chron/leap.h include/ghoti.io/chron/interop.h \
	src/instant/*.c src/instant/*.h src/parse/*.c src/parse/*.h \
	src/clock/*.c src/leap/*.c src/leap/*.h src/interop/*.c
TIER1_FORBIDDEN := chron/(zone|zoned|format)\.h
TIER1_EXEMPT := src/parse/rfc9557.c

TIER2_FILES := include/ghoti.io/chron/zone.h include/ghoti.io/chron/zoned.h \
	src/zone/*.c src/zone/*.h
TIER2_FORBIDDEN := chron/format\.h

# One rule, applied to each tier in turn. Spelled as a macro rather than a
# loop over a packed string: the forbidden pattern is an alternation and so
# contains "|" itself, which a loop that splits on "|" silently cuts in half -
# leaving a check that passes on everything, including a violation. Verified
# by adding an include of instant.h to src/civil/week.c and watching it fail.
#
# $1 = tier number, $2 = forbidden header pattern, $3 = the tier's files.
define layering-check
	@hits=$$(grep -lE '\#include <ghoti\.io/$2>' $3 2>/dev/null \
		| grep -vxF '$(TIER1_EXEMPT)' || true); \
	if [ -n "$$hits" ]; then \
		printf "\033[0;31m\n### Tier $1 includes a higher tier's header ###\033[0m\n" >&2; \
		printf "%s\n" "$$hits" >&2; \
		printf "\nThe tier boundary is the dependency boundary: it is what lets a\n" >&2; \
		printf "consumer parse an RFC 3339 timestamp without a zone database on disk.\n" >&2; \
		printf "See documentation/design.md section 3.\n" >&2; \
		exit 1; \
	fi

endef

check-aliasing: ## Fail if the strict-aliasing warning is no longer armed
# $(ALIASING_CFLAGS) detects the violations; this proves it can still detect
# one. The flags live in CFLAGS under -Werror, so a real violation fails the
# build and no sweep is needed - but a disarmed warning fails nothing and
# looks exactly like a clean library. So compile a planted violation with the
# library's OWN flags and fail if it is accepted.
#
# It is deliberately the real $(CFLAGS) and not a copy: a control compiled
# with flags written out beside it proves those flags work, which is not the
# question.
#
# THE SHAPE OF THE CONTROL IS LOAD-BEARING. What each level diagnoses depends
# on the violation, and only some shapes separate level 1 from the rest.
# Measured, counts of the diagnostic at -O2:
#
#                                              L0  L1  L2  L3
#   *(int *)&obj      known object, in place    0   1   1   1
#   int *p = (int *)&obj; *p                    0   1   1   0
#   *(int *)d         d is a PARAMETER          0   1   0   0
#   int *p = (int *)d; *p   (this control)      0   1   0   0
#   struct-to-struct cast of a parameter        0   1   0   0
#   punning through a void *                    0   0   0   0
#
# Two axes: taking the address of an object gcc can see is what level 2
# needs, and routing the cast through a separate pointer variable is what
# defeats level 3.
#
# THE REQUIREMENT, for anyone changing the level or the control: a control
# for a gate at level N must be caught at N and MISSED at N+1. A control
# that survives into the weaker level still passes after the gate has
# silently fallen back to it, which is indistinguishable from working. This
# control is a parameter cast through a variable - caught at 1, missed at 2 -
# so it certifies level 1 specifically. Raise the level and it must be
# respelled, or the gate passes green while asserting nothing.
#
# Which row to respell it to, if that day comes: the SECOND row, the known
# object through a pointer variable, is the only shape that certifies "2 and
# not 3". The first row is useless as a probe at any level - it fires from 1
# upward and so distinguishes nothing, which is the trap, because it is also
# the most natural way to write a type pun. libs/model's probe is the same
# row as this one, measured, so both gates in the suite certify level 1 and
# both would be vacuous at 2; it is one limitation twice, not a difference
# between them.
#
# So a "simpler" control spelled *(int *)&local would be diagnosed at level 3
# as well, and this gate would pass with the warning at 3 while asserting
# nothing - green, and switched off. Do not simplify it. The last row is the
# standing limit: no level catches punning through a void *, so a clean build
# is not evidence about that class at all.
#
# The warning is a gcc diagnostic. clang accepts -Wstrict-aliasing=1 and
# implements nothing, so `make CC=clang` reaches this gate with the aliasing
# flags visible on every compile line and no aliasing coverage behind them.
# That is a true failure and the gate reports it, but the reason is the
# compiler rather than the flags, so the message names both causes.
check-aliasing: $(LIBVER_GEN)
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' \
		'#include <stdint.h>' \
		'int32_t gchron_alias_control(float * f) {' \
		'  int32_t * p = (int32_t *)f;' \
		'  *f = 1.0f;' \
		'  return *p;' \
		'}' > $(BUILD_DIR)/alias_control.c
# qrc below is read on the same line the compiler runs on, and must stay
# there. Any $(...) evaluated in between - including one building the very
# message that reports the status - replaces $? with the subshell's, and the
# clang branch stops being selected. Adding a substitution to the lines above
# it looks like editing prose. The four-way test catches it, because the
# clang branch is chosen by qrc alone.
	@if $(CC) $(CFLAGS) $(INCLUDE) -fsyntax-only \
			$(BUILD_DIR)/alias_control.c 2> $(BUILD_DIR)/alias_control.log; then \
		qout=$$($(CC) -Q --help=warnings $(CFLAGS) 2>/dev/null); qrc=$$?; \
		lvl=$$(printf '%s\n' "$$qout" \
			| awk '/-Wstrict-aliasing=<0,3>/ { print $$2 }'); \
		printf "\033[0;31mcheck-aliasing: %s accepted a planted type-punning violation, so this build has no aliasing coverage.\033[0m\n" "$$($(CC) --version 2>/dev/null | head -1)" >&2; \
		if [ -z "$$lvl" ] && [ "$$qrc" = "0" ]; then \
			printf '%s\n' \
				'  -Q --help=warnings succeeded and named no -Wstrict-aliasing level at all, which is' \
				'  neither compiler behaviour seen here. Do NOT read this as the clang case: check what' \
				'  CFLAGS was actually passed before concluding anything about the warning.' >&2; \
		elif [ -z "$$lvl" ]; then \
			printf '%s\n' \
				'  This compiler would not report an effective -Wstrict-aliasing level, which gcc gives' \
				'  through -Q --help=warnings. Expect clang: it accepts -fstrict-aliasing' \
				'  -Wstrict-aliasing=1 in silence and implements no such diagnostic, so the flags ride' \
				'  every compile line of a clang build while detecting nothing. chron aliasing coverage' \
				'  is gcc-only, and a clang run does not have it.' >&2; \
		elif [ "$$lvl" = "1" ]; then \
			printf '%s\n' \
				'  The effective level is 1, which is the level that catches this violation. So the' \
				'  flags are right and the compiler is not implementing them - that is clang, which' \
				'  accepts -Wstrict-aliasing=1 in silence. chron aliasing coverage is gcc-only.' >&2; \
		else \
			printf '  The effective -Wstrict-aliasing level is %s, and only level 1 diagnoses this control.\n' "$$lvl" >&2; \
			printf '%s\n' \
				'  Levels 0, 2 and 3 are all silent on it, measured against this same file - so the' \
				'  warning is at the WRONG LEVEL rather than missing, and ALIASING_CFLAGS is likely' \
				'  untouched. What overrides it is a later EXPLICIT level, since an explicit level beats' \
				'  the 3 that -Wall implies from either side. CFLAGS ends with EXTRA_CFLAGS, so' \
				'  EXTRA_CFLAGS=-Wstrict-aliasing=3 does exactly this. Note that 3 is also what -Wall' \
				'  implies on its own, so a level of 3 is equally what removing ALIASING_CFLAGS looks' \
				'  like; 0 and 2 can only have been asked for.' >&2; \
		fi; \
		exit 1; \
	fi
	@if ! grep -q 'strict-aliasing' $(BUILD_DIR)/alias_control.log; then \
		printf "\033[0;31mcheck-aliasing: the control failed to compile, but not for aliasing - so this says nothing about whether the warning is armed:\033[0m\n" >&2; \
		cat $(BUILD_DIR)/alias_control.log >&2; \
		exit 1; \
	fi
	@printf "\033[0;32mA planted type-punning violation is still refused by the library's own flags.\033[0m\n"

# The flag stamps only work on the rules that name one. Nothing in make
# requires it: a new object rule added without its stamp compiles with
# whatever flags are in force and is then never rebuilt when they change,
# which looks exactly like a correct incremental build. That is how
# $(TEST_HELPER_OBJ) went unstamped - it sits inside an `ifneq`, so it does
# not exist in this library at all and make's own rule database cannot see
# it. The check therefore reads the makefile text, where every branch is
# visible, rather than asking make what rules it has.
#
# The population is "every recipe that compiles a prerequisite", spelled as
# the -c $< that all ten of them carry. Two things are checked per rule: that
# some flags stamp is a prerequisite, and that it is the stamp for the tree
# the object is built into - a rule copied between trees keeps the old
# stamp, and then misses exactly the flag changes it was supposed to catch.
# The tree's name is the prefix of both $(<TREE>_OBJ_DIR) and
# $(<TREE>_FLAGS_STAMP), so that pairing is derived, not tabulated, and a
# fourth tree needs no edit here.
#
# What it does not check: that the stamp's own recipe records the flags this
# rule uses. FLAGS_STAMP records CFLAGS, CXXFLAGS, LDFLAGS and INCLUDE
# together, so a rule compiling with some fifth variable would pass.
# The unmodelled arm anchors at the head of the recipe line. A compiler
# named anywhere in the line also matches an echo that mentions one, and
# chron has two: the fuzz target's "install clang" advice, and
# check-aliasing's explanation of what clang does not implement. Counting
# prose as an invocation puts English sentences into a pinned number, so
# rewording a message moves a figure that is supposed to measure the build.
# Each count is in the unit its own message claims, which is not the same
# unit for every arm:
#
#   TOTAL      "compile rules"         - rules.  One rule however many lines
#                                        it spans, so the compile arm claims
#                                        its whole record.
#   LINKED     "link rules"            - rules.  The invariant is per rule -
#                                        this rule names its tree's link
#                                        stamp - and a second command in the
#                                        same recipe is the same rule under
#                                        the same stamp.
#   UNMODELLED "compiler invocations"  - commands.  The pin exists to notice
#                                        a new compiler call, so a call at
#                                        the head of a continuation line
#                                        counts, and the arm must not consume
#                                        its record.
#
# UNRECORDED follows the rule, not the command: a record's variables are read
# once, on the line that begins it. Reading them per invocation reports one
# unrecorded name once per command, and since UNRECORDED is pinned at zero
# that is a count of faults which is not a count of faults.
#
# Getting this wrong in either direction is quiet. Count records where the
# message says invocations and a pinned number falls while the makefile still
# says otherwise; count invocations where it says rules and one fault is
# reported twice. Only the compile arm consumes its record: a compile
# rule is one rule however many lines it occupies, and its marker may sit on
# any of them. The link and unmodelled arms deliberately do not, because a
# logical line can hold more than one compiler invocation - check-aliasing is
# a single `@if ... fi` holding three - and consuming the record would count
# them once while the pin, the message and the makefile all still say three.
# That collapse looks like a clean refactor: the control passes, a pinned
# number falls, and nothing says why. The arms below include one for it.
define stamp-check-awk
{ L[NR] = $$0 }
function vars(s, out,   v) {
  while (match(s, /\$$\([A-Za-z0-9_]+\)/)) {
    v = substr(s, RSTART + 2, RLENGTH - 3)
    out[v] = 1
    s = substr(s, RSTART + RLENGTH)
  }
}
BEGIN {
  PREREQ_N = split("LIBOBJECTS ASAN_LIBOBJECTS TEST_HELPER_OBJ", pa, " ")
  for (x = 1; x <= PREREQ_N; x++) PREREQ[pa[x]] = 1
}
END {
  total = 0; bad = 0; unmodelled = 0; unrecorded = 0; linked = 0; probes = 0
  for (i = 1; i <= NR; i++) {
    if (L[i] !~ /^\$$\([A-Z_]*FLAGS_STAMP\):/) continue
    name = L[i]; sub(/^\$$\(/, "", name); sub(/\).*/, "", name)
    for (j = i + 1; j <= NR && j < i + 8; j++) {
      if (L[j] !~ /printf/) continue
      pf = L[j]; pe = j
      while (pe < NR && L[pe] ~ /\\[ \t]*$$/) { pe++; pf = pf " " L[pe] }
      delete tmp; vars(pf, tmp)
      for (v in tmp) if (v != "") SV[name "|" v] = 1
      break
    }
  }
  cur = ""; curline = 0; skipto = 0
  for (i = 1; i <= NR; i++) {
    if (i <= skipto) continue
    if (L[i] !~ /^\t/) {
      if (L[i] ~ /:/ && L[i] !~ /:=/ && L[i] !~ /^\043/ && L[i] !~ /^[ ]/) {
        cur = L[i]; curline = i; m = i
        while (m < NR && L[m] ~ /\\[ \t]*$$/) { m++; cur = cur " " L[m] }
        skipto = m
      }
      continue
    }
    rec = L[i]; e = i
    while (e < NR && L[e] ~ /\\[ \t]*$$/) { e++; rec = rec " " L[e] }
    iscont = (i > 1 && L[i - 1] ~ /\\[ \t]*$$/)
    if (rec !~ /-c \$$</) {
      if (rec ~ /^\t[ \t]*\043/) continue
      if (rec ~ /-c \$$\$$</) continue
      head = rec
      sub(/^\t[ \t]*/, "", head)
      sub(/^[-@]+[ \t]*/, "", head)
      sub(/^if[ \t]+/, "", head)
      sub(/^![ \t]*/, "", head)
      sub(/^[-@]+[ \t]*/, "", head)
      if (head !~ /^\$$\$$?\([A-Z_]*(CC|CXX)\)[ \t]/ &&
          head !~ /^(cc|c\+\+|gcc|g\+\+|clang|clang\+\+)[ \t]/) continue
      hdr = cur; j = curline
      if (hdr !~ /LINK_FLAGS_STAMP/) {
        if (rec ~ /-fsyntax-only/) probes++; else unmodelled++
        continue
      }
      if (iscont) continue
      linked++
      match(hdr, /\$$\([A-Z_]*LINK_FLAGS_STAMP\)/)
      sn = substr(hdr, RSTART + 2, RLENGTH - 3)
      delete rv; vars(rec, rv)
      for (v in rv) {
        if (v == "" || v in PREREQ) continue
        if (!((sn "|" v) in SV)) {
          unrecorded++
          printf "  %s:%d: link recipe expands $$(%s), which %s does not record\n", FILENAME, i, v, sn
        }
      }
      continue
    }
    skipto = e
    total++
    hdr = cur; j = curline
    if (hdr !~ /FLAGS_STAMP/) {
      bad++
      printf "  %s:%d: compiles with no flags stamp: %s\n", FILENAME, j, hdr
      continue
    }
    tgt = hdr; sub(/:.*/, "", tgt)
    if (tgt ~ /OBJ_DIR/) {
      tree = tgt; sub(/.*\$$\(/, "", tree); sub(/OBJ_DIR.*/, "", tree)
      want = "$$(" tree "FLAGS_STAMP)"
      if (index(hdr, want) == 0) {
        bad++
        printf "  %s:%d: stamped for another tree, wants %s: %s\n", FILENAME, j, want, hdr
        continue
      }
    }
    match(hdr, /\$$\([A-Z_]*FLAGS_STAMP\)/)
    sn = substr(hdr, RSTART + 2, RLENGTH - 3)
    delete rv; vars(rec, rv)
    for (v in rv) {
      if (v == "" || !((sn "|" v) in SV)) {
        if (v == "") continue
        unrecorded++
        printf "  %s:%d: recipe expands $$(%s), which %s does not record\n", FILENAME, i, v, sn
      }
    }
  }
  printf "TOTAL %d BAD %d UNMODELLED %d UNRECORDED %d LINKED %d PROBES %d PREREQ %d\n", total, bad, unmodelled, unrecorded, linked, probes, PREREQ_N
}
endef

STAMP_CHECK_MAKEFILE := $(firstword $(MAKEFILE_LIST))
STAMP_CHECK_AWK := $(BUILD_DIR)/stamp_check.awk

# What this gate does NOT model, pinned so the set cannot grow in silence.
# The stamp invariant is about object rules that compile $< incrementally, and
# that is a narrower population than "every compile". Two recipe lines invoke
# a compiler outside it, and they are excused for different reasons, so they
# are counted separately: the fuzz harness build, which is a build rule and
# carries no stamp, and check-aliasing's -fsyntax-only control, which is a
# probe. The enumeration that stood here listed ten and described the state
# before the link stamps existed - the shared-library links, the test-binary
# links and the three compile-to-executable rules have all been in LINKED
# since then. A comment is not checked by anything, so it went on being read
# as the gate's own account of itself while the number beside it moved.
#
# Splitting them is a peer's adaptation, taken because the same conflation
# showed up here: one pin covering two exclusions, with a message describing
# only one of them. A probe produces nothing that can go stale, so its count
# should not rise when a build rule escapes the model, and the unmodelled pin
# should not rise every time a gate compiles a control.
#
# The compile-to-executable rules are the ones that could go stale on their
# own, and here they do not: each names $(APP_DIR)/$(STATIC_TARGET) as a normal
# prerequisite, so a flag change moves the stamp, rebuilds the objects,
# rebuilds the archive and relinks them. Measured rather than reasoned, because
# a rule can lose that prerequisite without anything failing: EXTRA_CFLAGS=-O1
# rebuilt the example and moved its DW_AT_producer from "-O2" to "-O1 -O2".
#
# The first probe was -DPROBE_STAMP and showed nothing, which read as "the
# binary is stale" and was really "a macro nothing reads compiles to identical
# bytes, and -D does not appear in the producer string at all". Probe with
# something the artifact records.
#
# This is a pin, not a judgement. It fails when the number moves, so a new
# compile-to-executable rule - in a library whose binaries do not happen to
# depend on a stamped object - has to be looked at instead of passing.
#
# What this gate does not check, stated because BAD 0 is easy to read as more
# than it is: whether a rule names the GENERATED headers its source includes.
# It asks only about flags stamps. Eight rules here compiled sources that
# include libver_gen.h without naming it and scored BAD 0 throughout, and the
# fresh-clone habit is what found them, not this sweep. A rule-level check
# looks tractable - every recipe compiling a source that transitively includes
# a generated header should name it order-only - but the transitive part needs
# the .d files, which exist only after a build, which is the wrong end. Left
# unguarded deliberately rather than half-built; a peer reached the same
# conclusion independently about the same gap in their copy.
#
# The practice that does cover it: build one object per rule as the FIRST
# command in a never-built clone, `rm -rf build` between each. Eleven targets
# here, across every reachable rule including the fuzz tree. `make -n` is not
# a substitute - it resolves the graph without compiling, so a missing
# order-only prerequisite cannot show up.
STAMP_UNMODELLED_EXPECTED := 1

# Gate probes: compiler invocations that produce nothing. check-aliasing's
# -fsyntax-only control is the only one.
STAMP_PROBES_EXPECTED := 1

# Names the link sweep skips because the rule already lists them as file
# prerequisites, where mtime is the real check. Pinned so the list cannot
# grow into an excuse.
STAMP_LINK_PREREQ_EXPECTED := 3

# What the planted control must produce. Four compile recipes, one of them
# wrapped; one with no stamp; seven variables no stamp records, three of them
# past a line break and one before it; four stamped link rules, one of them
# holding two invocations; and five compiler invocations outside
# the model, two of which share one logical line. PREREQ is deliberately
# absent - see the note by the comparison.
STAMP_CONTROL_EXPECTED := TOTAL 4 BAD 1 UNMODELLED 2 UNRECORDED 7 LINKED 4 PROBES 3

# $(file) is expanded when make expands the recipe, and make expands every
# line of a recipe before running any of them - so writing the sweep here,
# under an `@mkdir -p $(BUILD_DIR)` on the line above, wrote it into a
# directory that did not exist yet. It worked everywhere except where it
# matters: any tree that had been built once already had the directory, and
# it persists for the life of the checkout, so the failure was visible only
# with this gate as the FIRST command in a fresh clone. Found by a peer
# running exactly that, and reproduced here before being believed.
#
# Its own rule fixes it, because make expands that recipe only when it
# decides to run it, after the order-only directory exists. The makefile as a
# NORMAL prerequisite is the other half: without it the sweep is written once
# and never refreshed, so editing the awk would leave the gate running the
# previous version - which is the staleness this gate exists to find,
# happening to the gate.
$(STAMP_CHECK_AWK): $(STAMP_CHECK_MAKEFILE) | $(BUILD_DIR)
	$(file >$@,$(stamp-check-awk))

$(BUILD_DIR):
	@mkdir -p $@

check-stamps: ## Fail if a compile rule has no flags stamp, or the wrong one
check-stamps: $(STAMP_CHECK_AWK)
# The control comes first, and is a planted pair rather than a single bad
# rule: one stamped, one not. A sweep that has stopped matching compile
# recipes reports nothing wrong, which is indistinguishable from a clean
# makefile - so require it to find the planted one and only the planted one.
	@printf '%s\n\t%s \\\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n%s\n\t%s\n' \
		'$$(FLAGS_STAMP): force-flags' \
		"@printf '%s' '\$$(CFLAGS)" \
		"  \$$(INCLUDE)' > \$$@.new" \
		'$$(OBJ_DIR)/%.o: src/%.c $$(FLAGS_STAMP)' \
		'cc $$(CFLAGS) $$(INCLUDE) -c $$< -o $$@' \
		'$$(OBJ_DIR)/planted_nostamp.o: src/planted.c' \
		'cc $$(CFLAGS) $$(INCLUDE) -c $$< -o $$@' \
		'$$(OBJ_DIR)/planted_unrecorded.o: src/planted2.c $$(FLAGS_STAMP)' \
		'cc $$(CFLAGS) $$(PLANTED_UNRECORDED) $$(INCLUDE) -c $$< -o $$@' \
		'$$(LINK_FLAGS_STAMP): force-flags' \
		"@printf '%s' '\$$(LDFLAGS)' > \$$@.new" \
		'$$(APP_DIR)/planted_link_ok: planted.o $$(LINK_FLAGS_STAMP)' \
		'c++ $$(LDFLAGS) -o $$@ planted.o' \
		'$$(APP_DIR)/planted_link_nostamp: planted.o' \
		'g++ $$(LDFLAGS) -o $$@ planted.o' \
		'$$(APP_DIR)/planted_link_unrec: planted.o $$(LINK_FLAGS_STAMP)' \
		'g++ $$(LDFLAGS) $$(PLANTED_LINK_UNRECORDED) -o $$@ planted.o' \
		> $(BUILD_DIR)/stamp_control.mk
# Three of the planted rules wrap, because every read of a recipe used to take
# one physical line and a wrapped recipe hid everything past the backslash.
# Each shape returned a clean answer for the half it could not see, and a
# correct makefile prints the same fingerprint either way - so without a
# wrapped rule in the control the fix is not demonstrable and the bug comes
# back the next time this is edited.
#
# Where the planted variable sits is the whole test. The first version of
# these two rules put it before the break, where the unjoined read still
# found it: the control passed with the joining removed, which is a control
# that exercises nothing. It has to sit past the backslash - and for the
# compile rule, on the same line as the -c $< that makes the rule visible at
# all, so that dropping the join loses the variable and the rule together.
# It carries a second variable *before* the break for the opposite reason:
# with the marker past the break and nothing planted ahead of it, an unjoined
# marker test still finds the rule on its continuation line and counts it
# correctly, so TOTAL stops being able to show the defect. One variable on
# each side of the backslash is what makes both joins load-bearing.
#
# One planted link rule is spelled c++ rather than g++, so that the bare
# driver names in the head pattern are exercised. Without it the pattern can
# lose cc and c++ and nothing here notices: every planted compile rule is
# found by its -c $< marker and never reaches that pattern at all.
#
# It is the *stamped* one deliberately. Spelled on the stampless rule it
# moved UNMODELLED, where the record-consuming arm and the negation arm
# already move the same field by the same amount, and three arms printed two
# fingerprints between them. On the stamped rule it moves LINKED instead and
# all three are distinguishable. Which field an arm disturbs is part of the
# control's design, not a detail of where the shape happened to be planted.
#
# One correction to the commit that added this. It said chron has no wrapped
# compile or link recipe, so the defect was latent here. chron has exactly
# one wrapped compiler invocation - the fuzz harness build below, whose
# arguments continue onto a second line. It is in the unmodelled arm, which
# reads only the head of the recipe, so no count moved and the fingerprint is
# identical before and after the fix; that part was right for the wrong
# reason. What is true is narrower: no rule in either *stamped* population
# wraps today. The same sweep in a library that does have one reads a
# naturally-occurring compile rule out of its population entirely.
	@printf '%s\n\t%s \\\n\t%s\n%s\n\t%s \\\n\t%s\n' \
		'$$(APP_DIR)/planted_link_wrap: planted.o $$(LINK_FLAGS_STAMP)' \
		'g++ $$(LDFLAGS) -o $$@ planted.o' \
		'  $$(PLANTED_WRAP_UNRECORDED)' \
		'$$(OBJ_DIR)/planted_wrapc.o: src/planted3.c $$(FLAGS_STAMP)' \
		'cc $$(CFLAGS) $$(PLANTED_WRAPC_HEAD) $$(INCLUDE)' \
		'  $$(PLANTED_WRAPC_UNRECORDED) -c $$< -o $$@' \
		>> $(BUILD_DIR)/stamp_control.mk
# One planted gate holds three compiler invocations on one logical line, two
# of them at the head of a continuation. It is the arm for counting records
# instead of commands: consume the record in the unmodelled arm and this
# reads 1 where the makefile says 3.
#
# Three rather than two so that this arm and the bare-driver-name arm stay
# distinguishable. Both remove invocations from UNMODELLED; with two, both
# subtracted one and printed the same fingerprint, so the number said
# something had broken without saying what. An arm that fails is the
# requirement, but two arms that fail identically are one arm for the purpose
# of reading the failure.
	@printf '%s\n\t%s \\\n\t%s \\\n\t%s \\\n\t%s\n' \
		'planted-gate:' \
		'@if $$(CC) $$(CFLAGS) -fsyntax-only probe.c; then' \
		'  $$(CC) $$(CFLAGS) -fsyntax-only probe2.c;' \
		'  $$(CC) $$(CFLAGS) -fsyntax-only probe3.c;' \
		'fi' \
		>> $(BUILD_DIR)/stamp_control.mk
# A stampless link rule spelled with a negation. The strip chain runs @, then
# `if`, then @ again, and a `!` sits between the last two - so without a strip
# for it the head test rejects the line as not starting with a compiler and
# the rule lands in neither LINKED nor UNMODELLED. All zeros is the failure
# mode: a file whose only link rule has no stamp reads as a file with no link
# rules. Found by a peer porting this sweep into a library with three of them.
	@printf '%s\n\t%s\n' \
		'$$(APP_DIR)/planted_negated: planted.o' \
		'@if ! $$(CXX) $$(LDFLAGS) -o $$@ planted.o; then exit 1; fi' \
		>> $(BUILD_DIR)/stamp_control.mk
# A stamped link rule holding two invocations, with a different unrecorded
# variable in each. It arms three things at once: count the commands instead
# of the rule and LINKED is long by one; let the second invocation re-read the
# record and each name is reported twice; read one command instead of the
# record and only the first name is reported.
#
# Two different variables rather than one repeated, because reading the record
# once is a de-duplication and a de-duplication can swallow a real second
# finding. "Reported once" and "the other one never reported" are the same
# output when both names are the same.
	@printf '%s\n\t%s \\\n\t%s\n' \
		'$$(APP_DIR)/planted_two_invocations: planted.o $$(LINK_FLAGS_STAMP)' \
		'g++ $$(LDFLAGS) $$(PLANTED_FIRST_UNRECORDED) -o $$@.a planted.o &&' \
		'  g++ $$(LDFLAGS) $$(PLANTED_SECOND_UNRECORDED) -o $$@.b planted.o' \
		>> $(BUILD_DIR)/stamp_control.mk
# The control's failure message names two causes because the fingerprint
# genuinely cannot separate them, which is a different situation from the two
# arms that were merged onto one number by accident. Those were separable, and
# were separated by moving a planted shape to disturb a different field. This
# pair is not: the control is written by this recipe's own printf, so "has the
# shape gone missing" and "has the sweep stopped seeing it" are the same
# question asked of one number. Measured rather than assumed - deleting the
# negated rule from the emitter and removing the negation strip from the awk
# both print UNMODELLED 1, identically. Where shapes can be separated,
# separate them; where they converge, the message carries the ambiguity
# instead of asserting the likelier half. A peer found this in their port and
# the first version of this message had the same defect as the shadowed pin:
# right about one cause, actively wrong about the other.
#
# PREREQ is cut from the comparison rather than pinned twice. It counts the
# link sweep's skip list, which is a constant of the sweep and not something
# the control exercises - and while it was in this string, adding a name to
# that list failed the control first, with a message about reading rules
# wrongly. The pin below is the one that is supposed to fail for that, and it
# could never fire. A second check on the same fact is not redundancy: it
# takes the first failure and reports the wrong cause.
	@ctl=$$(awk -f $(STAMP_CHECK_AWK) \
			$(BUILD_DIR)/stamp_control.mk | tail -1 \
			| sed 's/ PREREQ [0-9]*$$//'); \
	if [ "$$ctl" != "$(STAMP_CONTROL_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-stamps: the control says '%s', not '%s'.\033[0m\n" "$$ctl" "$(STAMP_CONTROL_EXPECTED)" >&2; \
		printf "\nThis has two causes and they need opposite fixes, and this number\n" >&2; \
		printf "cannot tell them apart - a planted shape the sweep stopped seeing and\n" >&2; \
		printf "a planted shape that is no longer there print the same fingerprint,\n" >&2; \
		printf "byte for byte. Read the control this run just wrote:\n\n" >&2; \
		printf "    %s\n\n" "$(BUILD_DIR)/stamp_control.mk" >&2; \
		printf "If the shape is in it, the sweep is what changed. If the shape is\n" >&2; \
		printf "missing, the recipe that writes the control is what changed, and the\n" >&2; \
		printf "sweep may be fine. Either way a clean result from this sweep means\n" >&2; \
		printf "nothing until it is resolved.\n" >&2; \
		exit 1; \
	fi

# Two independent counts of the same population. If the sweep silently stops
# matching, its total falls away from grep's and the gate fails rather than
# passing on an empty sweep. Comment lines are dropped first because the prose
# above names the marker it is looking for, and counted itself as an eleventh
# compile recipe the first time this ran.
	@want=$$(grep -v '^#' $(STAMP_CHECK_MAKEFILE) \
		| grep -cF -- '-c $$<'); \
	out=$$(awk -f $(STAMP_CHECK_AWK) $(STAMP_CHECK_MAKEFILE)); \
	got=$$(printf '%s\n' "$$out" | sed -n 's/^TOTAL \([0-9]*\) .*/\1/p'); \
	bad=$$(printf '%s\n' "$$out" | sed -n 's/^TOTAL [0-9]* BAD \([0-9]*\) .*/\1/p'); \
	unmodelled=$$(printf '%s\n' "$$out" | sed -n 's/.* UNMODELLED \([0-9]*\) .*/\1/p'); \
	unrecorded=$$(printf '%s\n' "$$out" | sed -n 's/.* UNRECORDED \([0-9]*\) .*/\1/p'); \
	linked=$$(printf '%s\n' "$$out" | sed -n 's/.* LINKED \([0-9]*\) .*/\1/p'); \
	probes=$$(printf '%s\n' "$$out" | sed -n 's/.* PROBES \([0-9]*\) .*/\1/p'); \
	prereq=$$(printf '%s\n' "$$out" | sed -n 's/.* PREREQ \([0-9]*\)$$/\1/p'); \
	if [ "$$got" != "$$want" ]; then \
		printf "\033[0;31mcheck-stamps: the sweep saw %s compile recipes and grep found %s. One of them is wrong, so neither count can be trusted.\033[0m\n" "$$got" "$$want" >&2; \
		exit 1; \
	fi; \
	if [ "$$probes" != "$(STAMP_PROBES_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-stamps: %s compiler invocations are gate probes, not the %s it is pinned to. A probe produces nothing that can go stale, so it is excused for a different reason than a build rule is; if a build rule has started to look like one, the exclusion is wrong.\033[0m\n" \
			"$$probes" "$(STAMP_PROBES_EXPECTED)" >&2; \
		exit 1; \
	fi; \
	if [ "$$unmodelled" != "$(STAMP_UNMODELLED_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-stamps: %s compiler invocations are outside what this gate models, not the %s it is pinned to. A rule that compiles a source straight to an executable goes stale on its own unless it depends on a stamped object; this gate does not check that, so the change needs a look.\033[0m\n" \
			"$$unmodelled" "$(STAMP_UNMODELLED_EXPECTED)" >&2; \
		exit 1; \
	fi; \
	if [ "$$prereq" != "$(STAMP_LINK_PREREQ_EXPECTED)" ]; then \
		printf "\033[0;31mcheck-stamps: the link sweep ignores %s variable names, not the %s it is pinned to. Those names are skipped because the rule already lists them as file prerequisites, so mtime covers them; a name added for any other reason silences the check for that variable.\033[0m\n" \
			"$$prereq" "$(STAMP_LINK_PREREQ_EXPECTED)" >&2; \
		exit 1; \
	fi; \
	if [ "$$unrecorded" != "0" ]; then \
		printf "\033[0;31m\n### %s recipes expand a variable their stamp does not record ###\033[0m\n" "$$unrecorded" >&2; \
		printf '%s\n' "$$out" | grep 'does not record' >&2; \
		printf "\nNaming the right stamp is not enough: the stamp only moves when the\n" >&2; \
		printf "variables inside its own printf change. A flag that lives only in a\n" >&2; \
		printf "variable the stamp omits rebuilds nothing at all.\n" >&2; \
		exit 1; \
	fi; \
	if [ "$$bad" != "0" ]; then \
		printf "\033[0;31m\n### %s compile rules carry the wrong flags stamp, or none ###\033[0m\n" "$$bad" >&2; \
		printf '%s\n' "$$out" | grep -v '^TOTAL ' >&2; \
		printf "\nAn object built without its tree's stamp as a prerequisite is\n" >&2; \
		printf "never rebuilt when the flags change, and the stale object links\n" >&2; \
		printf "into everything downstream of it.\n" >&2; \
		exit 1; \
	fi; \
	printf "\033[0;32mAll %s compile rules carry the flags stamp for their own tree and %s link rules carry their tree's link stamp, every variable either recorded or a file prerequisite; of the compiler invocations outside the model, %s build rules and %s gate probes, as pinned.\033[0m\n" "$$got" "$$linked" "$$unmodelled" "$$probes"

check-layering: ## Fail if a lower tier includes a higher tier's header
	$(call layering-check,0,$(TIER0_FORBIDDEN),$(TIER0_FILES))
	$(call layering-check,1,$(TIER1_FORBIDDEN),$(TIER1_FILES))
	$(call layering-check,2,$(TIER2_FORBIDDEN),$(TIER2_FILES))
	@printf "\033[0;32mNo tier includes a higher tier's header.\033[0m\n"

####################################################################
# Oracle drivers
####################################################################
#
# This library, wrapped so that an oracle written in another language can ask
# it the same questions it asks itself. Built on demand, installed by nothing,
# and not part of the library.

ORACLE_SOURCES := $(wildcard tools/oracle/gchron_*.c)
ORACLE_TOOLS := $(patsubst tools/oracle/%.c,$(APP_DIR)/tools/%$(EXE_EXTENSION),$(ORACLE_SOURCES))

$(APP_DIR)/tools/%$(EXE_EXTENSION): tools/oracle/%.c $(APP_DIR)/$(STATIC_TARGET) \
		$(LINK_FLAGS_STAMP) | $(APP_DIR)/$(TARGET)
	@printf "\n### Building oracle driver: $* ###\n"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(CHRONLIBRARY) $(CUTIL_LIBS) $(STATIC_LINK_LIBS)

tools: ## Build the oracle drivers the differentials run against
tools: $(ORACLE_TOOLS) $(APP_DIR)/tools/icu_format$(EXE_EXTENSION)

# ICU, for the LDML differential. Found through pkg-config and **never linked
# by the library** - design.md section 1: what ICU sells beyond a copy of the
# tzdb is CLDR, which is tier 3's problem and not a reason to link three
# hundred locales into everything. Here it is the oracle and nothing else.
ICU_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --cflags icu-i18n icu-uc 2>/dev/null)
ICU_LIBS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LOOKUP_PATH) pkg-config --libs icu-i18n icu-uc 2>/dev/null)

$(APP_DIR)/tools/icu_format$(EXE_EXTENSION): tools/oracle/icu_format.cpp $(LINK_FLAGS_STAMP)
	@if [ -z "$(strip $(ICU_LIBS))" ]; then \
		printf "\033[0;31micu_format: ICU was not found by pkg-config.\033[0m\n" >&2; \
		printf "The LDML differential has no oracle without it, and design.md\n" >&2; \
		printf "section 8.3 makes ICU the *definition* of what a pattern means.\n" >&2; \
		exit 1; \
	fi
	@printf "\n### Building oracle driver: icu_format ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(ICU_CFLAGS) -o $@ $< $(ICU_LIBS)

check-oracle-ldml: ## Check the LDML formatter against ICU (needs libicu-dev)
check-oracle-ldml: $(APP_DIR)/tools/gchron_format$(EXE_EXTENSION) \
		$(APP_DIR)/tools/icu_format$(EXE_EXTENSION)
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" python3 tools/oracle/ldml_diff.py \
		--chron $(APP_DIR)/tools/gchron_format$(EXE_EXTENSION) \
		--icu $(APP_DIR)/tools/icu_format$(EXE_EXTENSION)

check-oracle-ldml-parse: ## Check the LDML parser against ICU (needs libicu-dev)
check-oracle-ldml-parse: $(APP_DIR)/tools/gchron_scan$(EXE_EXTENSION) \
		$(APP_DIR)/tools/icu_format$(EXE_EXTENSION)
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" python3 tools/oracle/ldml_parse_diff.py \
		--chron $(APP_DIR)/tools/gchron_scan$(EXE_EXTENSION) \
		--icu $(APP_DIR)/tools/icu_format$(EXE_EXTENSION)

#
# Temporal's string format *is* RFC 9557 - the `[America/New_York]` suffix was
# standardised for it - and V8's implementation is the one test262 exercises,
# so asking V8 is the same authority without a corpus to fetch. Temporal is
# behind a flag in node 22, and the driver says so rather than skipping.
#
check-oracle-temporal: ## Check the RFC 9557 reader against V8's Temporal (needs node)
check-oracle-temporal: $(APP_DIR)/tools/gchron_iso$(EXE_EXTENSION)
	@if ! command -v node >/dev/null 2>&1; then \
		printf "\033[0;31mcheck-oracle-temporal: node is not installed.\033[0m\n" >&2; \
		printf "Temporal is the oracle for this grammar; without it the check\n" >&2; \
		printf "is absent rather than weaker, and saying so beats a green run.\n" >&2; \
		exit 1; \
	fi
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" node --harmony-temporal \
		tools/oracle/temporal_diff.js \
		--driver $(APP_DIR)/tools/gchron_iso$(EXE_EXTENSION)

#
# The four differentials in one target. They are not part of `make test` -
# they need ICU, node and python3, and a build machine is not obliged to have
# any of them - but until this existed they were in no aggregate target
# either, so each ran only when somebody typed its name and a regression
# waited for that to happen.
#
check-oracles: ## Run every differential against its outside oracle
check-oracles: check-oracle-ldml check-oracle-ldml-parse check-oracle-temporal
check-oracles: check-oracle-zoneinfo

check-oracle-zoneinfo: ## Check every zone against Python's zoneinfo (needs python3)
check-oracle-zoneinfo: $(APP_DIR)/tools/gchron_zone$(EXE_EXTENSION)
	@if ! command -v python3 >/dev/null 2>&1; then \
		printf "\033[0;31mcheck-oracle-zoneinfo: python3 is not installed.\033[0m\n" >&2; \
		printf "The oracle is the authority here; without it this check is not\n" >&2; \
		printf "weaker, it is absent, and saying so beats a green run.\n" >&2; \
		exit 1; \
	fi
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" python3 tools/oracle/zoneinfo_diff.py \
		--driver $(APP_DIR)/tools/gchron_zone$(EXE_EXTENSION)

####################################################################
# Conformance vectors
####################################################################
#
# Regeneration is a target rather than something `make test` does, and the
# vectors are committed, so that a test run never needs the oracle - and so
# that a CI that *does* have the oracle can regenerate and fail on a diff,
# which is how an upstream corpus change is noticed rather than absorbed.
# See documentation/design.md section 12.

JSON_SCHEMA_SUITE := third_party/json-schema-test-suite/$(shell cat tools/corpus/JSON_SCHEMA_COMMIT 2>/dev/null)

vectors: ## Regenerate every committed conformance vector file
vectors: vectors-jsonschema vectors-zones vectors-calendar vectors-leap
vectors: vectors-yaml vectors-calendars

vectors-yaml: ## Rebuild the YAML 1.1 timestamp vectors (needs PyYAML)
	@if ! python3 -c 'import yaml' 2>/dev/null; then \
		printf "\033[0;31mvectors-yaml: PyYAML is not installed.\033[0m\n" >&2; \
		printf "It is the reference implementation of YAML 1.1 and the oracle\n" >&2; \
		printf "for this grammar: pip install PyYAML\n" >&2; \
		exit 1; \
	fi
	python3 tools/oracle/yaml_timestamp.py --out tests/data/vectors/parse

vectors-leap: ## Rebuild the leap-second vectors (needs the tzdb leapseconds file)
	python3 tools/oracle/leapseconds.py

vectors-zones: ## Rebuild the zone transition vectors (needs zdump)
	python3 tools/oracle/zdump.py --out tests/data/vectors/zones/transitions.vec

vectors-calendar: ## Rebuild the Gregorian calendar vectors (needs python3)
	python3 tools/oracle/ordinal.py --out tests/data/vectors/calendar

#
# The Julian and hybrid vectors, which come from a different oracle than the
# Gregorian ones above: `convertdate` implements Reingold and Dershowitz's
# algorithms directly. design.md section 12 says regeneration is `make
# vectors`, and these two files were committed without a target that could
# rebuild them.
#
vectors-calendars: ## Rebuild the Julian and hybrid vectors (needs convertdate)
	@if ! python3 -c 'import convertdate' 2>/dev/null; then \
		printf "\033[0;31mvectors-calendars: convertdate is not installed.\033[0m\n" >&2; \
		printf "It implements Reingold and Dershowitz's algorithms and is the\n" >&2; \
		printf "oracle for the calendars beyond Gregorian: pip install convertdate\n" >&2; \
		exit 1; \
	fi
	python3 tools/oracle/calendars.py --out tests/data/vectors/calendar

vectors-jsonschema: ## Rebuild the JSON Schema format vectors (needs the fetched suite)
	@if [ ! -d "$(JSON_SCHEMA_SUITE)" ]; then \
		printf "\033[0;31mvectors-jsonschema: $(JSON_SCHEMA_SUITE) is missing.\033[0m\n" >&2; \
		printf "Run tools/corpus/fetch.sh first.\n" >&2; \
		exit 1; \
	fi
	python3 tools/oracle/jsonschema_format.py \
		--suite "$(JSON_SCHEMA_SUITE)" \
		--out tests/data/vectors/parse

test: ## Make and run the unit tests
test: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES) $(TEST_GATES)
	@#
	@# `failed` is what makes this a gate, for the reason spelled out on
	@# test-valgrind: a bare for-loop reports the exit status of its *last*
	@# iteration, so this target passed whenever the alphabetically-last suite
	@# passed and the other $(words $(TEST_EXECUTABLES)) could fail unseen.
	@# That is the defect section 12.3 names, and it sat in the target every
	@# commit is measured against - including the commit that fixed the
	@# identical loop one target below and stopped there.
	@#
	@failed=""; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\033[0;30;43m\n"; \
		printf "############################\n"; \
		printf "### Running %s tests ###\n" "$$test_name"; \
		printf "############################"; \
		printf "\033[0m\n\n"; \
		if ! LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$test_exe --gtest_brief=1; then \
			failed="$$failed $$test_name"; \
		fi; \
	done; \
	if [ -n "$$failed" ]; then \
		printf "\033[0;31m\nFailed:$$failed\033[0m\n"; \
		exit 1; \
	fi; \
	printf "\033[0;32m\nAll $(words $(TEST_EXECUTABLES)) suites passed.\033[0m\n"

#
# What a release is measured with. `make test` stays lenient about a missing
# tool, because the common case is a developer without libicu-dev who still
# wants the suite to run; this is the run that refuses to call an unasked
# question an answer. notes/suite/SUITE-TODO.md item 17.
#
test-full: ## Run the suite with every gate and every differential required
test-full:
	@$(MAKE) --no-print-directory test REQUIRE_ORACLES=1
	@$(MAKE) --no-print-directory check-oracles

test-quiet: ## Run tests with minimal output (one line per test suite)
test-quiet: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES)
	@#
	@# total_suites_failed is counted separately from total_failed because a
	@# suite that dies before printing its summary - a segfault at start-up, a
	@# missing shared library, an ASan abort - yields no "[  FAILED  ] n" line
	@# to parse. Its row said FAIL while the total said PASS and the target
	@# exited 0, because the only thing being summed was a number that suite
	@# never got far enough to print.
	@#
	@total_tests=0; total_passed=0; total_failed=0; total_suites_failed=0; \
	total_time=0; failed_suites=""; \
	printf "\n\033[1;36m%-30s %8s %10s %s\033[0m\n" "Test Suite" "Tests" "Time" "Status"; \
	printf "\033[1;36m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		output=$$(LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$test_exe --gtest_brief=1 2>&1); \
		exit_code=$$?; \
		num_tests=$$(echo "$$output" | grep -oP '\[\s*=+\s*\]\s*\K\d+(?=\s+tests?)' | head -1); \
		time_ms=$$(echo "$$output" | grep -oP '\(\K\d+(?=\s*ms\s*total\))' | head -1); \
		[ -z "$$num_tests" ] && num_tests=0; \
		[ -z "$$time_ms" ] && time_ms=0; \
		total_tests=$$((total_tests + num_tests)); \
		total_time=$$((total_time + time_ms)); \
		if [ $$exit_code -eq 0 ]; then \
			total_passed=$$((total_passed + num_tests)); \
			printf "%-30s %8d %8dms \033[0;32mPASS\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
		else \
			failures=$$(echo "$$output" | grep -oP '\[\s*FAILED\s*\]\s*\K\d+' | head -1); \
			[ -z "$$failures" ] && failures=$$num_tests; \
			total_failed=$$((total_failed + failures)); \
			total_suites_failed=$$((total_suites_failed + 1)); \
			total_passed=$$((total_passed + num_tests - failures)); \
			printf "%-30s %8d %8dms \033[0;31mFAIL\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
			failed_suites="$$failed_suites\n\033[0;31m=== $$test_name FAILURES ===\033[0m\n$$output\n"; \
		fi; \
	done; \
	printf "\033[1;36m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	if [ $$total_failed -eq 0 ] && [ $$total_suites_failed -eq 0 ]; then \
		printf "\033[0;32m%-30s %8d %6dms PASS\033[0m\n\n" "TOTAL" "$$total_tests" "$$total_time"; \
	else \
		printf "\033[0;31m%-30s %8d %6dms FAIL (%d failed in %d suites)\033[0m\n" "TOTAL" "$$total_tests" "$$total_time" "$$total_failed" "$$total_suites_failed"; \
		printf "$$failed_suites\n"; \
		exit 1; \
	fi

test-valgrind: ## Run all tests under valgrind (Linux only)
test-valgrind: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES)
ifeq ($(OS_NAME), Linux)
	@#
	@# `failed` is what makes this a gate. A bare for-loop reports the exit
	@# status of its *last* iteration, so the first spelling of this target
	@# passed whenever the alphabetically-last suite passed, no matter what
	@# the others did - a gate that cannot fail, which is the one thing
	@# section 12.3 says a gate must be able to do. `--error-exitcode=1` was
	@# set the whole time and had nothing to report to.
	@#
	@failed=""; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\033[0;30;43m\n"; \
		printf "############################\n"; \
		printf "### Running %s tests under Valgrind ###\n" "$$test_name"; \
		printf "############################"; \
		printf "\033[0m\n\n"; \
		if ! GCHRON_CIVIL_SWEEP_YEARS="$(VALGRIND_SWEEP_YEARS)" \
			LD_LIBRARY_PATH="$(TEST_LD_PATH)" valgrind $(VALGRIND_FLAGS) \
			$$test_exe --gtest_brief=1; then \
			failed="$$failed $$test_name"; \
		fi; \
	done; \
	if [ -n "$$failed" ]; then \
		printf "\033[0;31m\nValgrind failed:$$failed\033[0m\n"; \
		exit 1; \
	fi; \
	printf "\033[0;32m\nValgrind clean across $(words $(TEST_EXECUTABLES)) suites.\033[0m\n"
else
	@printf "\033[0;31m\nValgrind is only available on Linux\n\033[0m\n"
	@exit 1
endif

# test-valgrind-quiet passes only when both the tests pass and Valgrind is
# clean, so a FAIL here can mean an assertion failure even with no leaks.
test-valgrind-quiet: ## Run tests under valgrind with minimal output (Linux only)
test-valgrind-quiet: $(APP_DIR)/$(TARGET) $(TEST_EXECUTABLES)
ifeq ($(OS_NAME), Linux)
	@total_tests=0; total_failed=0; total_time=0; failed_suites=""; \
	printf "\n\033[1;35m%-30s %8s %10s %s\033[0m\n" "Test Suite (Valgrind)" "Tests" "Time" "Status"; \
	printf "\033[1;35m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	for test_exe in $(TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		output=$$(GCHRON_CIVIL_SWEEP_YEARS="$(VALGRIND_SWEEP_YEARS)" \
			LD_LIBRARY_PATH="$(TEST_LD_PATH)" valgrind $(VALGRIND_FLAGS) $$test_exe --gtest_brief=1 2>&1); \
		exit_code=$$?; \
		num_tests=$$(echo "$$output" | grep -oP '\[\s*=+\s*\]\s*\K\d+(?=\s+tests?)' | head -1); \
		time_ms=$$(echo "$$output" | grep -oP '\(\K\d+(?=\s*ms\s*total\))' | head -1); \
		[ -z "$$num_tests" ] && num_tests=0; \
		[ -z "$$time_ms" ] && time_ms=0; \
		total_tests=$$((total_tests + num_tests)); \
		total_time=$$((total_time + time_ms)); \
		has_leak=$$(echo "$$output" | grep -c "are definitely lost\|are indirectly lost\|are possibly lost" || true); \
		if [ $$exit_code -eq 0 ] && [ $$has_leak -eq 0 ]; then \
			printf "%-30s %8d %8dms \033[0;32mPASS\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms"; \
		else \
			if [ $$has_leak -gt 0 ]; then status_msg="LEAK"; else status_msg="FAIL"; fi; \
			total_failed=$$((total_failed + 1)); \
			printf "%-30s %8d %8dms \033[0;31m%s\033[0m\n" "$$test_name" "$$num_tests" "$$time_ms" "$$status_msg"; \
			failed_suites="$$failed_suites\n\033[0;31m=== $$test_name FAILURES ===\033[0m\n$$output\n"; \
		fi; \
	done; \
	printf "\033[1;35m%-30s %8s %10s %s\033[0m\n" "------------------------------" "--------" "----------" "------"; \
	if [ $$total_failed -eq 0 ]; then \
		printf "\033[0;32m%-30s %8d %6dms PASS\033[0m\n\n" "TOTAL" "$$total_tests" "$$total_time"; \
	else \
		printf "\033[0;31m%-30s %8d %6dms FAIL (%d suites)\033[0m\n" "TOTAL" "$$total_tests" "$$total_time" "$$total_failed"; \
		printf "$$failed_suites\n"; \
		exit 1; \
	fi
else
	@printf "\033[0;31m\nValgrind is only available on Linux\n\033[0m\n"
	@exit 1
endif

####################################################################
# Sanitizer build (ASan + UBSan): separate build dir, run the test suite
####################################################################
#
# -fno-sanitize-recover=undefined for the reason given at FUZZ_SAN below: with
# UBSan recovering, undefined behaviour prints a line to stderr and execution
# continues, so the suite finishes green and the finding scrolls past in a few
# hundred lines of gtest output. ASan aborts either way, which is why this is
# easy to miss - the half of the pair that does not abort is the half that
# catches signed overflow, and this library computes in nanoseconds since 1970.
# The flag was added to the fuzz build and not to this one, which is the build
# that runs on every change.
#
# float-cast-overflow is named because GCC does not put it in `undefined`,
# though Clang does - so the sanitizer build, which uses $(CC), was not
# checking it while the fuzz build, which uses clang, was.  `(int64_t)d` for a
# `d` too large for the type is undefined, and this library converts Excel,
# Cocoa and MJD serials from `double`.  Naming it in one variable used by both
# lists is the point: the check has to appear in -fsanitize *and* in
# -fno-sanitize-recover, and spelling them separately is how they drift - with
# the check enabled and the recover left at `undefined`, UBSan prints the
# finding and exits 0, so the gate describes the bug and passes.
#
# Measured on gcc 14.2 rather than assumed: bounds-strict and
# pointer-overflow already report under plain `undefined`, and
# unsigned-integer-overflow does not exist there.  float-divide-by-zero is
# deliberately absent - IEEE 754 defines it, and it would fire on correct code
# that records an infinity.
UBSAN_CHECKS := undefined,float-cast-overflow

# The optimization level both sanitizer builds compile at, for the same reason
# the checks list above is one variable: spelled separately in two places, the
# two builds drift and nobody notices until a finding will not reproduce.
#
# -O1 rather than the -O2 the release ships, chosen on what was measured
# across the suite rather than on the usual folklore:
#
#   - It is not about detection. On gcc 14.2, heap-use-after-free,
#     stack-buffer-overflow, signed overflow and float-to-int overflow are all
#     caught at -O1 and at -O2 alike. Strict aliasing is caught at neither, at
#     any level, by any sanitizer in this toolchain - so "the optimizer
#     exploits aliasing at -O2" is a true statement about the optimizer and
#     says nothing about what the gate would see. chron has no aliasing
#     exposure to lose either way: every big-endian read is byte-at-a-time
#     (tzif.c), there are no punning unions, and `make check-aliasing` sweeps
#     for it with a planted control.
#
#   - It is about the gate and the fuzzers landing on one codegen. `test-asan`
#     and the ten fuzz harnesses ask overlapping questions, and a finding from
#     one has to reproduce under the other. Different levels would make a
#     difference in codegen indistinguishable from a difference in the input.
#
#   - And it does not move when the release level does. An ASan build that
#     inherits $(OPT_CFLAGS) changes fidelity as a side effect of a decision
#     about shipping speed, which is a change to the gate that nobody chose.
SAN_OPT_CFLAGS := -O1

ASAN_UBSAN_FLAGS := -fsanitize=address,$(UBSAN_CHECKS) \
	-fno-sanitize-recover=$(UBSAN_CHECKS) \
	-fno-omit-frame-pointer -g $(SAN_OPT_CFLAGS)
ASAN_BUILD_DIR := ./build/$(BUILD)-asan
ASAN_OBJ_DIR := $(ASAN_BUILD_DIR)/objects
ASAN_FLAGS_STAMP := $(ASAN_OBJ_DIR)/.flags
ASAN_LINK_FLAGS_STAMP := $(ASAN_OBJ_DIR)/.linkflags
ASAN_APP_DIR := $(ASAN_BUILD_DIR)/apps

ASAN_LIBOBJECTS := $(patsubst src/%.c,$(ASAN_OBJ_DIR)/%.o,$(SOURCES))
ASAN_TARGET := $(BASE_NAME_PREFIX)-asan.$(LIB_EXTENSION)
ASAN_CHRONLIBRARY := -L $(ASAN_APP_DIR) -l$(SUITE)-$(PROJECT)$(BRANCH)-asan

ASAN_CFLAGS := $(CFLAGS) $(ASAN_UBSAN_FLAGS) -DGCHRON_BUILD -DGCHRON_TEST_BUILD
ASAN_CXXFLAGS := $(CXXFLAGS) $(ASAN_UBSAN_FLAGS)
ASAN_LDFLAGS := $(LDFLAGS) $(ASAN_UBSAN_FLAGS)
ifeq ($(UNAME_S), Linux)
	ASAN_CFLAGS += -fPIC
endif

$(ASAN_OBJ_DIR)/%.o: src/%.c $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling (ASan+UBSan): $< ###\n"
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_APP_DIR)/$(ASAN_TARGET): $(ASAN_LIBOBJECTS) $(ASAN_LINK_FLAGS_STAMP)
	@printf "\n### Linking ASan+UBSan Chron Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) -shared -o $@ $(ASAN_LIBOBJECTS) $(ASAN_LDFLAGS) $(CUTIL_LIBS)

$(ASAN_OBJ_DIR)/tests/%.o: tests/%.cpp $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling ASan Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_OBJ_DIR)/tests/%.o: tests/unit/%.cpp $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling ASan Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_OBJ_DIR)/tests/%.o: tests/conformance/%.cpp $(ASAN_FLAGS_STAMP) | $(LIBVER_GEN)
	@printf "\n### Compiling ASan Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

define asan-test-executable-rule
ASAN_TEST_OBJ_$1 := $(ASAN_OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(ASAN_APP_DIR)/$2$(EXE_EXTENSION): $$(ASAN_TEST_OBJ_$1) $(ASAN_APP_DIR)/$(ASAN_TARGET) \
		$(ASAN_LINK_FLAGS_STAMP)
	@printf "\n### Linking ASan Test: $2 ###\n"
	@mkdir -p $$(@D)
	$(CXX) $(ASAN_CXXFLAGS) -o $$@ $$(ASAN_TEST_OBJ_$1) $(ASAN_LDFLAGS) $(ASAN_CHRONLIBRARY) $(CUTIL_LIBS) $(TESTFLAGS) $(STATIC_LINK_LIBS)
endef

$(foreach pair,$(TEST_PAIRS),\
	$(eval $(call asan-test-executable-rule,$(word 1,$(subst |, ,$(pair))),$(word 2,$(subst |, ,$(pair))))))

ASAN_TEST_EXECUTABLES := $(addprefix $(ASAN_APP_DIR)/,$(addsuffix $(EXE_EXTENSION),$(TEST_NAMES)))

# ASan insists on being the first library loaded. A desktop session that sets
# LD_PRELOAD for its own reasons (libgtk3-nocsd, for instance) puts something
# ahead of it and every sanitized binary refuses to start, so put the runtime
# back in front rather than discarding whatever the user had set.
ASAN_RUNTIME := $(shell $(CC) -print-file-name=libasan.so 2>/dev/null)

test-asan: ## Build with ASan+UBSan and run the test suite
test-asan: $(ASAN_TEST_EXECUTABLES)
	@for test_exe in $(ASAN_TEST_EXECUTABLES); do \
		test_name=$$(basename $$test_exe $(EXE_EXTENSION)); \
		printf "\033[0;30;43m\n### Running %s (ASan+UBSan) ###\033[0m\n\n" "$$test_name"; \
		LD_PRELOAD="$(ASAN_RUNTIME)$${LD_PRELOAD:+:$$LD_PRELOAD}" \
		LD_LIBRARY_PATH="$(ASAN_APP_DIR):$(LIB_INSTALL_PATH)/$(SUITE)" \
			$$test_exe --gtest_brief=1 || exit 1; \
	done
	@printf "\033[0;32m\nASan+UBSan suite clean.\033[0m\n"

####################################################################
# Fuzzing (libFuzzer)
####################################################################
#
# The library is rebuilt with -fsanitize=fuzzer-no-link rather than linking the
# ordinary shared library. That matters: libFuzzer steers its mutations by the
# coverage it observes, and a harness linked against an uninstrumented library
# sees none of the parser's branches, which leaves it generating random input
# rather than exploring the format.
FUZZ_CC ?= clang
FUZZ_CXX ?= clang++
FUZZ_CC_OK := $(shell which $(FUZZ_CC) 2>/dev/null)
#
# -fno-sanitize-recover=undefined is what makes UBSan a *finding* rather than
# a log line. Without it undefined behaviour prints and execution continues,
# so libFuzzer never sees a crash and the input that caused it is not saved -
# the fuzzer runs on happily past the defect it just found. ASan aborts either
# way; UBSan does not. `regex` had this flag and this library did not.
#
# $(UBSAN_CHECKS) rather than `undefined` so this list and the sanitizer
# build's cannot drift apart.  Clang already has float-cast-overflow in its
# `undefined` group, so naming it changes nothing here today; it is named so
# that the two builds are checking the same thing whoever reads them.
FUZZ_SAN := -fsanitize=address,$(UBSAN_CHECKS) \
	-fno-sanitize-recover=$(UBSAN_CHECKS) \
	-fno-omit-frame-pointer -g $(SAN_OPT_CFLAGS) $(ALIASING_ASSUME_CFLAGS)
FUZZ_LIB_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer-no-link
FUZZ_BIN_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer
FUZZ_DIR := $(BUILD_DIR)/fuzz
FUZZ_OBJ_DIR := $(FUZZ_DIR)/objects
FUZZ_FLAGS_STAMP := $(FUZZ_OBJ_DIR)/.flags
FUZZ_APP_DIR := $(FUZZ_DIR)/apps
FUZZ_OBJECTS := $(patsubst src/%.c,$(FUZZ_OBJ_DIR)/%.o,$(SOURCES))
FUZZ_CORPUS := tests/fuzz/corpus

#
# The sanitiser and fuzzer builds need this every bit as much as the ordinary
# one does. They were written without it, and because they build into their
# own directories, nothing in the ordinary build's dependency graph ever
# reached them: a header change rebuilt the release objects and left the ASan
# objects untouched. `GCHRON_Limits` grew a field in phase 3, and ASan then
# reported a stack-buffer-overflow in `gchron_limits_default` - a real
# overflow, of a phase-1 struct written by a phase-3 function, in a build that
# should have been rebuilt entirely. The gate that exists to find memory
# errors was the one build that could manufacture them.
#
ASAN_DEPFILES := $(ASAN_LIBOBJECTS:.o=.d) \
    $(foreach pair,$(TEST_PAIRS),$(ASAN_OBJ_DIR)/tests/$(basename $(notdir $(word 1,$(subst |, ,$(pair))))).d)
-include $(ASAN_DEPFILES)
-include $(FUZZ_OBJECTS:.o=.d)

# A smoke-test length by default; for a real campaign: make fuzz FUZZ_TIME=3600
FUZZ_TIME ?= 60

$(FUZZ_OBJ_DIR)/%.o: src/%.c $(FUZZ_FLAGS_STAMP) | $(LIBVER_GEN)
	@mkdir -p $(@D)
	@$(FUZZ_CC) $(FUZZ_LIB_FLAGS) -std=c17 -w $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

# $1 = harness basename (fuzz_obj), $2 = target suffix (obj)
define fuzz-rule
fuzz-$2: ## Build the $2 fuzz harness (requires clang)
fuzz-$2: $$(FUZZ_APP_DIR)/$1

$$(FUZZ_APP_DIR)/$1: tests/fuzz/$1.cpp $$(FUZZ_OBJECTS)
	@if [ -z "$$(FUZZ_CC_OK)" ]; then \
		echo "fuzzing requires $$(FUZZ_CXX); install clang or set FUZZ_CC/FUZZ_CXX"; \
		exit 1; \
	fi
	@mkdir -p $$(@D) $$(FUZZ_CORPUS)/$2
	@printf "\n### Building fuzz harness: $1 ###\n"
	$$(FUZZ_CXX) $$(FUZZ_BIN_FLAGS) -std=c++20 -w $$(INCLUDE) \
		-o $$@ $$< $$(FUZZ_OBJECTS) $(CUTIL_LIBS) $(STATIC_LINK_LIBS)

fuzz-run-$2: ## Run the $2 fuzzer for $$(FUZZ_TIME) seconds
fuzz-run-$2: $$(FUZZ_APP_DIR)/$1
	@mkdir -p $$(FUZZ_CORPUS)/$2
	@printf "\n### Fuzzing $2 for $$(FUZZ_TIME)s ###\n"
	@LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$(FUZZ_APP_DIR)/$1 $$(FUZZ_CORPUS)/$2 \
		-max_total_time=$$(FUZZ_TIME) -print_final_stats=1
endef

$(eval $(call fuzz-rule,fuzz_parse,parse))
$(eval $(call fuzz-rule,fuzz_arith,arith))
$(eval $(call fuzz-rule,fuzz_tzif,tzif))
$(eval $(call fuzz-rule,fuzz_posix_tz,posix_tz))
$(eval $(call fuzz-rule,fuzz_duration,duration))
$(eval $(call fuzz-rule,fuzz_format,format))
$(eval $(call fuzz-rule,fuzz_leap,leap))
$(eval $(call fuzz-rule,fuzz_scan,scan))
$(eval $(call fuzz-rule,fuzz_zonedir,zonedir))
$(eval $(call fuzz-rule,fuzz_textfmt,textfmt))

fuzz: ## Build and run every fuzzer for $(FUZZ_TIME) seconds each
fuzz: fuzz-run-parse fuzz-run-arith fuzz-run-tzif fuzz-run-posix_tz \
	fuzz-run-duration fuzz-run-format fuzz-run-leap fuzz-run-scan \
	fuzz-run-zonedir fuzz-run-textfmt

fuzz-clean: ## Remove the fuzz build (keeps the corpus)
	-@rm -rf $(FUZZ_DIR)

fuzz-seed: ## Reset the seed corpora to what tools/fuzz/seed.py produces
# What is committed is the seed corpus only. A ninety-second campaign adds
# several thousand units, which is a great many git objects for input the next
# run would find again anyway - and this suite already has cutil's six
# megabytes of tracked Doxygen output as the cautionary tale. Run this after a
# campaign to put the corpus back.
	python3 tools/fuzz/seed.py

####################################################################
# Install / uninstall
####################################################################

# Where the loader configuration fragment is written. Kept overridable so a
# staged or user-prefix install has somewhere to write it.
LDCONF_INSTALL_PATH ?= /etc/ld.so.conf.d

# What goes in the .pc Requires: field. Built from the same variables the
# compile uses, so a dependency on another branch cannot be named one way for
# the build and another way for consumers.
PC_REQUIRES := $(CUTIL_PC)

# Where this project's own .pc file is installed.
PKGCONFIG_INSTALL_PATH ?= $(PKG_CONFIG_PATH)


install: ## Install the library globally, requires sudo
# Depends on all: install used to copy whatever happened to be in the build
# directory, so it could install a stale artifact or fail outright on a clean
# tree.
install: all
	# Installing the shared library.
	@mkdir -p $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
	@cp $(APP_DIR)/$(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/
	@ln -f -s $(TARGET) $(LIB_INSTALL_PATH)/$(SUITE)/$(SO_NAME)
	@ln -f -s $(SO_NAME) $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)
	# Installing the ld configuration file.
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then mkdir -p $(LDCONF_INSTALL_PATH); fi
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then echo "$(LIB_INSTALL_PATH)/$(SUITE)" > $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf; fi
endif
ifeq ($(OS_NAME), Windows)
	@mkdir -p $(BIN_INSTALL_PATH)/$(SUITE)
	@cp $(APP_DIR)/$(TARGET).a $(LIB_INSTALL_PATH)
	@cp $(APP_DIR)/$(TARGET) $(BIN_INSTALL_PATH)
endif
	# Installing the headers.
	# Removed first: this directory is owned entirely by this project and
	# branch, and copying over the top of it would leave headers behind that
	# have since been renamed or deleted.
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@mkdir -p $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@if [ -d include/ghoti.io ]; then \
		cp -r include/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	@if [ -d $(GEN_DIR)/ghoti.io ]; then \
		cp -r $(GEN_DIR)/ghoti.io $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)/ ; \
	fi
	# Installing the pkg-config files.
	@mkdir -p $(PKGCONFIG_INSTALL_PATH)
	@cat pkgconfig/$(SUITE)-$(PROJECT).pc | sed 's/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g; s/(VERSION)/$(VERSION)/g; s|(PC_LIB_DIR)|$(PC_LIB_DIR)|g; s|(PC_INCLUDE_DIR)|$(PC_INCLUDE_DIR)|g; s|(REQUIRES)|$(PC_REQUIRES)|g' > $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
ifeq ($(OS_NAME), Linux)
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) installed"

uninstall: ## Delete the globally-installed files.  Requires sudo.
ifeq ($(OS_NAME), Linux)
	@rm -f $(LIB_INSTALL_PATH)/$(SUITE)/$(BASE_NAME)*
	@rm -f $(LDCONF_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).conf
endif
ifeq ($(OS_NAME), Windows)
	@rm -f $(LIB_INSTALL_PATH)/$(TARGET).a
	@rm -f $(BIN_INSTALL_PATH)/$(TARGET)
endif
	@rm -rf $(INCLUDE_INSTALL_PATH)/$(SUITE)/$(PROJECT)$(BRANCH)
	@rm -f $(PKGCONFIG_INSTALL_PATH)/$(SUITE)-$(PROJECT)$(BRANCH).pc
	@rmdir --ignore-fail-on-non-empty $(INCLUDE_INSTALL_PATH)/$(SUITE)
	@rmdir --ignore-fail-on-non-empty $(LIB_INSTALL_PATH)/$(SUITE)
ifeq ($(OS_NAME), Linux)
	@if [ -n "$(LDCONF_INSTALL_PATH)" ]; then ldconfig >> /dev/null 2>&1; fi
endif
	@echo "Ghoti.io $(PROJECT)$(BRANCH) has been uninstalled"

debug: ## Build the project in DEBUG mode
	make all BUILD=debug

install-debug: ## Install the DEBUG library globally, requires sudo
	make install BUILD=debug

uninstall-debug: ## Delete the DEBUG globally-installed files.  Requires sudo.
	make uninstall BUILD=debug

test-debug: ## Make and run the Unit tests in DEBUG mode
	make test BUILD=debug

test-valgrind-debug: ## Run all tests under valgrind in DEBUG mode (Linux only)
	make test-valgrind BUILD=debug

watch-debug: ## Watch the file directory for changes and compile the target in DEBUG mode
	make watch BUILD=debug

test-watch-debug: ## Watch the file directory for changes and run the unit tests in DEBUG mode
	make test-watch BUILD=debug

docs: ## Generate the documentation in the ./docs subdirectory
	doxygen

#
# `make docs` cannot be the gate. It has to finish in order to produce the
# manual, so its warnings are advisory, and there were 154 of them here -
# 125 of which said an implementation detail in a .c file was undocumented,
# because the documentation for those functions is on their declarations in
# the *internal.h headers that EXCLUDE_PATTERNS keeps out. Nobody reads a
# list like that, and underneath it sat seventeen unresolved @ref commands,
# two @ref-eaten backslashes in a YAML grammar, a README link to a file the
# manual could not see, and three doc blocks written for one entity and
# attached by adjacency to the next one along.
#
# So the warnings that matter are separated from the ones that do not, and
# only the first kind is fatal. Two passes, because doxygen's undocumented
# warning is global and only the public headers should answer for it:
#
#   1. everything in INPUT, undocumented off: catches markup faults - a
#      broken @ref, an unknown command, a code span that wrapped across a
#      newline and left <tt> unbalanced.
#   2. include/ only, undocumented on: catches a public declaration that
#      shipped with no documentation at all.
#
# Neither generates output; the point is the exit status. The config is the
# real Doxyfile with the overrides appended, so the gate cannot drift away
# from the manual it is checking.
#
#
# CONVENTIONS.md section 8 says a new file under src/ or include/ carries the
# SPDX identifier and the LGPLv3 notice. Nothing enforced it, and a per-file
# notice whose whole job is to travel with code somebody copied out is worth
# very little if it is only there when whoever added the file remembered.
#
# The second half exists because of what the relicensing actually missed. The
# COPYING files, the per-file headers and the README were all changed; the
# prose that asserted MIT in passing was not, and was found weeks later by
# grepping. This library's own README still said "MIT licensed" at the
# bottom until 2026-09-21. A gate on src/ alone would not have seen it.
#
# LICENSE_ALLOWLIST is for a *legitimate* mention of another project's terms
# - a third-party attribution, a comparison table. It is empty here because
# nothing in the tracked set needs it yet; the pattern is a space-separated
# list of paths.
#
LICENSE_ID := LGPL-3.0-only
LICENSE_FOREIGN := MIT|Apache|BSD|ISC|MPL|AGPL|GPL-2\\.0|Unlicense|proprietary
LICENSE_ALLOWLIST :=

check-license: ## Fail if a source file has no SPDX header or a document claims another license
	@missing=""; \
	count=0; \
	for f in $$(find src include -type f \( -name '*.c' -o -name '*.h' \) | sort); do \
		count=$$((count + 1)); \
		grep -q "SPDX-License-Identifier: $(LICENSE_ID)" "$$f" \
			|| missing="$$missing\n  $$f"; \
	done; \
	if [ -n "$$missing" ]; then \
		printf "\033[0;31mcheck-license: no SPDX header:\033[0m$$missing\n"; \
		exit 1; \
	fi; \
	printf "check-license: %d sources carry $(LICENSE_ID).\n" "$$count"; \
	if ! command -v git >/dev/null 2>&1 || ! git rev-parse --git-dir >/dev/null 2>&1; then \
		printf "check-license: skipped the prose half (not a git checkout). $(SKIP_NOTE)\n" >&2; \
		exit $(SKIP_EXIT); \
	fi; \
	skip="$(LICENSE_ALLOWLIST) COPYING COPYING.LESSER"; \
	claims=$$(git ls-files -z \
		| xargs -0 grep -HInIE "(SPDX-License-Identifier:|[Ll]icen[sc]ed under|[Ll]icense:)" 2>/dev/null \
		| grep -E "$(LICENSE_FOREIGN)" | grep -v "$(LICENSE_ID)"); \
	for f in $$skip; do \
		claims=$$(printf "%s\n" "$$claims" | grep -v "^$$f:" || true); \
	done; \
	sections=$$(git ls-files -z | xargs -0 grep -lIE "^#+ +License" 2>/dev/null); \
	bad_sections=""; \
	for f in $$sections; do \
		case " $$skip " in *" $$f "*) continue;; esac; \
		grep -A3 -IE "^#+ +License" "$$f" | grep -q "$(LICENSE_ID)" \
			|| bad_sections="$$bad_sections $$f"; \
	done; \
	if [ -n "$$claims" ] || [ -n "$$bad_sections" ]; then \
		printf "\033[0;31mcheck-license: another license is asserted\033[0m\n"; \
		[ -n "$$claims" ] && printf "%s\n" "$$claims"; \
		[ -n "$$bad_sections" ] && printf "  a License section that does not say $(LICENSE_ID):$$bad_sections\n"; \
		exit 1; \
	fi; \
	printf "\033[0;32mcheck-license: nothing claims a license other than $(LICENSE_ID).\033[0m\n"

check-counts: ## Fail if README.md's test count no longer matches the suites
# The README tells a reader what `make test` will print before they run it,
# which is worth having and is worth nothing if it is wrong.  It went stale
# the first time tests were added without touching it, and nothing noticed:
# the number is prose, so no compiler, no test and no other gate reads it.
# A documented fact that nothing checks becomes a wrong one, and review
# cannot catch this sort: the reviewer has no more idea of the true number
# than the writer did.
#
# Counted by asking the built binaries rather than by grepping the sources,
# because the binaries are what `make test` runs.  A grep over tests/ agrees
# today, and agrees by luck:  it counts macros as written, so it would keep
# counting a suite that stopped being linked, miss anything gtest registers
# rather than spells, and count a DISABLED_ test that never runs.  A gate
# that can disagree with the thing it certifies is not measuring it.
check-counts: $(TEST_EXECUTABLES)
	@tests=0; \
	for test_exe in $(TEST_EXECUTABLES); do \
		n=$$(LD_LIBRARY_PATH="$(TEST_LD_PATH)" $$test_exe --gtest_list_tests 2>/dev/null \
			| grep -cE "^  ") || n=0; \
		tests=$$((tests + n)); \
	done; \
	suites=$(words $(TEST_EXECUTABLES)); \
	claimed=$$(grep -oE "# [0-9]+ tests in [0-9]+ suites" README.md | head -1); \
	actual="# $$tests tests in $$suites suites"; \
	if [ -z "$$claimed" ]; then \
		printf "\033[0;31mcheck-counts: README.md no longer states a test count\033[0m\n"; \
		printf "  expected a line containing: %s\n" "$$actual"; \
		exit 1; \
	fi; \
	if [ "$$claimed" != "$$actual" ]; then \
		printf "\033[0;31mcheck-counts: README.md is stale\033[0m\n"; \
		printf "  README says: %s\n" "$$claimed"; \
		printf "  the suites are: %s\n" "$$actual"; \
		exit 1; \
	fi; \
	printf "\033[0;32mcheck-counts: README.md's %d tests in %d suites is what is there.\033[0m\n" \
		"$$tests" "$$suites"

check-docs: ## Fail on a documentation fault in the headers or the manual
	@if ! command -v doxygen >/dev/null 2>&1; then \
		printf "check-docs: skipped (no doxygen). $(SKIP_NOTE)\n" >&2; \
		exit $(SKIP_EXIT); \
	fi; \
	tmp=$$(mktemp -d) || exit 1; \
	trap 'rm -rf "$$tmp"' EXIT; \
	fail=0; \
	for pass in markup undocumented; do \
		if [ "$$pass" = markup ]; then \
			extra="WARN_IF_UNDOCUMENTED=NO"; \
		else \
			extra="INPUT=include\nUSE_MDFILE_AS_MAINPAGE=\nWARN_IF_UNDOCUMENTED=YES"; \
		fi; \
		out=$$( { cat Doxyfile; printf "QUIET=YES\nOUTPUT_DIRECTORY=$$tmp\nGENERATE_LATEX=NO\nHAVE_DOT=NO\nWARN_AS_ERROR=FAIL_ON_WARNINGS\n$$extra\n"; } \
			| doxygen - 2>&1 ) || fail=1; \
		printf "%s\n" "$$out" | grep -E "warning:|error:" | sort -u || true; \
	done; \
	if [ $$fail -ne 0 ]; then \
		printf "\033[0;31mcheck-docs: the documentation has faults\033[0m\n"; \
		exit 1; \
	fi; \
	printf "\033[0;32mcheck-docs: no documentation faults.\033[0m\n"

docs-pdf: docs ## Generate the documentation as a pdf, at ./docs/(SUITE)-(PROJECT)(BRANCH).pdf
	cd ./docs/latex/ && make
	mv -f ./docs/latex/refman.pdf ./docs/$(SUITE)-$(PROJECT)$(BRANCH)-docs.pdf

cloc: ## Count the lines of code used in the project
	cloc src include tests Makefile

coverage: ## Build instrumented, run the tests, and report line coverage
# Cleans first because the object files would otherwise be reused without the
# instrumentation, then cleans and rebuilds at the end: leaving the
# instrumented objects behind would have a later `make` silently link them,
# and leaving the tree cleaned would break any sibling project that links this
# one. The cost is one extra build; coverage is not run often.
	@$(MAKE) --no-print-directory clean > /dev/null
# The instrumented build, the report and the restoration of the tree are one
# shell command so that the cleanup runs whatever fails. Letting a failure
# stop the recipe leaves the --coverage objects in build/, and the next
# ordinary `make` links them into a library that needs the gcov runtime; every
# later build then fails with undefined references to __gcov_init until
# somebody works out why.
#
# TEST_GATES is cleared because --coverage links the gcov runtime, which
# exports mangle_path. check-symbols is right to reject that in a shipping
# build and wrong to reject it here, and it made this target fail before it
# ever produced a report.
	@status=0; \
	$(MAKE) --no-print-directory test TEST_GATES= \
		EXTRA_CFLAGS="--coverage -O0" \
		EXTRA_LDFLAGS="--coverage" > /dev/null || status=$$?; \
	if [ $$status -eq 0 ]; then \
		tools/coverage.sh $(OBJ_DIR) || status=$$?; \
	else \
		printf "coverage: the instrumented test run failed; no report\n" >&2; \
	fi; \
	$(MAKE) --no-print-directory clean > /dev/null; \
	$(MAKE) --no-print-directory all > /dev/null; \
	exit $$status

clean: ## Remove all contents of the build directories.
	-@rm -rvf $(OBJ_DIR)/*
	-@rm -rvf $(APP_DIR)/*
	-@rm -rvf $(GEN_DIR)/*
	-@rm -rvf $(ASAN_BUILD_DIR)

help: ## Display this help
	@grep -E '^[ a-zA-Z_-]+:.*?## .*$$' Makefile | sort | sed 's/\\([^:]*\\):.*## \\(.*\\)/\\1:\\2/' | awk -F: '{printf "%-20s %s\n", $$1, $$2}' | sed "s/(SUITE)/$(SUITE)/g; s/(PROJECT)/$(PROJECT)/g; s/(BRANCH)/$(BRANCH)/g"


####################################################################
# Flag stamps
####################################################################
# Each build tree carries the flag string it was built with. The stamp is
# rewritten only when that string differs -- written to a scratch file,
# compared, moved into place only on a difference -- so its mtime moves on a
# flag change and on nothing else. The object rules above depend on it.
#
# This replaces listing `Makefile` as a prerequisite, which was too broad (a
# comment-only edit recompiled everything) and too narrow (a command-line
# override such as `make EXTRA_CFLAGS=-O2` changes no file's mtime and so was
# invisible).
#
# These rules sit at the end of the file for two reasons. A rule's target
# expands when make reads the line, so a stamp rule above its own OBJ_DIR
# definition has an empty target: not an error, just a rule that silently does
# not exist. And the first target in a makefile is the default goal, so a stamp
# rule above `all:` makes a bare `make` build the stamp and nothing else.
.PHONY: force-flags

# Link flags are their own stamp rather than an addition to the compile
# stamp: the two sets overlap but a change to one should not rebuild the
# other's population. A gtest upgrade has no business recompiling 41
# library objects.
#
# The archive rule is deliberately not here. It runs ar, which takes none
# of these, and it passes $$^ - a stamp prerequisite would be archived
# into the library rather than watched.
$(LINK_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(CFLAGS) $(CXXFLAGS) $(LDFLAGS) $(INCLUDE) $(CUTIL_LIBS) $(STATIC_LINK_LIBS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG) $(ICU_CFLAGS) $(ICU_LIBS) $(CHRONLIBRARY) $(TESTFLAGS) $(TESTFLAGS_RESOLVED)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(ASAN_LINK_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CXX) $(ASAN_CXXFLAGS) $(ASAN_LDFLAGS) $(ASAN_CHRONLIBRARY) $(CUTIL_LIBS) $(STATIC_LINK_LIBS) $(TESTFLAGS) $(TESTFLAGS_RESOLVED)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(LIB_CFLAGS) $(CFLAGS) $(CXXFLAGS) $(LDFLAGS) $(INCLUDE) $(TEST_DATA)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(ASAN_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(CC) $(CXX) $(ASAN_CFLAGS) $(ASAN_CXXFLAGS) $(ASAN_LDFLAGS) $(INCLUDE) $(TEST_DATA)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@

$(FUZZ_FLAGS_STAMP): force-flags
	@mkdir -p $(@D)
	@printf '%s\n' '$(FUZZ_CC) $(FUZZ_SAN) $(FUZZ_LIB_FLAGS) $(FUZZ_BIN_FLAGS) $(INCLUDE)' > $@.new
	@cmp -s $@.new $@ 2>/dev/null && rm -f $@.new || mv -f $@.new $@
