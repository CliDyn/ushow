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
    FILE_TYPE_NETCDF
} FileType;

/* Forward declarations */
typedef struct USVar USVar;
typedef struct USFile USFile;
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

    /* Variables in this file */
    USVar      *vars;
    int         n_vars;
};

/* Current view state */
struct USView {
    USVar      *variable;           /* Current variable being displayed */
    USMesh     *mesh;               /* Current mesh/coordinates */
    USRegrid   *regrid;             /* Current regridding setup */

    /* Current position in data space */
    size_t      time_index;         /* Current time step */
    size_t      depth_index;        /* Current depth level */
    size_t      n_times;            /* Total time steps */
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
} USOptions;

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
