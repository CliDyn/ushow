# Makefile for ushow
#
# Unstructured data visualization tool

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
# --enable-new-dtags is Linux-only, skip on macOS (Darwin)
UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
LDFLAGS = -Wl,--enable-new-dtags
else
LDFLAGS =
endif

# NetCDF - try DKRZ system installation first, fall back to system nc-config
DKRZ_NC_CONFIG := /sw/spack-levante/netcdf-c-4.8.1-qk24yp/bin/nc-config
NC_CONFIG := $(shell if [ -x "$(DKRZ_NC_CONFIG)" ]; then echo "$(DKRZ_NC_CONFIG)"; else echo "nc-config"; fi)
NETCDF_CFLAGS := $(shell $(NC_CONFIG) --cflags 2>/dev/null || pkg-config --cflags netcdf 2>/dev/null)
NETCDF_LIBS := $(shell $(NC_CONFIG) --libs 2>/dev/null || pkg-config --libs netcdf 2>/dev/null || echo "-lnetcdf")
NETCDF_LIBDIR := $(shell $(NC_CONFIG) --prefix 2>/dev/null)/lib
NETCDF_RPATH := -Wl,-rpath,$(NETCDF_LIBDIR)

# X11 - DKRZ spack paths for Levante, fallback to pkg-config or common locations
# Override with X11_PREFIX for other systems (e.g., X11_PREFIX=/opt/X11 on macOS)
DKRZ_X11 := /sw/spack-levante/libx11-1.7.0-ozce5g
DKRZ_XT := /sw/spack-levante/libxt-1.1.5-vhamew
DKRZ_XAW := /sw/spack-levante/libxaw-1.0.13-gbiur2
DKRZ_SM := /sw/spack-levante/libsm-1.2.3-q4qkow
DKRZ_ICE := /sw/spack-levante/libice-1.0.9-2v7j4q
DKRZ_XMU := /sw/spack-levante/libxmu-1.1.2-4spgxo
DKRZ_XEXT := /sw/spack-levante/libxext-1.3.3-o4dpxe

X11_PKG_CFLAGS := $(shell pkg-config --cflags x11 xt xaw7 2>/dev/null)
X11_PKG_LIBS := $(shell pkg-config --libs x11 xt xaw7 2>/dev/null)
ifneq ($(strip $(X11_PKG_LIBS)),)
  X11_PKG_INCLUDEDIR := $(shell pkg-config --variable=includedir x11 2>/dev/null)
  X11_CFLAGS := $(if $(X11_PKG_INCLUDEDIR),-I$(X11_PKG_INCLUDEDIR),) $(X11_PKG_CFLAGS)
  X11_LIBS := $(X11_PKG_LIBS)
else
  X11_CFLAGS :=
  X11_LIBS :=
endif
ifeq ($(X11_CFLAGS),)
  ifdef X11_PREFIX
    X11_CFLAGS := -I$(X11_PREFIX)/include
    X11_LIBS := -L$(X11_PREFIX)/lib -lXaw -lXt -lX11
    X11_RPATH := -Wl,-rpath,$(X11_PREFIX)/lib
  else ifneq ($(wildcard $(DKRZ_X11)/lib/libX11.so),)
    # Use DKRZ spack X11 libraries
    X11_CFLAGS := -I$(DKRZ_X11)/include -I$(DKRZ_XT)/include -I$(DKRZ_XAW)/include -I$(DKRZ_SM)/include -I$(DKRZ_ICE)/include -I$(DKRZ_XMU)/include -I$(DKRZ_XEXT)/include
    X11_LIBS := -L$(DKRZ_XAW)/lib -L$(DKRZ_XT)/lib -L$(DKRZ_X11)/lib -L$(DKRZ_SM)/lib -L$(DKRZ_ICE)/lib -L$(DKRZ_XMU)/lib -L$(DKRZ_XEXT)/lib -lXaw -lXmu -lXt -lX11 -lSM -lICE -lXext
    X11_RPATH := -Wl,-rpath,$(DKRZ_XAW)/lib -Wl,-rpath,$(DKRZ_XT)/lib -Wl,-rpath,$(DKRZ_X11)/lib -Wl,-rpath,$(DKRZ_SM)/lib -Wl,-rpath,$(DKRZ_ICE)/lib -Wl,-rpath,$(DKRZ_XMU)/lib -Wl,-rpath,$(DKRZ_XEXT)/lib
  else ifneq ($(wildcard /opt/X11/include/X11),)
    X11_CFLAGS := -I/opt/X11/include
    X11_LIBS := -L/opt/X11/lib -lXaw -lXt -lX11
    X11_RPATH := -Wl,-rpath,/opt/X11/lib
  else
    X11_CFLAGS := -I/usr/include
    X11_LIBS := -lXaw -lXt -lX11
    X11_RPATH :=
  endif
