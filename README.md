# ushow - Unstructured Data Viewer
The `ushow` command uses X11 to display data:
![usow_example](https://github.com/user-attachments/assets/125165e5-b3e1-4cb4-a728-5f39cf7838b3)

The `uterm` command displays data in the terminal using ASCII or UTF characters.
![uterm](https://github.com/user-attachments/assets/28720ba7-173f-428a-a02a-61582aee6aec)

A fast, ncview‑inspired visualization tool for structured and unstructured geoscientific data.

## Features

- **Multiple formats**: Supports netCDF, zarr, GRIB, and MITgcm binary (MDS)
- **Unified data handling**: Treats all data as collections of points with lon/lat coordinates
- **Fast visualization**: KDTree-based nearest-neighbor interpolation to regular grid
- **X11/Xaw interface**: Works over SSH with X forwarding, dark/light theme
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

For optional YAC interpolation support (professional-grade interpolation methods):
```bash
brew install open-mpi autoconf automake libtool
```
Then build YAXT and YAC from source (see [YAC Interpolation](#yac-interpolation) below).

Install [XQuartz](https://www.xquartz.org/) for X11 support. After installation, the X11 libraries will be in `/opt/X11`.

Build:
```bash
make                  # Without zarr/grib support
make WITH_ZARR=1      # With zarr support
make WITH_GRIB=1      # With grib support
make WITH_YAC=1       # With YAC interpolation support
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

For optional YAC interpolation support:
```bash
sudo apt-get install libopenmpi-dev autoconf automake libtool
```
Then build YAXT and YAC from source (see [YAC Interpolation](#yac-interpolation) below).

Build:
```bash
make                  # Without zarr/grib support
make WITH_ZARR=1      # With zarr support
make WITH_GRIB=1      # With grib support
make WITH_YAC=1       # With YAC interpolation support
make uterm            # Build terminal viewer only
```

### DKRZ Levante

On Levante, the Makefile automatically uses the DKRZ spack-installed libraries:
- NetCDF-C from `/sw/spack-levante/netcdf-c-4.8.1-qk24yp`
- X11 libraries from `/sw/spack-levante/libx*`
- c-blosc and lz4 for Zarr support
- eccodes for GRIB support

For basic builds (without YAC), no modules need to be loaded:
```bash
make                  # Without zarr/grib support
make WITH_ZARR=1      # With zarr support (uses system blosc/lz4)
make WITH_GRIB=1      # With grib support (uses system eccodes)
```

For YAC support, load the OpenMPI module (provides `mpicc`) and build YAXT/YAC first (see [YAC Interpolation](#yac-interpolation)):
```bash
module load openmpi/4.1.2-gcc-11.2.0
make WITH_YAC=1       # With YAC interpolation support
```

Full build with all optional features:
```bash
module load openmpi/4.1.2-gcc-11.2.0
make WITH_GRIB=1 WITH_YAC=1 WITH_ZARR=1
```

The binary will have the library paths embedded (via rpath), so it runs without setting `LD_LIBRARY_PATH`.

### YAC Interpolation

YAC (Yet Another Coupler) provides professional-grade interpolation methods beyond the built-in nearest-neighbor. It requires building two libraries from source: YAXT and YAC.

On DKRZ Levante, load the OpenMPI module first:
```bash
module load openmpi/4.1.2-gcc-11.2.0
```

#### 1. Build YAXT

Get YAXT source from https://swprojects.dkrz.de/redmine/projects/yaxt (or `git clone https://gitlab.dkrz.de/dkrz-sw/yaxt.git`).

```bash
cd /path/to/yaxt
scripts/reconfigure
mkdir build && cd build
../configure \
  MPI_LAUNCH="mpirun --map-by socket:OVERSUBSCRIBE" \
  --prefix=$HOME/local/yaxt \
  --disable-shared \
  CC=mpicc FC=mpif90
make -j4
make install
```

On macOS, a cosmetic `date: illegal time format` warning during `scripts/reconfigure` can be ignored.

#### 2. Build YAC

Get YAC source from https://gitlab.dkrz.de/dkrz-sw/yac (version 3.14.0 or later).

```bash
cd /path/to/yac
./autogen.sh
./configure \
  --prefix=$HOME/local/yac \
  --with-yaxt-root=$HOME/local/yaxt \
  --disable-mci --disable-utils --disable-examples \
  --disable-tools --disable-fortran-bindings \
  CC=mpicc
make -j4
make install
```

On macOS, if configure fails with a shell error, prefix the command with `CONFIG_SHELL=/bin/bash /bin/bash`.

#### 3. Build ushow with YAC

```bash
make clean
make WITH_YAC=1                              # Auto-detects at ~/local/yac
make WITH_YAC=1 YAC_PREFIX=/custom/path/yac  # Custom install location
```

The Makefile uses `pkg-config` to find YAC, with a fallback to `YAC_PREFIX` (default: `$HOME/local/yac`).

### AWI Albedo

On Albedo, dependencies are provided via environment modules and spack. First load spack and the NetCDF module:
```bash
module load spack
module load netcdf-c/4.8.1-gcc12.1.0
```

Load the X11 development libraries from spack:
```bash
spack load /eub564f /gyimrqa /dp6g46v /aioyu3n /mxnurir /l6kzj5s /x75vrux
```

Basic build:
```bash
make
```

For optional Zarr support, load c-blosc and lz4 (c-blosc may need to be installed into your home directory first with `spack install c-blosc%gcc@12.1.0`):
```bash
spack load c-blosc%gcc@12.1.0
spack load /ahjcumd   # lz4
```

For optional GRIB support, load eccodes:
```bash
spack load /fi5kc7g   # eccodes 2.34.0
```

Build with Zarr and GRIB support (explicit paths needed because the Makefile's auto-detection does not cover Albedo):
```bash
make WITH_ZARR=1 WITH_GRIB=1 \
  ZARR_CFLAGS="-DHAVE_ZARR -I$HOME/.spack/sw/c-blosc/1.21.5-rrsl7wt/include -I/albedo/soft/sw/spack-sw/lz4/1.9.3-ahjcumd/include" \
  ZARR_LIBS="-L$HOME/.spack/sw/c-blosc/1.21.5-rrsl7wt/lib64 -L/albedo/soft/sw/spack-sw/lz4/1.9.3-ahjcumd/lib -lblosc -llz4 -Wl,-rpath,$HOME/.spack/sw/c-blosc/1.21.5-rrsl7wt/lib64 -Wl,-rpath,/albedo/soft/sw/spack-sw/lz4/1.9.3-ahjcumd/lib"
```

The binary will have library paths embedded via rpath, so no `LD_LIBRARY_PATH` is needed at runtime.

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
./ushow [options] <data_file.nc|data.zarr|data.grib|mitgcm_dir> [file2 ...]

Options:
  -m, --mesh <file>      Mesh file with coordinates (for unstructured data)
  -r, --resolution <deg> Target grid resolution in degrees (default: 1.0)
  -i, --influence <m>    Influence radius in meters (default: 80000)
  -d, --delay <ms>       Animation frame delay in milliseconds (default: 200)
  -p, --polygon-only     Skip regridding, use polygon mode only (faster)
  --box W,E,S,N          Regional box (e.g. --box -10,30,35,70 for Europe)
  --polar <pole>         Polar LAEA projection (north or south)
  --cutoff <deg>         Cutoff latitude for polar view (default: 60)
  --light                Use light theme (default: dark)
  --yac                  Use YAC interpolation with default method (avg_arith)
  --yac-method <method>  Use YAC interpolation with specific method;
                         click the method button in the GUI to cycle methods at runtime
  --yac-3d               Fractional fill-value masking for 3D variables
  -h, --help             Show help message
```

Terminal quick-look mode:
```bash
./uterm [options] <data_file.nc|data.zarr|data.grib> [file2 ...]

Options (uterm):
  -m, --mesh <file>      Mesh file with coordinates
  -r, --resolution <deg> Target grid resolution in degrees (default: 1.0)
  -i, --influence <m>    Influence radius in meters (default: 80000)
  -d, --delay <ms>       Animation frame delay in milliseconds (default: 200)
  --chars <ramp>     ASCII ramp (default: " .:-=+*#%@")
  --render <mode>    Render mode: ascii | half | braille
  --color            Force ANSI color output
  --no-color         Disable ANSI color output
  --box W,E,S,N      Regional box (e.g. --box -10,30,35,70)
  --polar <pole>     Polar LAEA projection (north or south)
  --cutoff <deg>     Cutoff latitude for polar view (default: 60)
  --yac              Use YAC interpolation (default: avg_arith)
  --yac-method <m>   Use YAC interpolation method (requires WITH_YAC=1)
  --yac-3d           Fractional fill-value masking for 3D variables
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

MITgcm binary data (MDS format, always compiled — no extra libraries):
```bash
./ushow /path/to/mitgcm/run           # Open directory with .data/.meta files
./ushow /path/to/diags3D.0000008760.data  # Open specific .data file (uses parent dir)
```

MPAS unstructured data:
```bash
./ushow data.nc grid.nc                # MPAS UGRID (face_lon/face_lat)
./ushow mpas_output.nc                 # Native MPAS (lonCell/latCell)
./ushow mpas_output.nc --yac -r 0.1    # MPAS with YAC interpolation (uses native cellsOnVertex connectivity)
```

YAC interpolation (requires `make WITH_YAC=1`):
```bash
./ushow temp.nc -m mesh.nc --yac                    # Default avg_arith, cycle in GUI
./ushow temp.nc -m mesh.nc --yac-method nnn4dist    # 4-NN distance-weighted
./ushow temp.nc -m mesh.nc --yac-method nnn4gauss   # 4-NN Gaussian-weighted
./ushow temp.nc -m mesh.nc --yac-method avg_arith   # Cell averaging
./ushow temp.nc -m mesh.nc --yac-3d                 # Fractional masking for 3D ocean data
./ushow temp.nc -m mesh.nc --yac-3d --yac-method nnn4dist  # Combine with specific method
```

Zarr store with consolidated metadata (faster loading):
```bash
# Zarr stores with .zmetadata file are loaded more efficiently
./ushow output.zarr                    # Auto-detects consolidated metadata
```

Regional box (restrict view to a geographic region):
```bash
./ushow data.nc --box -10,30,35,70               # Europe/Mediterranean
./ushow data.nc --box -80,0,20,60                 # North Atlantic
./uterm data.nc --box 120,290,-30,30 --color      # Tropical Pacific
```

Polar projections (Lambert Azimuthal Equal-Area):
```bash
./ushow data.nc --polar north                     # Arctic, default 60° cutoff
./ushow data.nc --polar south                     # Antarctic, default 60° cutoff
./ushow data.nc --polar north --cutoff 50         # Wider view (down to 50°N)
./uterm data.nc --polar south --cutoff 70 --color # Narrow Antarctic view
./ushow data.nc --polar north -r 0.5              # High-res polar view
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
- **test_projection**: LAEA projection math, regional box, and polar grid tests
- **test_colormaps**: Color mapping functions
- **test_term_render_mode**: Terminal render mode parsing/cycling helpers
- **test_range_popup**: Range popup logic (symmetric computation, value parsing)
- **test_timeseries**: Time series reading, multi-file concatenation, and CF time unit conversion
- **test_file_netcdf**: NetCDF file I/O
- **test_file_mitgcm**: MITgcm MDS binary file I/O
- **test_file_zarr**: Zarr file I/O (when built with `WITH_ZARR=1`)
- **test_yac_3d**: Fractional fill-value masking for YAC interpolation (when built with `WITH_YAC=1`)
- **test_yac_click**: Time series click-to-node lookup in YAC mode (when built with `WITH_YAC=1`)
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
  - Works in both KDTree and YAC interpolation modes (`--yac`, `--yac-3d`)
  - When files have different time epochs, values are automatically normalized to a common reference
- **Render mode / YAC method button**: Normally toggles Interp/Polygon mode. When YAC is active (`--yac-method`), cycles through interpolation methods instead: `avg_arith` -> `avg_dist` -> `avg_bary` -> `nnn1` -> `nnn4dist` -> `nnn4gauss`
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

## Interpolation Methods

By default, ushow uses a fast KDTree nearest-neighbor lookup to map unstructured data onto a regular grid. With YAC support (`make WITH_YAC=1`), additional interpolation methods are available via `--yac-method`. Once launched, click the method button in the GUI to cycle through methods at runtime:

| Method | Description |
|--------|-------------|
| `nnn1` | Single nearest neighbor (similar to default KDTree) |
| `nnn4dist` | 4 nearest neighbors, distance-weighted |
| `nnn4gauss` | 4 nearest neighbors, Gaussian-weighted |
| `avg_arith` | Cell averaging, arithmetic mean |
| `avg_dist` | Cell averaging, distance-weighted |
| `avg_bary` | Cell averaging, barycentric |

NNN methods work with any grid type. Averaging methods require element connectivity — for grids that lack it (reduced Gaussian, HEALPix, etc.), ushow auto-generates a triangulation from latitude bands. For MPAS grids, native dual-mesh connectivity (`cellsOnVertex`) is read directly from the file.

### Fractional Fill-Value Masking (`--yac-3d`)

For 3D ocean variables (e.g., temperature at 50 depth levels), deeper levels have more land masking — fewer valid source points. Without `--yac-3d`, a single interpolation is used for all depths, so fill values from land points can bleed into the result at deeper levels.

With `--yac-3d`, ushow generates a dynamic fractional mask from the source field before each interpolation step: valid points receive weight 1.0, fill points 0.0. The mask is passed to YAC's fractional interpolation (`yac_interpolation_execute_frac`), which rescales partially-masked target cells and assigns the fill value to fully-masked cells. This produces clean coastlines at every depth level.

## Data Flow

1. Load mesh coordinates (from mesh file or data file)
2. Convert lon/lat to Cartesian coordinates on unit sphere
3. Build KDTree from source points (one-time), or compute YAC interpolation weights
4. For each target grid cell, find nearest source point (one-time)
5. Per frame: read data slice, apply regrid indices, convert to pixels

## Supported Data Formats

- **NetCDF**: Full support (NetCDF-3 and NetCDF-4)
- **MITgcm MDS**: Binary `.data`/`.meta` file pairs (always compiled, no extra libraries)
  - Reads grid coordinates from XC/YC binary files
  - Supports 2D and 3D diagnostic fields with multiple variables per file
  - Automatic land masking via hFacC grid file
  - Depth levels from RC grid file
  - Multi-timestep iteration discovery
  - Automatic velocity rotation on LLC/cubed-sphere grids: if AngleCS/AngleSN (or CS/SN) grid files are present, UVEL/VVEL and other paired velocity fields are rotated from local grid coordinates to geographic (eastward/northward) components
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
- Longitude: lon, longitude, x, nav_lon, glon, xt_ocean, xu_ocean, xh, xq, face_lon, node_lon, edge_lon, lonCell, lonVertex, lonEdge
- Latitude: lat, latitude, y, nav_lat, glat, yt_ocean, yu_ocean, yh, yq, face_lat, node_lat, edge_lat, latCell, latVertex, latEdge

## Dimension Detection

Automatically identifies dimension roles:
- Time: time, t
- Depth: depth, z, lev, level, nz, nz1
- Nodes: nod2, nod2d, node, nodes, ncells, npoints, n_node, n_face, n_edge, nCells, nVertices, nEdges

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
