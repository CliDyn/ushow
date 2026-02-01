# Makefile for ushow
#
# Unstructured data visualization tool

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -Wl,--enable-new-dtags

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

X11_CFLAGS := $(shell pkg-config --cflags x11 xt xaw7 2>/dev/null)
X11_LIBS := $(shell pkg-config --libs x11 xt xaw7 2>/dev/null)
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

CFLAGS += $(NETCDF_CFLAGS) $(X11_CFLAGS)
LIBS = $(NETCDF_LIBS) $(NETCDF_RPATH) $(X11_LIBS) $(X11_RPATH) -lm

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = .

# Sources
SRCS = $(SRCDIR)/ushow.c \
       $(SRCDIR)/kdtree.c \
       $(SRCDIR)/mesh.c \
       $(SRCDIR)/regrid.c \
       $(SRCDIR)/file_netcdf.c \
       $(SRCDIR)/colormaps.c \
       $(SRCDIR)/view.c \
       $(SRCDIR)/interface/x_interface.c \
       $(SRCDIR)/interface/colorbar.c

# Objects
OBJS = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Target
TARGET = $(BINDIR)/ushow

.PHONY: all clean dirs test test-clean

all: dirs $(TARGET)

dirs:
	@mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/interface

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Built $(TARGET)"

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -c -o $@ $<

$(OBJDIR)/interface/%.o: $(SRCDIR)/interface/%.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -c -o $@ $<

clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET)

# Dependencies
$(OBJDIR)/ushow.o: $(SRCDIR)/ushow.c $(SRCDIR)/ushow.defines.h $(SRCDIR)/mesh.h \
                   $(SRCDIR)/regrid.h $(SRCDIR)/file_netcdf.h $(SRCDIR)/colormaps.h \
                   $(SRCDIR)/view.h $(SRCDIR)/interface/x_interface.h
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
                                    $(SRCDIR)/interface/colorbar.h $(SRCDIR)/ushow.defines.h
$(OBJDIR)/interface/colorbar.o: $(SRCDIR)/interface/colorbar.c \
                                 $(SRCDIR)/interface/colorbar.h $(SRCDIR)/colormaps.h

# Print configuration
info:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LIBS: $(LIBS)"
	@echo "SRCS: $(SRCS)"
	@echo "OBJS: $(OBJS)"

# Test targets
test:
	@$(MAKE) -C tests run-tests

test-clean:
	@$(MAKE) -C tests clean
