"""Spatial preparation for private scenery. No game files required to test it."""
import math
import numpy as np

CELL = 128
WORLD_DTYPE = np.dtype([('p', '<f4', (3, 3)), ('c', '<u4')])
UV_DTYPE = np.dtype([('uv', '<f4', (3, 2)), ('material', '<u4')])


def clip_plane(poly, axis, edge, greater):
    """Clip XYZ/UV together; force the shared coordinate to the exact grid edge."""
    out = []
    for p, q in zip(poly, poly[1:] + poly[:1]):
        ip = p[axis] >= edge if greater else p[axis] <= edge
        iq = q[axis] >= edge if greater else q[axis] <= edge
        if ip:
            out.append(p)
        if ip != iq:
            t = (edge - p[axis]) / (q[axis] - p[axis])
            v = [p[k] + t * (q[k] - p[k]) for k in range(5)]
            v[axis] = float(edge)
            out.append(v)
    return out


def split_triangle(points, uv):
    """Disjoint tile fragments, preserving winding, height and repeated UVs."""
    lo = [math.floor(min(p[k] for p in points) / CELL) for k in range(2)]
    hi = [math.floor(max(p[k] for p in points) / CELL) for k in range(2)]
    if (hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) > 16384:
        raise ValueError('oversized scenery triangle')
    source = [list(p) + list(t) for p, t in zip(points, uv)]
    for x in range(lo[0], hi[0] + 1):
        for y in range(lo[1], hi[1] + 1):
            poly = source
            for axis, edge, greater in ((0,x*CELL,True),(0,(x+1)*CELL,False),(1,y*CELL,True),(1,(y+1)*CELL,False)):
                poly = clip_plane(poly, axis, edge, greater)
                if len(poly) < 3:
                    break
            for i in range(1, len(poly) - 1):
                tri = np.asarray((poly[0], poly[i], poly[i+1]), dtype=np.float64)
                normal = np.cross(tri[1,:3]-tri[0,:3], tri[2,:3]-tri[0,:3])
                if np.dot(normal, normal) <= 1e-16:
                    continue
                # Vertical faces exactly on a grid plane belong to one side only.
                owner = np.floor(tri[:,:2].mean(axis=0) / CELL).astype(int)
                if owner[0] == x and owner[1] == y:
                    yield (x, y), tri[:,:3], tri[:,3:]


def append_spatial(tiles, records, uvrecords, textured):
    """Fast bulk path for interior faces; only boundary faces need clipping."""
    points = records['p']
    low = np.floor(points.min(axis=1)[:,:2] / CELL).astype(np.int32)
    high = np.floor(points.max(axis=1)[:,:2] / CELL).astype(np.int32)
    interior = np.all(low == high, axis=1)
    for cell in np.unique(low[interior], axis=0):
        mask = interior & np.all(low == cell, axis=1)
        tile = tiles.setdefault(tuple(int(v) for v in cell), [bytearray(), bytearray(), bytearray()])
        tile[0].extend(records[mask].tobytes())
        if textured:
            tile[2].extend(uvrecords[mask].tobytes())
    fragments = int(np.count_nonzero(interior))
    for i in np.flatnonzero(~interior):
        for cell, p, uv in split_triangle(points[i].tolist(), uvrecords['uv'][i].tolist()):
            tile = tiles.setdefault(cell, [bytearray(), bytearray(), bytearray()])
            row = np.empty(1, dtype=WORLD_DTYPE); row['p'][0] = p; row['c'][0] = records['c'][i]
            tile[0].extend(row.tobytes())
            if textured:
                u = np.empty(1, dtype=UV_DTYPE); u['uv'][0] = uv; u['material'][0] = uvrecords['material'][i]
                tile[2].extend(u.tobytes())
            fragments += 1
    return int(np.count_nonzero(~interior)), fragments


def spatial_order(records):
    """Morton XYZ order keeps consecutive 64-face runtime clusters compact."""
    if not len(records):
        return np.empty(0, dtype=np.int64)
    centers = records['p'].mean(axis=1)
    low = centers.min(axis=0)
    # One metric for all axes preserves physical proximity in tall sectors.
    scale = max(float(np.ptp(centers, axis=0).max()), 1.0)
    q = np.clip((centers-low) * (1023.0/scale), 0, 1023).astype(np.uint32)
    code = np.zeros(len(records), dtype=np.uint32)
    for bit in range(10):
        for axis in range(3):
            code |= ((q[:,axis] >> bit) & 1) << (3*bit+axis)
    return np.argsort(code, kind='stable')