else
  X11_LIBDIR := $(patsubst -L%,%,$(firstword $(filter -L%,$(X11_LIBS))))
  ifneq ($(X11_LIBDIR),)
    X11_RPATH := -Wl,-rpath,$(X11_LIBDIR)
  else
    X11_RPATH :=
  endif
endif

BASE_CFLAGS = $(CFLAGS) $(NETCDF_CFLAGS)
X11_FULL_CFLAGS = $(X11_CFLAGS) $(BASE_CFLAGS)

COMMON_LIBS = $(NETCDF_LIBS) $(NETCDF_RPATH) -lm
USHOW_LIBS = $(COMMON_LIBS) $(X11_LIBS) $(X11_RPATH)
UTERM_LIBS = $(COMMON_LIBS)

# Zarr support (optional) - build with: make WITH_ZARR=1
# Requires: c-blosc and lz4 (brew install c-blosc lz4 on macOS)
ifdef WITH_ZARR
# Prefer pkg-config (works well for conda/homebrew/system). Fallback to fixed prefixes.
ZARR_PKG_CFLAGS := $(shell pkg-config --cflags blosc lz4 2>/dev/null)
ZARR_PKG_LIBS := $(shell pkg-config --libs blosc lz4 2>/dev/null)

ifneq ($(strip $(ZARR_PKG_CFLAGS)$(ZARR_PKG_LIBS)),)
  ZARR_CFLAGS := -DHAVE_ZARR $(ZARR_PKG_CFLAGS)
  ZARR_LIBS := $(ZARR_PKG_LIBS)
else
  # DKRZ Levante spack paths (blosc uses lib64, lz4 uses lib)
  DKRZ_BLOSC := /sw/spack-levante/c-blosc-1.21.6-okwipv
  DKRZ_LZ4 := /sw/spack-levante/lz4-1.9.4-qrh4oo

  # Try DKRZ first, then brew (macOS), then /usr/local fallback
  ifneq ($(wildcard $(DKRZ_BLOSC)/include/blosc.h),)
    BLOSC_PREFIX ?= $(DKRZ_BLOSC)
    LZ4_PREFIX ?= $(DKRZ_LZ4)
    BLOSC_LIBDIR := $(BLOSC_PREFIX)/lib64
    LZ4_LIBDIR := $(LZ4_PREFIX)/lib
    ZARR_RPATH := -Wl,-rpath,$(BLOSC_LIBDIR) -Wl,-rpath,$(LZ4_LIBDIR)
  else
    BLOSC_PREFIX ?= $(shell brew --prefix c-blosc 2>/dev/null || echo "/usr/local")
    LZ4_PREFIX ?= $(shell brew --prefix lz4 2>/dev/null || echo "/usr/local")
    BLOSC_LIBDIR := $(BLOSC_PREFIX)/lib
    LZ4_LIBDIR := $(LZ4_PREFIX)/lib
    ZARR_RPATH :=
  endif

  ZARR_CFLAGS := -DHAVE_ZARR -I$(BLOSC_PREFIX)/include -I$(LZ4_PREFIX)/include
  ZARR_LIBS := -L$(BLOSC_LIBDIR) -L$(LZ4_LIBDIR) -lblosc -llz4 $(ZARR_RPATH)
endif

BASE_CFLAGS += $(ZARR_CFLAGS)
X11_FULL_CFLAGS += $(ZARR_CFLAGS)
USHOW_LIBS += $(ZARR_LIBS)
UTERM_LIBS += $(ZARR_LIBS)
endif

# GRIB support (optional) - build with: make WITH_GRIB=1
# Requires: eccodes (brew install eccodes on macOS)
ifdef WITH_GRIB
GRIB_PKG_CFLAGS := $(shell pkg-config --cflags eccodes 2>/dev/null)
GRIB_PKG_LIBS := $(shell pkg-config --libs eccodes 2>/dev/null)

