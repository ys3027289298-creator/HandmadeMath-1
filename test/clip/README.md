# Polygon clipping tests

Tests for the Sutherland-Hodgman batch polygon clipping API added to
`HandmadeMath.h` (`HMM_ClipPolygon`, `HMM_ClipPolygonOne`, and the
`HMM_Polygon` / `HMM_ClipResult` types).

Run everything (C99, C11, C++98, C++17, plus an ASan/UBSan build) with:

```
./run_clip_test.sh [timing_iterations]
```

The script builds only inside `test/clip/build/`; it does not touch the
repository Makefile or any public interface. It exits non-zero if any check
fails.

Coverage:

1. convex subject fully inside a region (identity output),
2. concave (L-shaped) subject,
3. subject spanning three chained regions, compared against an independently
   written per-region reference implementation,
4. subject fully outside a region (empty + degenerate/empty flags),
5. degenerate inputs: self-intersection, repeated points, all-coincident,
6. the `PreserveEdges` option (zero-length edges kept vs. removed + flagged),
7. `HMM_ClipPolygonOne` vs. `HMM_ClipPolygon`, including clockwise regions,
8. batch timing over the multi-region fixture.

Each geometric fixture prints its vertex count, flags, vertex sequence and
absolute/relative area error.
