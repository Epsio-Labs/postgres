# Makefile for libpgplanner - Standalone PostgreSQL Planner Library
#
# This library exposes the PostgreSQL query planner for use by external
# databases (e.g., Rust-based databases via FFI) without requiring a
# running PostgreSQL instance.

# PostgreSQL source directory (parent)
PG_SOURCE = ..
PG_BUILD = $(PG_SOURCE)

# PostgreSQL configuration
PG_INCLUDE = $(PG_SOURCE)/src/include
PG_BACKEND = $(PG_SOURCE)/src/backend

# Compiler settings
CC = gcc
CFLAGS = -Wall -g -O2 -fPIC
CFLAGS += -I$(PG_INCLUDE)
CFLAGS += -I$(PG_SOURCE)/src/interfaces/libpq
CFLAGS += -I$(PG_BUILD)/src/include
CFLAGS += -Iinclude
CFLAGS += -DPG_PLANNER_STANDALONE

# Library output
LIB_NAME = libpgplanner
LIB_SHARED = $(LIB_NAME).so
LIB_STATIC = $(LIB_NAME).a

# Our source files
LIB_SOURCES = \
	src/pgplanner_init.c \
	src/pgplanner_catalog.c \
	src/pgplanner_types.c \
	src/pgplanner_operators.c \
	src/pgplanner_functions.c \
	src/pgplanner_relations.c \
	src/pgplanner_api.c \
	src/pgplanner_stubs.c \
	src/pgplanner_memory.c \
	src/pgplanner_query.c

LIB_OBJECTS = $(LIB_SOURCES:.c=.o)

# PostgreSQL backend object files we need
# Parser
PG_PARSER_OBJS = $(wildcard $(PG_BACKEND)/parser/*.o)

# Optimizer
PG_OPTIMIZER_OBJS = $(wildcard $(PG_BACKEND)/optimizer/*/*.o)