ifneq ($(strip $(GRIB_PKG_CFLAGS)$(GRIB_PKG_LIBS)),)
  GRIB_CFLAGS := -DHAVE_GRIB $(GRIB_PKG_CFLAGS)
  GRIB_LIBS := $(GRIB_PKG_LIBS)
else
  # DKRZ Levante spack path (eccodes uses lib64)
  DKRZ_ECCODES := /sw/spack-levante/eccodes-2.44.0-hsksp4

  ifneq ($(wildcard $(DKRZ_ECCODES)/include/eccodes.h),)
    GRIB_PREFIX ?= $(DKRZ_ECCODES)
    GRIB_LIBDIR := $(GRIB_PREFIX)/lib64
    GRIB_RPATH := -Wl,-rpath,$(GRIB_LIBDIR)
  else
    GRIB_PREFIX ?= $(shell brew --prefix eccodes 2>/dev/null || echo "/usr/local")
    GRIB_LIBDIR := $(GRIB_PREFIX)/lib
    GRIB_RPATH :=
  endif

  GRIB_CFLAGS := -DHAVE_GRIB -I$(GRIB_PREFIX)/include
  GRIB_LIBS := -L$(GRIB_LIBDIR) -leccodes $(GRIB_RPATH)
endif

BASE_CFLAGS += $(GRIB_CFLAGS)
X11_FULL_CFLAGS += $(GRIB_CFLAGS)
USHOW_LIBS += $(GRIB_LIBS)
UTERM_LIBS += $(GRIB_LIBS)
endif

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = .

COMMON_SRCS = $(SRCDIR)/kdtree.c \
              $(SRCDIR)/mesh.c \
              $(SRCDIR)/regrid.c \
              $(SRCDIR)/file_netcdf.c \
              $(SRCDIR)/colormaps.c \
              $(SRCDIR)/view.c

USHOW_SRCS = $(SRCDIR)/ushow.c \
             $(COMMON_SRCS) \
             $(SRCDIR)/interface/x_interface.c \
             $(SRCDIR)/interface/colorbar.c \
             $(SRCDIR)/interface/range_popup.c \
             $(SRCDIR)/interface/range_utils.c \
             $(SRCDIR)/interface/timeseries_popup.c

UTERM_SRCS = $(SRCDIR)/uterm.c \
             $(SRCDIR)/term_render_mode.c \
             $(COMMON_SRCS)

# Add zarr sources if enabled
ifdef WITH_ZARR
COMMON_SRCS += $(SRCDIR)/file_zarr.c
USHOW_SRCS += $(SRCDIR)/cJSON/cJSON.c
UTERM_SRCS += $(SRCDIR)/cJSON/cJSON.c
endif

# Add grib sources if enabled
ifdef WITH_GRIB
COMMON_SRCS += $(SRCDIR)/file_grib.c
endif

