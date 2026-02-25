# MITgcm Velocity Rotation on LLC/Cubed-Sphere Grids

## The Problem

On MITgcm LLC (Lat-Lon-Cap) and cubed-sphere grids, UVEL and VVEL are stored in **local grid coordinates**, not geographic (eastward/northward). Each face of the cubed sphere has its own local coordinate system, so the i-direction and j-direction point in different geographic directions on different faces.

When visualized on a lon-lat map without rotation, this causes visible discontinuities at face boundaries — velocities appear to jump abruptly where one face meets another, because the local axes change orientation.

Tracer fields (THETA, SALT, ETAN, etc.) are scalar values and are unaffected by grid orientation — they display correctly as-is.

### Grid staggering (secondary issue)

UVEL lives on the XG/YC grid (western cell face), VVEL lives on the XC/YG grid (southern cell face), while tracers live on XC/YC (cell centers). This creates a small spatial offset but is NOT the primary cause of the visible discontinuities. The dominant effect is the face rotation.

## The Solution: CS/SN Rotation

MITgcm can output two grid files:
- `AngleCS.data` / `CS.data` — cosine of the angle between grid-i and geographic-east
- `AngleSN.data` / `SN.data` — sine of the angle between grid-i and geographic-east

With these, geographic velocities are computed as:

```
u_east  = CS * UVEL - SN * VVEL
v_north = SN * UVEL + CS * VVEL
```

This transforms the local (i,j) velocity components to proper (east, north) components.

## Implementation Plan

### 1. Load CS/SN during `mitgcm_open()`

In `MitgcmStore`, add:
```c
float *angle_cs;   /* AngleCS or CS [ny*nx], NULL if unavailable */
float *angle_sn;   /* AngleSN or SN [ny*nx], NULL if unavailable */
```

During `mitgcm_open()`, try loading in order:
1. `AngleCS.data` / `AngleSN.data`
2. `CS.data` / `SN.data`

Both must be present; if either is missing, set both to NULL (no rotation).

### 2. Mark velocity variables

In `MitgcmVarData`, add:
```c
int velocity_component;  /* 0=none, 1=U-component, 2=V-component */
```

During `mitgcm_scan_variables()`, detect velocity fields by name:
- U-component: names starting with "U" or containing "UVEL"
- V-component: names starting with "V" or containing "VVEL"

This heuristic covers UVEL, VVEL, and diagnostic velocity fields.

### 3. Pair U/V variables

When scanning variables, if both a U-component and V-component exist in the same diagnostic group (same prefix, same dimensions), link them:
```c
USVar *paired_velocity;  /* Pointer to the other component */
```

### 4. Apply rotation during `mitgcm_read_slice()`

After reading the raw slab, if `angle_cs` is available and the variable is a velocity component:

```c
if (store->angle_cs && vd->velocity_component != 0 && vd->paired_velocity) {
    /* Need to read the paired component too */
    float *other = malloc(slab_size * sizeof(float));
    /* read paired component at same time/depth */

    if (vd->velocity_component == 1) {
        /* This is U, other is V */
        for (i = 0; i < slab_size; i++) {
            float u = data[i], v = other[i];
            data[i] = store->angle_cs[i] * u - store->angle_sn[i] * v;
        }
    } else {
        /* This is V, other is U */
        for (i = 0; i < slab_size; i++) {
            float u = other[i], v = data[i];
            data[i] = store->angle_sn[i] * u + store->angle_cs[i] * v;
        }
    }
    free(other);
}
```

### 5. Handle missing CS/SN gracefully

If CS/SN files are not present, velocities display in raw local coordinates (current behavior). No error, no warning — the data is still valid, just not geographically oriented.

Optionally print an info message:
```
MITgcm: AngleCS/AngleSN not found, velocity fields shown in local grid coordinates
```

### Considerations

- **Performance**: rotation requires reading two fields per slice instead of one. For interactive use this should be fine (binary reads are fast).
- **Timeseries**: the `mitgcm_read_timeseries()` function would also need to read both components at each timestep for rotated output. This doubles the I/O for velocity time series.
- **Grid staggering**: for full correctness, UVEL should be interpolated from XG to XC before rotation. This is a C-grid averaging: `u_at_center[i] = 0.5 * (uvel[i] + uvel[i-1])` in the i-direction (with wrapping within each face). This is a secondary refinement.
- **Variable naming**: rotated variables could be renamed to indicate geographic orientation (e.g., "UVEL_east", "VVEL_north"), or the original names kept with a visual indicator.
