# Makefile for ushow tests
#
# Builds and runs unit tests for each module

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I../src

# NetCDF configuration (reuse from main Makefile)
DKRZ_NC_CONFIG := /sw/spack-levante/netcdf-c-4.8.1-qk24yp/bin/nc-config
NC_CONFIG := $(shell if [ -x "$(DKRZ_NC_CONFIG)" ]; then echo "$(DKRZ_NC_CONFIG)"; else echo "nc-config"; fi)
NETCDF_CFLAGS := $(shell $(NC_CONFIG) --cflags 2>/dev/null || pkg-config --cflags netcdf 2>/dev/null)
NETCDF_LIBS := $(shell $(NC_CONFIG) --libs 2>/dev/null || pkg-config --libs netcdf 2>/dev/null || echo "-lnetcdf")
NETCDF_LIBDIR := $(shell $(NC_CONFIG) --prefix 2>/dev/null)/lib
NETCDF_RPATH := -Wl,-rpath,$(NETCDF_LIBDIR)

CFLAGS += $(NETCDF_CFLAGS)
LIBS = $(NETCDF_LIBS) $(NETCDF_RPATH) -lm

# Zarr support (optional) - build with: make WITH_ZARR=1
ifdef WITH_ZARR
BLOSC_PREFIX := $(shell brew --prefix c-blosc 2>/dev/null || echo "/usr/local")
LZ4_PREFIX := $(shell brew --prefix lz4 2>/dev/null || echo "/usr/local")
ZARR_CFLAGS := -DHAVE_ZARR -I$(BLOSC_PREFIX)/include -I$(LZ4_PREFIX)/include
ZARR_LIBS := -L$(BLOSC_PREFIX)/lib -L$(LZ4_PREFIX)/lib -lblosc -llz4
CFLAGS += $(ZARR_CFLAGS)
LIBS += $(ZARR_LIBS)
endif

# Source directory
SRCDIR = ../src

# Test executables
TEST_TARGETS = test_kdtree test_mesh test_regrid test_colormaps test_file_netcdf test_integration

# Add zarr test if enabled
ifdef WITH_ZARR
TEST_TARGETS += test_file_zarr
endif

# Object files needed from main project
KDTREE_OBJ = $(SRCDIR)/kdtree.c
MESH_OBJ = $(SRCDIR)/mesh.c
REGRID_OBJ = $(SRCDIR)/regrid.c
COLORMAPS_OBJ = $(SRCDIR)/colormaps.c
FILE_NETCDF_OBJ = $(SRCDIR)/file_netcdf.c

# Zarr support files (only when WITH_ZARR=1)
FILE_ZARR_OBJ = $(SRCDIR)/file_zarr.c
CJSON_OBJ = $(SRCDIR)/cJSON/cJSON.c

# When zarr is enabled, mesh.c uses cJSON, so we need to link it
ifdef WITH_ZARR
MESH_DEPS = $(MESH_OBJ) $(CJSON_OBJ)
else
MESH_DEPS = $(MESH_OBJ)
endif

.PHONY: all clean test run-tests

all: $(TEST_TARGETS)

# Individual test builds
test_kdtree: test_kdtree.c $(KDTREE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_mesh: test_mesh.c $(MESH_DEPS) $(KDTREE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_regrid: test_regrid.c $(REGRID_OBJ) $(MESH_DEPS) $(KDTREE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_colormaps: test_colormaps.c $(COLORMAPS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_file_netcdf: test_file_netcdf.c $(FILE_NETCDF_OBJ) $(MESH_DEPS) $(KDTREE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test_integration: test_integration.c $(FILE_NETCDF_OBJ) $(MESH_DEPS) $(KDTREE_OBJ) $(REGRID_OBJ) $(COLORMAPS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Zarr test (only built with WITH_ZARR=1)
test_file_zarr: test_file_zarr.c $(FILE_ZARR_OBJ) $(CJSON_OBJ) $(MESH_DEPS) $(KDTREE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Run all tests
test: run-tests

run-tests: $(TEST_TARGETS)
	@echo ""
	@echo "========================================"
	@echo "Running all ushow tests"
	@echo "========================================"
	@echo ""
	@failed=0; \
	passed=0; \
	for t in $(TEST_TARGETS); do \
		echo "----------------------------------------"; \
		if ./$$t; then \
			passed=$$((passed + 1)); \
		else \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "========================================"; \
	echo "Test Summary: $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

# Run individual test suites
test-kdtree: test_kdtree
	./test_kdtree

test-mesh: test_mesh
	./test_mesh

test-regrid: test_regrid
	./test_regrid

test-colormaps: test_colormaps
	./test_colormaps

test-netcdf: test_file_netcdf
	./test_file_netcdf

test-integration: test_integration
	./test_integration

test-zarr: test_file_zarr
	./test_file_zarr

# Clean up
clean:
	rm -f $(TEST_TARGETS) test_file_zarr
	rm -f /tmp/test_ushow_*.nc
	rm -rf /tmp/test_ushow_zarr_*.zarr

# Verbose build for debugging
verbose: CFLAGS += -v
verbose: all

# Memory check with valgrind (if available)
memcheck: $(TEST_TARGETS)
	@for t in $(TEST_TARGETS); do \
		echo "Checking $$t with valgrind..."; \
		valgrind --leak-check=full --error-exitcode=1 ./$$t 2>&1 | grep -E "(ERROR SUMMARY|definitely lost|indirectly lost)"; \
	done

# Help
help:
	@echo "ushow test suite"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build all test executables"
	@echo "  test         - Build and run all tests"
	@echo "  run-tests    - Run all tests (builds if needed)"
	@echo "  test-kdtree  - Run KDTree tests only"
	@echo "  test-mesh    - Run Mesh tests only"
	@echo "  test-regrid  - Run Regrid tests only"
	@echo "  test-colormaps - Run Colormap tests only"
	@echo "  test-netcdf  - Run NetCDF tests only"
	@echo "  test-zarr    - Run Zarr tests only (requires WITH_ZARR=1)"
	@echo "  test-integration - Run Integration tests only"
	@echo "  memcheck     - Run valgrind memory check on all tests"
	@echo "  clean        - Remove test executables and temp files"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Zarr support:"
	@echo "  Build with:  make WITH_ZARR=1"
	@echo "  Test with:   make WITH_ZARR=1 test-zarr"
