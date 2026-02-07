/*
 * ushow.defines.h - Data structures and constants for ushow
 *
 * Unstructured data visualization tool
 */

#ifndef USHOW_DEFINES_H
#define USHOW_DEFINES_H

#include <stddef.h>

/* Constants */
#define EARTH_RADIUS_M      6371000.0
#define DEG2RAD             (M_PI / 180.0)
#define RAD2DEG             (180.0 / M_PI)

/* Default target grid resolution */
#define DEFAULT_TARGET_NX   360
#define DEFAULT_TARGET_NY   180
#define DEFAULT_RESOLUTION  1.0  /* degrees */

/* Default influence radius for interpolation (meters) */
#define DEFAULT_INFLUENCE_RADIUS_M  200000.0  /* 200 km */

/* Fill value for missing data */
#define DEFAULT_FILL_VALUE  1.0e20f

/* Threshold for invalid data detection (close to FLT_MAX ~ 3.4e38) */
#define INVALID_DATA_THRESHOLD  1e37f

/* Maximum variables */
#define MAX_VARS            256
#define MAX_DIMS            10
#define MAX_NAME_LEN        256

/* Coordinate type identification */
typedef enum {
    COORD_TYPE_UNKNOWN = 0,
    COORD_TYPE_1D_STRUCTURED,    /* Regular grid: lon(x), lat(y) */
    COORD_TYPE_2D_CURVILINEAR,   /* Curvilinear: lon(y,x), lat(y,x) */
    COORD_TYPE_1D_UNSTRUCTURED   /* Unstructured: lon(node), lat(node) */
} CoordType;

/* File type */
typedef enum {
    FILE_TYPE_UNKNOWN = 0,
    FILE_TYPE_NETCDF,
#ifdef HAVE_ZARR
    FILE_TYPE_ZARR,
#endif
} FileType;

/* Forward declarations */
typedef struct USVar USVar;
typedef struct USFile USFile;
typedef struct USFileSet USFileSet;
typedef struct USMesh USMesh;
typedef struct USRegrid USRegrid;
typedef struct USView USView;
typedef struct KDTree KDTree;

/* Mesh/coordinate structure - unified coordinate system */
struct USMesh {
    /* Source coordinates (always stored as 1D after flattening) */
    size_t      n_points;           /* Total number of spatial points */
    double     *lon;                /* Longitude array [n_points] */
    double     *lat;                /* Latitude array [n_points] */

    /* Cartesian representation (unit sphere) for KDTree */
    double     *xyz;                /* Cartesian coords [n_points * 3] */

    /* Original grid info (for structured data) */
    CoordType   coord_type;
    size_t      orig_nx, orig_ny;   /* Original grid dimensions if structured */

    /* Element connectivity for polygon rendering (triangular elements) */
    size_t      n_elements;         /* Number of triangular elements */
    int         n_vertices;         /* Vertices per element (3 for triangles) */
    int        *elem_nodes;         /* Node indices [n_elements * n_vertices] */

    /* Mesh file info (for unstructured data with separate mesh) */
    char       *mesh_filename;
    int         mesh_loaded;

    /* Coordinate variable names */
    char       *lon_varname;
    char       *lat_varname;
};

/* KDTree regridding structure */
struct USRegrid {
    /* KDTree handle */
    KDTree     *kdtree;

    /* Target regular grid */
    size_t      target_nx, target_ny;
    double      target_lon_min, target_lon_max;
    double      target_lat_min, target_lat_max;
    double      target_dlon, target_dlat;

    /* Precomputed interpolation indices */
    size_t     *nn_indices;         /* Nearest neighbor index for each target point */
    double     *nn_distances;       /* Distance to nearest neighbor (chord units) */
    unsigned char *valid_mask;      /* 1 if point is valid, 0 otherwise */

    /* Influence radius (chord distance on unit sphere) */
    double      influence_radius_chord;
    double      influence_radius_meters;

    /* Source mesh reference */
    size_t      source_n_points;
};

/* Variable structure */
struct USVar {
    char        name[MAX_NAME_LEN];
    char        long_name[MAX_NAME_LEN];
    char        units[MAX_NAME_LEN];

    /* Dimensionality */
    int         n_dims;
    size_t      dim_sizes[MAX_DIMS];
    char        dim_names[MAX_DIMS][MAX_NAME_LEN];

    /* Dimension roles */
    int         time_dim_id;        /* Index of time dimension, -1 if none */
    int         depth_dim_id;       /* Index of depth dimension, -1 if none */
    int         node_dim_id;        /* Index of node/spatial dimension */

