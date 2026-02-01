# ushow - Unstructured Data Viewer

A fast, ncview‑inspired visualization tool for structured and unstructured geoscientific data.

## Features

- **Unified data handling**: Treats all data as collections of points with lon/lat coordinates
- **Fast visualization**: KDTree-based nearest-neighbor interpolation to regular grid
- **X11/Xaw interface**: Works over SSH with X forwarding
- **Animation support**: Step through time dimensions
- **Multiple colormaps**: viridis, hot, grayscale, plus the full cmocean set

## Building

Requirements:
- NetCDF-C library
- X11 development libraries (libX11, libXt, libXaw, libXmu, libXext, libSM, libICE)
- C compiler (gcc or clang)

### macOS (Homebrew + XQuartz)

Install dependencies:
```bash
brew install netcdf
```

Install [XQuartz](https://www.xquartz.org/) for X11 support. After installation, the X11 libraries will be in `/opt/X11`.

Build:
```bash
make
```

The Makefile auto-detects XQuartz at `/opt/X11`.

### Linux (Debian/Ubuntu)

Install dependencies:
```bash
sudo apt-get install libnetcdf-dev libx11-dev libxt-dev libxaw7-dev libxmu-dev libxext-dev
```

Build:
```bash
make
```

### DKRZ Levante

On Levante, the Makefile automatically uses the DKRZ spack-installed libraries:
- NetCDF-C from `/sw/spack-levante/netcdf-c-4.8.1-qk24yp`
- X11 libraries from `/sw/spack-levante/libx*`

No modules need to be loaded. Simply run:
```bash
make
```

The binary will have the library paths embedded (via rpath), so it runs without setting `LD_LIBRARY_PATH`.

### Custom Library Paths

If your libraries are in non-standard locations, you can override the detection:

```bash
# Custom nc-config location
make NC_CONFIG=/path/to/nc-config

# Custom X11 prefix (libraries in $X11_PREFIX/lib, headers in $X11_PREFIX/include)
make X11_PREFIX=/path/to/x11

# Both
make NC_CONFIG=/path/to/nc-config X11_PREFIX=/path/to/x11
```

### Verifying the Build

After building, verify all libraries are found:
```bash
ldd ./ushow          # Linux
otool -L ./ushow     # macOS
```

No libraries should show as "not found".

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
- **Dimension panel**: Shows dimension names, ranges, current values
- **Colorbar**: Min/max and intermediate labels update as you adjust range

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

## Output

- **Screenshot/Save**: Use the Save button to write a PPM image for the current variable/time/depth.
- Output filenames are auto-generated as: `<var>_t<time>_d<depth>.ppm`

## Troubleshooting

### X11 Issues

- **X11 on macOS**: Install [XQuartz](https://www.xquartz.org/) and ensure it is running before launching ushow.
- **SSH**: Use `ssh -X` (or `ssh -Y` for trusted forwarding) to enable X11 forwarding.
- **DISPLAY not set**: Ensure the `DISPLAY` environment variable is set. On SSH, this is set automatically with `-X`.

### Library Issues

- **"cannot open shared object file"**: The runtime linker cannot find a library. The Makefile embeds library paths via rpath, but if you move the binary or libraries, you may need to set `LD_LIBRARY_PATH`:
  ```bash
  export LD_LIBRARY_PATH=/path/to/libs:$LD_LIBRARY_PATH
  ./ushow data.nc
  ```

- **Symbol lookup error**: This usually indicates mixing libraries from different installations (e.g., system vs conda). Rebuild with libraries from a single source:
  ```bash
  make clean
  make NC_CONFIG=/path/to/consistent/nc-config X11_PREFIX=/path/to/consistent/x11
  ```

- **Verify library paths**: Use `ldd ./ushow` (Linux) or `otool -L ./ushow` (macOS) to check which libraries are being loaded.

### Build Issues

- **Missing headers**: Install the development packages for the missing library. On Debian/Ubuntu, these typically end in `-dev`.
- **nc-config not found**: Install NetCDF-C or specify the path: `make NC_CONFIG=/path/to/nc-config`

## Performance

- KDTree built once per mesh, cached for all frames
- Regrid indices precomputed once per resolution
- Only current 2D slice loaded per frame (~500KB vs full ~290MB for typical data)
- Efficient nearest-neighbor interpolation (index lookup)

## Acknowledgments

ushow’s interface design is inspired by **ncview**.

cmocean colormap tables are sourced from the cmocean-ncmaps project (https://gitlab.dkrz.de/m300575/cmocean-ncmaps). Thanks to Lukas Kluft for providing the ncmaps data.

## License

GPL-3.0
