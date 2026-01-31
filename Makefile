# Makefile for ushow
#
# Unstructured data visualization tool

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

# NetCDF
NETCDF_CFLAGS := $(shell nc-config --cflags 2>/dev/null || pkg-config --cflags netcdf 2>/dev/null)
NETCDF_LIBS := $(shell nc-config --libs 2>/dev/null || pkg-config --libs netcdf 2>/dev/null || echo "-lnetcdf")

# X11
X11_CFLAGS := $(shell pkg-config --cflags x11 xt xaw7 2>/dev/null || echo "-I/usr/X11/include -I/opt/X11/include")
X11_LIBS := $(shell pkg-config --libs x11 xt xaw7 2>/dev/null || echo "-L/usr/X11/lib -L/opt/X11/lib -lX11 -lXt -lXaw")

CFLAGS += $(NETCDF_CFLAGS) $(X11_CFLAGS)
LIBS = $(NETCDF_LIBS) $(X11_LIBS) -lm

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

.PHONY: all clean dirs

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