    /* Associated mesh */
    USMesh     *mesh;

    /* Data range */
    float       global_min, global_max;
    float       user_min, user_max;
    float       fill_value;
    int         range_set;

    /* File association */
    USFile     *file;
    int         varid;              /* NetCDF variable ID */

#ifdef HAVE_ZARR
    /* Zarr-specific */
    void       *zarr_data;          /* ZarrArray* for zarr variables */
#endif

    /* Linked list */
    USVar      *next;
};

/* File descriptor */
struct USFile {
    int         id;
    char        filename[MAX_NAME_LEN];
    FileType    file_type;

    /* NetCDF-specific */
    int         ncid;               /* NetCDF file ID */

#ifdef HAVE_ZARR
    /* Zarr-specific */
    void       *zarr_data;          /* ZarrStore* for zarr files */
#endif

    /* Variables in this file */
    USVar      *vars;
    int         n_vars;
};

/* Multi-file set for time concatenation */
struct USFileSet {
    USFile    **files;              /* Array of open files */
    int         n_files;            /* Number of files */
    size_t     *time_offsets;       /* Cumulative time offsets [n_files+1] */
    size_t      total_times;        /* Total virtual time steps */
    char       *base_filename;      /* First filename (for display) */
};

/* Render mode */
typedef enum {
    RENDER_MODE_INTERPOLATE = 0,    /* Default: regrid to regular grid */
    RENDER_MODE_POLYGON             /* Render actual mesh polygons */
} RenderMode;

/* Current view state */
struct USView {
    USVar      *variable;           /* Current variable being displayed */
    USMesh     *mesh;               /* Current mesh/coordinates */
    USRegrid   *regrid;             /* Current regridding setup */
    USFileSet  *fileset;            /* Multi-file set (NULL for single file) */

    /* Render mode */
    RenderMode  render_mode;        /* Interpolate or polygon rendering */

    /* Current position in data space */
    size_t      time_index;         /* Current time step (virtual if fileset) */
    size_t      depth_index;        /* Current depth level */
    size_t      n_times;            /* Total time steps (virtual if fileset) */
    size_t      n_depths;           /* Total depth levels */

    /* Raw data buffer (1D, unstructured representation) */
    float      *raw_data;           /* [n_points] */
    size_t      raw_data_size;

    /* Regridded data buffer */
    float      *regridded_data;     /* [target_ny * target_nx] */

    /* Data grid dimensions (from regrid) */
    size_t      data_nx, data_ny;

    /* Pixel buffer for display (scaled) */
    unsigned char *pixels;          /* [display_ny * display_nx * 3] RGB */
    size_t      display_nx, display_ny;
    int         scale_factor;       /* Display magnification */

    /* Data status */
    int         data_valid;

    /* Animation state */
    int         animating;
    int         frame_delay_ms;
};

/* Global options */
typedef struct {
    int         debug;
    double      influence_radius;   /* Regrid influence radius in meters */
    double      target_resolution;  /* Target grid resolution in degrees */
    char        mesh_file[MAX_NAME_LEN];  /* Separate mesh file path */
    int         frame_delay_ms;     /* Animation speed */
    int         polygon_only;       /* Skip regridding, polygon mode only */
} USOptions;

/* Dimension info for display */
typedef struct {
    char        name[MAX_NAME_LEN];     /* Dimension name (e.g., "time", "depth") */
    char        units[MAX_NAME_LEN];    /* Units string */
    size_t      size;                    /* Dimension size */
    size_t      current;                 /* Current index */
    double      min_val;                 /* Min coordinate value */
    double      max_val;                 /* Max coordinate value */
    double     *values;                  /* All coordinate values (NULL if no coord var) */
    int         is_scannable;            /* Can we navigate this dimension? */
} USDimInfo;

/* Time series data for popup plot */
typedef struct {
    double *times;       /* Time coordinate values [n_points] */
    float  *values;      /* Data values [n_points] */
    int    *valid;       /* 1=valid, 0=fill [n_points] */
    size_t  n_points;    /* Total time steps */
    size_t  n_valid;     /* Valid (non-fill) count */
    char    title[512];  /* "varname (units) at lon, lat" */
    char    x_label[256];/* Time units string or "Time Step" */
    char    y_label[256];/* Variable name + units */
} TSData;

/* Colormap entry */
typedef struct {
    unsigned char r, g, b;
} USColor;

/* Colormap */
typedef struct {
    char        name[64];
    int         n_colors;
    USColor    *colors;
} USColormap;

#endif /* USHOW_DEFINES_H */
