# ushow - Unstructured Data Viewer
The `ushow` command uses X11 to display data:
![usow_example](https://github.com/user-attachments/assets/125165e5-b3e1-4cb4-a728-5f39cf7838b3)

The `uterm` command displays data in the terminal using ASCII or UTF characters.
![uterm](https://github.com/user-attachments/assets/28720ba7-173f-428a-a02a-61582aee6aec)

A fast, ncview‑inspired visualization tool for structured and unstructured geoscientific data.

## Features

- **Multiple formats**: Supports netCDF, zarr and GRIB
- **Unified data handling**: Treats all data as collections of points with lon/lat coordinates
- **Fast visualization**: KDTree-based nearest-neighbor interpolation to regular grid
- **X11/Xaw interface**: Works over SSH with X forwarding
- **Terminal quick-look mode**: Separate `uterm` binary with raw terminal interaction (no X is needed)
- **Animation support**: Step through time dimensions
- **Multiple colormaps**: viridis, hot, grayscale, plus the full cmocean set

## Building

Requirements:
- NetCDF-C library
- eccodes for GRIB support (can be build without)
- X11 development libraries (libX11, libXt, libXaw, libXmu, libXext, libSM, libICE)
- C compiler (gcc or clang)

### macOS (Homebrew + XQuartz)

Install dependencies:
```bash
brew install netcdf
```

For optional Zarr support:
```bash
brew install c-blosc lz4
```

For optional GRIB support:
```bash
brew install eccodes
```

Install [XQuartz](https://www.xquartz.org/) for X11 support. After installation, the X11 libraries will be in `/opt/X11`.

Build:
```bash
make                  # Without zarr/grib support
make WITH_ZARR=1      # With zarr support
make WITH_GRIB=1      # With grib support
make uterm            # Build terminal viewer only
```

The Makefile auto-detects XQuartz at `/opt/X11`.

### Linux (Debian/Ubuntu)

Install dependencies:
```bash
sudo apt-get install libnetcdf-dev libx11-dev libxt-dev libxaw7-dev libxmu-dev libxext-dev
```

For optional Zarr support:
```bash
sudo apt-get install libblosc-dev liblz4-dev
```

For optional GRIB support:
```bash
sudo apt-get install libeccodes-dev
```

Build:
```bash
make                  # Without zarr/grib support
make WITH_ZARR=1      # With zarr support
make WITH_GRIB=1      # With grib support
make uterm            # Build terminal viewer only
```

### DKRZ Levante

On Levante, the Makefile automatically uses the DKRZ spack-installed libraries:
- NetCDF-C from `/sw/spack-levante/netcdf-c-4.8.1-qk24yp`
- X11 libraries from `/sw/spack-levante/libx*`

No modules need to be loaded. Simply run:
```bash
make                  # Without zarr/grib support
make WITH_ZARR=1      # With zarr support (uses system blosc/lz4)
make WITH_GRIB=1      # With grib support (uses system eccodes)
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
./ushow [options] <data_file.nc|data.zarr|data.grib> [file2 ...]

Options:
  -m, --mesh <file>      Mesh file with coordinates (for unstructured data)
  -r, --resolution <deg> Target grid resolution in degrees (default: 1.0)
  -i, --influence <m>    Influence radius in meters (default: 200000)
  -d, --delay <ms>       Animation frame delay in milliseconds (default: 200)
  -h, --help             Show help message
```

Terminal quick-look mode:
```bash
./uterm [options] <data_file.nc|data.zarr|data.grib> [file2 ...]

Options (uterm):
  -m, --mesh <file>      Mesh file with coordinates
  -r, --resolution <deg> Target grid resolution in degrees (default: 1.0)
  -i, --influence <m>    Influence radius in meters (default: 200000)
  -d, --delay <ms>       Animation frame delay in milliseconds (default: 200)
  --chars <ramp>     ASCII ramp (default: " .:-=+*#%@")
  --render <mode>    Render mode: ascii | half | braille
  --color            Force ANSI color output
  --no-color         Disable ANSI color output
  -h, --help             Show help
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

Multi-file time concatenation (NetCDF):
```bash
./ushow "temp.fesom.*.nc" -m mesh.nc   # Glob pattern
./ushow file1.nc file2.nc -m mesh.nc   # Explicit files
```

Zarr store (requires `make WITH_ZARR=1`):
```bash
./ushow data.zarr                      # Single zarr store
./ushow "data_*.zarr"                  # Multiple zarr stores (time concat)
./ushow data.zarr -r 0.25              # Higher resolution display
```

GRIB file (requires `make WITH_GRIB=1`):
```bash
./ushow data.grib                      # Single GRIB file
./uterm data.grib --color
```

Zarr store with consolidated metadata (faster loading):
```bash
# Zarr stores with .zmetadata file are loaded more efficiently
./ushow output.zarr                    # Auto-detects consolidated metadata
```

Terminal mode examples:
```bash
./uterm temp.fesom.1964.nc -m fesom.mesh.diag.nc
./uterm data.zarr --color
./uterm "temp.fesom.*.nc" -m mesh.nc -d 120
./uterm data.nc --render half
./uterm data.nc --render braille --color
```
## Testing

Run the test suite:
```bash
make test
```

Clean test binaries:
```bash
make test-clean
```

The test suite includes:
- **test_kdtree**: Spatial indexing and nearest-neighbor queries
- **test_mesh**: Coordinate transformations (lon/lat to Cartesian)
- **test_regrid**: Interpolation to regular grids
- **test_colormaps**: Color mapping functions
- **test_term_render_mode**: Terminal render mode parsing/cycling helpers
- **test_range_popup**: Range popup logic (symmetric computation, value parsing)
- **test_timeseries**: Time series reading, multi-file concatenation, and CF time unit conversion
- **test_file_netcdf**: NetCDF file I/O
- **test_file_zarr**: Zarr file I/O (when built with `WITH_ZARR=1`)
- **test_integration**: End-to-end workflow tests


## Interface

- **Variable buttons**: Click to select which variable to display
- **Animation controls**:
  - `< Back`: Step backward one timestep
  - `|| Pause`: Stop animation
  - `Fwd >`: Start forward animation
- **Time/Depth sliders**: Navigate through dimensions
- **Colormap button**: Click to cycle through colormaps
- **Min-/Min+/Max-/Max+**: Adjust display range in 10% steps
- **Range button**: Opens a popup dialog to set the display range explicitly:
  - **Minimum/Maximum**: Editable text fields for exact values
  - **Symmetric about Zero**: Sets range to [-max(|min|,|max|), max(|min|,|max|)]
  - **Reset to Global Values**: Restores the variable's full data range
- **Time series plot**: Click on the image to show a time series popup at that location:
  - Displays value vs time for the selected variable at the clicked grid point
  - Y-axis with numeric tick labels, X-axis with CF time date formatting (when detected)
  - Blue data line with dots at data points; gaps shown for fill/missing values
  - Works with both single files and multi-file datasets
  - When files have different time epochs, values are automatically normalized to a common reference
- **Dimension panel**: Shows dimension names, ranges, current values
- **Colorbar**: Min/max and intermediate labels update as you adjust range

## Terminal Controls (`uterm`)

- `q`: quit
- `space`: pause/resume animation
- `j` / `k`: previous/next time step
- `u` / `i`: previous/next depth level
- `n` / `p`: next/previous variable
- `1`..`9`: direct variable select (first 9 variables)
- `c` / `C`: next/previous colormap
- `m`: cycle render mode (`ascii` -> `half` -> `braille`)
- `[` / `]`: decrease/increase display minimum
- `{` / `}`: decrease/increase display maximum
- `r`: reset min/max to estimated global range
- `s`: save current frame as PPM (`<var>_t<time>_d<depth>.ppm`)
- `?`: toggle extended help line

## Data Flow

1. Load mesh coordinates (from mesh file or data file)
2. Convert lon/lat to Cartesian coordinates on unit sphere
3. Build KDTree from source points (one-time)
4. For each target grid cell, find nearest source point (one-time)
5. Per frame: read data slice, apply regrid indices, convert to pixels

## Supported Data Formats

- **NetCDF**: Full support (NetCDF-3 and NetCDF-4)
- **Zarr**: v2 format (requires `make WITH_ZARR=1`)
  - Compression: LZ4, Blosc (with various inner codecs), or uncompressed
  - Data types: Float32, Float64, Int64
  - Supports consolidated metadata (.zmetadata) for faster loading
  - Supports unstructured data (ICON, FESOM, etc.)
  - Reads coordinates from embedded latitude/longitude arrays
  - Dimension metadata via `_ARRAY_DIMENSIONS` attribute (xarray convention)
  - Multi-file time concatenation supported via glob patterns
  - Supports multi-chunk arrays (coordinates and data can be split across multiple chunks)

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