USHOW_OBJS = $(USHOW_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
UTERM_OBJS = $(UTERM_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Targets
TARGET = ushow
UTERM_TARGET = uterm

.PHONY: all clean dirs test test-clean

all: dirs $(TARGET) $(UTERM_TARGET)

dirs:
	@mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/interface
ifdef WITH_ZARR
	@mkdir -p $(SRCDIR)/cJSON
	@mkdir -p $(OBJDIR)/cJSON
endif

$(TARGET): $(USHOW_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(USHOW_LIBS)
	@echo "Built $(TARGET)"

$(UTERM_TARGET): $(UTERM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(UTERM_LIBS)
	@echo "Built $(UTERM_TARGET)"

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BASE_CFLAGS) -I$(SRCDIR) -c -o $@ $<

$(OBJDIR)/interface/%.o: $(SRCDIR)/interface/%.c
	@mkdir -p $(dir $@)
	$(CC) $(X11_FULL_CFLAGS) -I$(SRCDIR) -c -o $@ $<

# cJSON object file (zarr support)
$(OBJDIR)/cJSON/cJSON.o: $(SRCDIR)/cJSON/cJSON.c
	@mkdir -p $(dir $@)
	$(CC) $(BASE_CFLAGS) -I$(SRCDIR) -c -o $@ $<

clean:
	rm -rf $(OBJDIR)
	rm -f ushow
	rm -f uterm

# Dependencies
$(OBJDIR)/ushow.o: $(SRCDIR)/ushow.c $(SRCDIR)/ushow.defines.h $(SRCDIR)/mesh.h \
                   $(SRCDIR)/regrid.h $(SRCDIR)/file_netcdf.h $(SRCDIR)/colormaps.h \
                   $(SRCDIR)/view.h $(SRCDIR)/interface/x_interface.h
$(OBJDIR)/uterm.o: $(SRCDIR)/uterm.c $(SRCDIR)/ushow.defines.h $(SRCDIR)/mesh.h \
                   $(SRCDIR)/regrid.h $(SRCDIR)/file_netcdf.h $(SRCDIR)/colormaps.h \
                   $(SRCDIR)/term_render_mode.h \
                   $(SRCDIR)/view.h
$(OBJDIR)/term_render_mode.o: $(SRCDIR)/term_render_mode.c $(SRCDIR)/term_render_mode.h
$(OBJDIR)/kdtree.o: $(SRCDIR)/kdtree.c $(SRCDIR)/kdtree.h
$(OBJDIR)/mesh.o: $(SRCDIR)/mesh.c $(SRCDIR)/mesh.h $(SRCDIR)/ushow.defines.h
$(OBJDIR)/regrid.o: $(SRCDIR)/regrid.c $(SRCDIR)/regrid.h $(SRCDIR)/mesh.h \
                    $(SRCDIR)/kdtree.h $(SRCDIR)/ushow.defines.h
$(OBJDIR)/file_netcdf.o: $(SRCDIR)/file_netcdf.c $(SRCDIR)/file_netcdf.h $(SRCDIR)/ushow.defines.h
$(OBJDIR)/colormaps.o: $(SRCDIR)/colormaps.c $(SRCDIR)/colormaps.h $(SRCDIR)/ushow.defines.h
$(OBJDIR)/view.o: $(SRCDIR)/view.c $(SRCDIR)/view.h $(SRCDIR)/file_netcdf.h \
                  $(SRCDIR)/regrid.h $(SRCDIR)/colormaps.h $(SRCDIR)/ushow.defines.h
$(OBJDIR)/interface/x_interface.o: $(SRCDIR)/interface/x_interface.c \
                                    $(SRCDIR)/interface/x_interface.h \
                                    $(SRCDIR)/interface/colorbar.h \
                                    $(SRCDIR)/interface/timeseries_popup.h \
                                    $(SRCDIR)/ushow.defines.h
$(OBJDIR)/interface/colorbar.o: $(SRCDIR)/interface/colorbar.c \
                                 $(SRCDIR)/interface/colorbar.h $(SRCDIR)/colormaps.h
$(OBJDIR)/interface/range_popup.o: $(SRCDIR)/interface/range_popup.c \
                                    $(SRCDIR)/interface/range_popup.h \
                                    $(SRCDIR)/interface/range_utils.h
$(OBJDIR)/interface/range_utils.o: $(SRCDIR)/interface/range_utils.c \
                                    $(SRCDIR)/interface/range_utils.h
$(OBJDIR)/interface/timeseries_popup.o: $(SRCDIR)/interface/timeseries_popup.c \
                                         $(SRCDIR)/interface/timeseries_popup.h \
                                         $(SRCDIR)/ushow.defines.h

# Zarr dependencies (when WITH_ZARR is set)
ifdef WITH_ZARR
$(OBJDIR)/file_zarr.o: $(SRCDIR)/file_zarr.c $(SRCDIR)/file_zarr.h $(SRCDIR)/ushow.defines.h \
                        $(SRCDIR)/cJSON/cJSON.h
$(OBJDIR)/cJSON/cJSON.o: $(SRCDIR)/cJSON/cJSON.c $(SRCDIR)/cJSON/cJSON.h
endif

# GRIB dependencies (when WITH_GRIB is set)
ifdef WITH_GRIB
$(OBJDIR)/file_grib.o: $(SRCDIR)/file_grib.c $(SRCDIR)/file_grib.h $(SRCDIR)/ushow.defines.h
endif

# Print configuration
info:
	@echo "CC: $(CC)"
	@echo "BASE_CFLAGS: $(BASE_CFLAGS)"
	@echo "X11_FULL_CFLAGS: $(X11_FULL_CFLAGS)"
	@echo "USHOW_LIBS: $(USHOW_LIBS)"
	@echo "UTERM_LIBS: $(UTERM_LIBS)"

# Test targets
test:
	@$(MAKE) -C tests run-tests

test-clean:
	@$(MAKE) -C tests clean
