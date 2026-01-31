# ushow - Unstructured Data Viewer

A fast, ncview-like visualization tool for structured and unstructured geoscientific data.

## Features

- **Unified data handling**: Treats all data as collections of points with lon/lat coordinates
- **Fast visualization**: KDTree-based nearest-neighbor interpolation to regular grid
- **X11/Xaw interface**: Works over SSH with X forwarding
- **Animation support**: Step through time dimensions
- **Multiple colormaps**: jet, viridis, hot, grayscale

## Building

Requirements:
- NetCDF-C library
- X11 development libraries (libX11, libXt, libXaw)
- C compiler (gcc or clang)

```bash
cd ushow
make
```

## Usage

```bash
./ushow [options] <data_file.nc>

Options:
  -m, --mesh <file>      Mesh file with coordinates (for unstructured data)
  -r, --resolution <deg> Target grid resolution in degrees (default: 1.0)
  -i, --influence <m>    Influence radius in meters (default: 200000)
  -d, --delay <ms>       Animation frame delay in milliseconds (default: 200)
  -h, --help             Show help message
```

### Examples

FESOM unstructured data with separate mesh file:
```bash
./ushow temp.fesom.1964.nc -m fesom.mesh.diag.nc
```

Standard NetCDF with embedded coordinates:
```bash
./ushow sst.nc
```

Higher resolution display:
```bash
./ushow data.nc -r 0.5  # 0.5 degree grid (720x360)
```

## Interface

- **Variable buttons**: Click to select which variable to display
- **Animation controls**:
  - `< Back`: Step backward one timestep
  - `|| Pause`: Stop animation
  - `Fwd >`: Start forward animation
- **Time/Depth sliders**: Navigate through dimensions
- **Colormap button**: Click to cycle through colormaps

## Data Flow

1. Load mesh coordinates (from mesh file or data file)
2. Convert lon/lat to Cartesian coordinates on unit sphere
3. Build KDTree from source points (one-time)
4. For each target grid cell, find nearest source point (one-time)
5. Per frame: read data slice, apply regrid indices, convert to pixels

## Supported Data Formats

- **NetCDF**: Full support (NetCDF-3 and NetCDF-4)
- **Zarr**: Planned for future

## Coordinate Detection

Automatically searches for coordinate variables by common names:
- Longitude: lon, longitude, x, nav_lon, glon, xt_ocean, xu_ocean, xh, xq
- Latitude: lat, latitude, y, nav_lat, glat, yt_ocean, yu_ocean, yh, yq

## Dimension Detection

Automatically identifies dimension roles:
- Time: time, t
- Depth: depth, z, lev, level, nz, nz1
- Nodes: nod2, nod2d, node, nodes, ncells, npoints

## Performance

- KDTree built once per mesh, cached for all frames
- Regrid indices precomputed once per resolution
- Only current 2D slice loaded per frame (~500KB vs full ~290MB for typical data)
- Efficient nearest-neighbor interpolation (index lookup)

## License

BSD-style license (same as ncview)