# Nodes
PG_NODES_OBJS = $(wildcard $(PG_BACKEND)/nodes/*.o)

# Utils (selective - we need specific modules)
PG_UTILS_OBJS = \
	$(wildcard $(PG_BACKEND)/utils/adt/*.o) \
	$(wildcard $(PG_BACKEND)/utils/cache/*.o) \
	$(wildcard $(PG_BACKEND)/utils/error/*.o) \
	$(wildcard $(PG_BACKEND)/utils/fmgr/*.o) \
	$(wildcard $(PG_BACKEND)/utils/hash/*.o) \
	$(wildcard $(PG_BACKEND)/utils/init/*.o) \
	$(wildcard $(PG_BACKEND)/utils/mb/*.o) \
	$(wildcard $(PG_BACKEND)/utils/misc/*.o) \
	$(wildcard $(PG_BACKEND)/utils/mmgr/*.o) \
	$(wildcard $(PG_BACKEND)/utils/resowner/*.o) \
	$(wildcard $(PG_BACKEND)/utils/sort/*.o) \
	$(wildcard $(PG_BACKEND)/utils/time/*.o)

# Catalog
PG_CATALOG_OBJS = $(wildcard $(PG_BACKEND)/catalog/*.o)

# Access methods
PG_ACCESS_OBJS = \
	$(wildcard $(PG_BACKEND)/access/common/*.o) \
	$(wildcard $(PG_BACKEND)/access/hash/*.o) \
	$(wildcard $(PG_BACKEND)/access/heap/*.o) \
	$(wildcard $(PG_BACKEND)/access/index/*.o) \
	$(wildcard $(PG_BACKEND)/access/nbtree/*.o) \
	$(wildcard $(PG_BACKEND)/access/table/*.o) \
	$(wildcard $(PG_BACKEND)/access/transam/*.o)

# Rewrite
PG_REWRITE_OBJS = $(wildcard $(PG_BACKEND)/rewrite/*.o)

# Storage
PG_STORAGE_OBJS = \
	$(wildcard $(PG_BACKEND)/storage/buffer/*.o) \
	$(wildcard $(PG_BACKEND)/storage/file/*.o) \
	$(wildcard $(PG_BACKEND)/storage/freespace/*.o) \
	$(wildcard $(PG_BACKEND)/storage/ipc/*.o) \
	$(wildcard $(PG_BACKEND)/storage/large_object/*.o) \
	$(wildcard $(PG_BACKEND)/storage/lmgr/*.o) \
	$(wildcard $(PG_BACKEND)/storage/page/*.o) \
	$(wildcard $(PG_BACKEND)/storage/smgr/*.o) \
	$(wildcard $(PG_BACKEND)/storage/sync/*.o)

# Lib
PG_LIB_OBJS = $(wildcard $(PG_BACKEND)/lib/*.o)

# Regex
PG_REGEX_OBJS = $(wildcard $(PG_BACKEND)/regex/*.o)

# Executor (needed for some functions)
PG_EXECUTOR_OBJS = $(wildcard $(PG_BACKEND)/executor/*.o)

# Tcop - exclude postgres.o and dest.o which have too many dependencies
PG_TCOP_OBJS = $(filter-out $(PG_BACKEND)/tcop/postgres.o $(PG_BACKEND)/tcop/dest.o, $(wildcard $(PG_BACKEND)/tcop/*.o))

# Port
PG_PORT_OBJS = \
	$(PG_SOURCE)/src/port/libpgport.a \
	$(PG_SOURCE)/src/common/libpgcommon.a

# All PostgreSQL objects
PG_ALL_OBJS = \
	$(PG_PARSER_OBJS) \
	$(PG_OPTIMIZER_OBJS) \
	$(PG_NODES_OBJS) \
	$(PG_UTILS_OBJS) \
	$(PG_CATALOG_OBJS) \
	$(PG_ACCESS_OBJS) \
	$(PG_REWRITE_OBJS) \
	$(PG_STORAGE_OBJS) \
	$(PG_LIB_OBJS) \
	$(PG_REGEX_OBJS) \
	$(PG_EXECUTOR_OBJS) \
	$(PG_TCOP_OBJS)

# System libraries
SYS_LIBS = -lpthread -lm -ldl -lz -lreadline

# Test sources
TEST_SOURCES = \
	tests/test_init.c \
	tests/test_types.c \
	tests/test_tables.c \
	tests/test_planning.c \
	tests/test_aggregates.c

TEST_BINARIES = $(TEST_SOURCES:.c=)

# Example sources
EXAMPLE_SOURCES = \
	examples/simple_select.c \
	examples/join_query.c \
	examples/aggregate_query.c

EXAMPLE_BINARIES = $(EXAMPLE_SOURCES:.c=)

# Default target
all: check-pg-build $(LIB_STATIC)

# Check that PostgreSQL has been built
check-pg-build:
	@if [ ! -f "$(PG_BACKEND)/parser/parser.o" ]; then \
		echo "Error: PostgreSQL must be built first."; \
		echo "Run 'make' in the parent directory."; \
		exit 1; \
	fi

# Static library - just our code
$(LIB_STATIC): $(LIB_OBJECTS)
	ar rcs $@ $(LIB_OBJECTS)

# Compile our source files
src/%.o: src/%.c include/pgplanner.h include/pgplanner_internal.h include/pgplanner_query.h
	$(CC) $(CFLAGS) -c $< -o $@

# Simple API tests - no PostgreSQL dependencies
tests: tests/test_api

tests/test_api: tests/test_api.c include/pgplanner.h
	$(CC) -Wall -g -O2 -Iinclude -o $@ $<

# Full tests - link our code with PG objects (requires extensive stub functions)
# Currently disabled due to PostgreSQL backend dependencies
tests-full: $(LIB_STATIC)
	@echo "Full integration tests are not yet implemented."
	@echo "The PostgreSQL backend has too many interdependencies."
	@echo "Use 'make tests' for basic API verification tests."

# Examples
examples: $(LIB_STATIC) $(EXAMPLE_BINARIES)

examples/%: examples/%.c $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_STATIC) \
		$(PG_ALL_OBJS) \
		$(PG_PORT_OBJS) \
		$(SYS_LIBS)

# Run tests
check: tests
	@echo "Running API tests..."
	@./tests/test_api
	@echo ""
	@echo "All tests passed!"

# Clean
clean:
	rm -f $(LIB_OBJECTS) $(LIB_SHARED) $(LIB_STATIC)
	rm -f $(TEST_BINARIES) $(EXAMPLE_BINARIES)

# Install (to a local prefix)
PREFIX ?= /usr/local
install: $(LIB_STATIC)
	install -d $(PREFIX)/lib
	install -d $(PREFIX)/include
	install -m 644 $(LIB_STATIC) $(PREFIX)/lib/
	install -m 644 include/pgplanner.h $(PREFIX)/include/

.PHONY: all check-pg-build tests examples check clean install
