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
	BUILD := linux/$(BUILD)

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
	BUILD := mac/$(BUILD)

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
	BUILD := win32/$(BUILD)

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
	BUILD := win64/$(BUILD)

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


CXX := g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c++20 -O1 -g $(EXTRA_CXXFLAGS)
CC := cc
CFLAGS := -pedantic-errors -Wall -Wextra -Werror -Wno-error=unused-function -Wfatal-errors -std=c17 -O0 -g $(EXTRA_CFLAGS)
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
DEPLESS_GOALS := docs docs-pdf clean cloc help vectors vectors-jsonschema \
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
TEST_GATES ?= check-symbols check-layering check-generated

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
		printf "check-generated: skipped entirely (no python3)\n"; \
		exit 0; \
	fi; \
	tmp=$$(mktemp -d) || exit 1; \
	trap 'rm -rf "$$tmp"' EXIT; \
	checked=0; \
	stale=""; \
	leap_source="$${TZDIR:-/usr/share/zoneinfo}/leap-seconds.list"; \
	if [ ! -f "$$leap_source" ]; then \
		printf "  src/leap/leap_builtin.c      not checked: no $$leap_source (it ships with tzdata)\n"; \
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

$(OBJ_DIR)/%.o: src/%.c | $(LIBVER_GEN)
	@printf "\n### Compiling $@ ###\n"
	@mkdir -p $(@D)
	$(CC) $(LIB_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

####################################################################
# Shared Library
####################################################################

$(APP_DIR)/$(TARGET): $(LIBOBJECTS)
	@printf "\n### Compiling Chron Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ $(LDFLAGS) $(CUTIL_LIBS) $(OS_SPECIFIC_LIBRARY_NAME_FLAG)

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
$(TEST_HELPER_OBJ): $(TEST_HELPER_SRC)
	@printf "\n### Compiling Test Helper ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@
endif

# Test sources live in tests/ and tests/unit/; the object name comes from the
# basename either way, so the executable name matches.
$(OBJ_DIR)/tests/%.o: tests/%.cpp
	@printf "\n### Compiling Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(OBJ_DIR)/tests/%.o: tests/unit/%.cpp
	@printf "\n### Compiling Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(OBJ_DIR)/tests/%.o: tests/conformance/%.cpp
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
		$(APP_DIR)/$(STATIC_TARGET) | $(APP_DIR)/$(TARGET)
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
		| $(APP_DIR)/$(TARGET)
	@printf "\n### Compiling Example: $* ###\n"
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $< $(LDFLAGS) $(CHRONLIBRARY) $(CUTIL_LIBS) $(STATIC_LINK_LIBS)

####################################################################
# Commands
####################################################################

# General commands
.PHONY: clean cloc docs docs-pdf examples coverage check-symbols check-layering
.PHONY: vectors vectors-jsonschema vectors-zones vectors-calendar
.PHONY: tools check-oracle-zoneinfo check-oracle-ldml check-oracle-ldml-parse check-generated
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
		| $(APP_DIR)/$(TARGET)
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

$(APP_DIR)/tools/icu_format$(EXE_EXTENSION): tools/oracle/icu_format.cpp
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
vectors: vectors-yaml

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
ASAN_UBSAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
ASAN_BUILD_DIR := ./build/$(BUILD)-asan
ASAN_OBJ_DIR := $(ASAN_BUILD_DIR)/objects
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

$(ASAN_OBJ_DIR)/%.o: src/%.c
	@printf "\n### Compiling (ASan+UBSan): $< ###\n"
	@mkdir -p $(@D)
	$(CC) $(ASAN_CFLAGS) $(INCLUDE) -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_APP_DIR)/$(ASAN_TARGET): $(ASAN_LIBOBJECTS)
	@printf "\n### Linking ASan+UBSan Chron Library ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) -shared -o $@ $^ $(ASAN_LDFLAGS) $(CUTIL_LIBS)

$(ASAN_OBJ_DIR)/tests/%.o: tests/%.cpp
	@printf "\n### Compiling ASan Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_OBJ_DIR)/tests/%.o: tests/unit/%.cpp
	@printf "\n### Compiling ASan Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

$(ASAN_OBJ_DIR)/tests/%.o: tests/conformance/%.cpp
	@printf "\n### Compiling ASan Test: $* ###\n"
	@mkdir -p $(@D)
	$(CXX) $(ASAN_CXXFLAGS) $(INCLUDE) -Itests -DGCHRON_TEST_DATA=\"$(TEST_DATA)\" -c $< -MMD -MP -MF $(@:.o=.d) -o $@

define asan-test-executable-rule
ASAN_TEST_OBJ_$1 := $(ASAN_OBJ_DIR)/tests/$(basename $(notdir $1)).o

$(ASAN_APP_DIR)/$2$(EXE_EXTENSION): $$(ASAN_TEST_OBJ_$1) $(ASAN_APP_DIR)/$(ASAN_TARGET)
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
FUZZ_SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-fno-omit-frame-pointer -g -O1
FUZZ_LIB_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer-no-link
FUZZ_BIN_FLAGS := $(FUZZ_SAN) -fsanitize=fuzzer
FUZZ_DIR := $(BUILD_DIR)/fuzz
FUZZ_OBJ_DIR := $(FUZZ_DIR)/objects
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

$(FUZZ_OBJ_DIR)/%.o: src/%.c
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

fuzz: ## Build and run every fuzzer for $(FUZZ_TIME) seconds each
fuzz: fuzz-run-parse fuzz-run-arith fuzz-run-tzif fuzz-run-posix_tz \
	fuzz-run-duration fuzz-run-format fuzz-run-leap fuzz-run-scan

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
