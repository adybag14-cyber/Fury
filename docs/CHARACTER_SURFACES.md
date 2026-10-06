# Original character surface atlas

The character material is an original, deterministic, runtime-generated 1024 ×
1024 PBR atlas. It adds surface response to the modeled face, hair, clothing and
accessories while preserving linear vertex colors as the character's skin tone,
hair color and wardrobe palette. It contains no downloaded textures, captured
likeness, painted illumination, highlights, ambient occlusion, or emission.
The analytic fields and generated pixels follow the repository's MIT license.

## Integration contract

Include `fury/character_surface.hpp`. For each vertex, convert the part's local
coordinates using `character_surface_uv(CharacterSurface::Jacket, u, v)` (or its
appropriate surface). Draw the resulting mesh with
`make_character_surface_material()`.

The factory supplies white albedo, roughness factor **1**, metallic factor **1**,
normal scale **1**, opaque coverage, and UV0 mapping. The atlas's green and blue
channels therefore supply the final roughness and metallic values. Leaving an
old metallic factor of zero would incorrectly turn hardware into a dielectric;
leaving an old roughness factor of 0.55 would make every material too glossy.
Existing per-vertex colors should stay in linear space and must not be replaced
with white. The atlas is a near-neutral reflectance multiplier, not a complete
untinted character color palette.

`character_surface_textures()` returns one shared immutable `MaterialTextures`
instance. Generation occurs once on first use; subsequent calls retain the same
allocation. Every draw can share it, including near and far geometry.

### Stable regions

Coordinates are pixels from the atlas's top-left corner. Tile sizes include a
32-pixel gutter on each edge.

| Surface | Origin X, Y | Tile | Usable content | Approximate roughness |
|---|---:|---:|---:|---:|
| Face | 0, 0 | 512 × 512 | 448 × 448 | 0.46; slightly smoother nose/lips |
| Jacket | 512, 0 | 512 × 512 | 448 × 448 | 0.79 |
| Hair | 0, 512 | 256 × 256 | 192 × 192 | 0.39 |
| Skin | 256, 512 | 256 × 256 | 192 × 192 | 0.46 |
| Shirt | 512, 512 | 256 × 256 | 192 × 192 | 0.86 |
| Pants | 768, 512 | 256 × 256 | 192 × 192 | 0.82 |
| Leather | 0, 768 | 256 × 256 | 192 × 192 | 0.55 |
| Eyes | 256, 768 | 256 × 256 | 192 × 192 | 0.17 |
| Rubber | 512, 768 | 256 × 256 | 192 × 192 | 0.88 |
| Metal | 768, 768 | 256 × 256 | 192 × 192 | 0.29 |

All surfaces are nonmetallic except hardware, which uses metallic 0.94.
`character_surface_region()` exposes these rectangles. Invalid enum values throw
`std::out_of_range`. `character_surface_uv()` clamps finite local coordinates to
the 0–1 interval and maps them to the first/last interior texel centers; nonfinite
coordinates throw `std::invalid_argument`.

Local face U follows the loft's theta: forward is U=0.25 and the back is U=0.75.
Local V runs from chin 0 to crown 1. Eye pigmentation is centered near V=0.56,
cheeks near V=0.42, and lips near V=0.25. Broad geometry, eyelids, lips, iris,
pupil, eyebrows and garment folds remain geometry. The eye atlas does not paint
a pupil or catchlight that could conflict with their actual positions.

Duplicate vertices at a loft's U=0/U=1 seam, so no triangle interpolates across
the atlas or the whole circumference. Parts with different materials also need
their own vertices. Do not assign UVs in a triangle from two atlas regions.
For a cap, averaging the UVs of one same-surface ring is safe: the helper is
affine and the rectangle is convex, so the averaged coordinate remains within
the content rectangle and equals the mapped average local coordinate. Keep cap
rim vertices in that same region. This does not supply a radial cap unwrap;
it only preserves material selection for the existing loft-cap convention.
V is not flipped by the helper; the same stored image and UV convention is used
by the CPU and OpenGL paths. Tangent frames are derived from these authored UVs.

## Map semantics and filtering

- Base-color RGB is generated in **linear reflectance**, encoded as **sRGB8**,
  and sampled/filtered in linear light. Alpha is uniformly one
- Tangent normals encode XYZ into linear RGB8. They are derived from shallow
  physical-height fields, with +U/+V derivatives matching mesh UVs. Mips are
  renormalized, and all texels remain close to the original surface normal
- ORM is linear data: **R=1 (unoccluded), G=roughness, B=metallic**, alpha one.
  Fury's current material shaders use G and B; the reserved R channel makes no
  unsupported claim of texture-based occlusion
- Base color, normals and ORM each have six additional mips: 512, 256, 128, 64,
  32 and 16 pixels. `MaterialTextures` mip element zero is level one

Fine cloth yarns, subtly irregular leather grain, skin microvariation, shallow
curved hair bundles and gentle metal brushing have bounded contrast and slopes.
The maps do not bake directional light or large garment folds. Lighting and the
actual modeled form create their highlights and shadows.

U gutters continue the opposite content edge to make filtered wrapping natural.
V gutters duplicate the nearest edge for chin/crown, cuffs and hems. The 32-pixel
padding keeps every bilinear/trilinear footprint inside its own region through
level six, including UV endpoints and corners. The lowest mip is deliberately
16 × 16; smaller full-atlas levels would combine unrelated materials. Very large
footprints clamp to this last safe level.

**Backend requirement:** honor valid supplied, bounded mip chains. The software
and CPU ray samplers already clamp to the final supplied level. OpenGL must
upload supplied levels and set `GL_TEXTURE_MAX_LEVEL` to their last level;
automatic mip generation should be used only when no valid additional mip was
supplied. Regenerating a full chain down to 1 × 1 would silently defeat atlas
isolation. This requirement matters even when level-zero screenshots look fine.

Three RGBA images and their retained mip levels use **16,776,192 bytes**, just
below 16 MiB, shared across instances. There is no per-frame generation or random
state, and no dependency on image downloads or files on disk.

## Focused verification

`tests/character_surface_tests.cpp` validates dimensions, complete/nonoverlapping
layout, the memory bound, valid UVs, finite near-unit tangent normals, alpha,
physical roughness ranges, dielectric/conductor separation, shared material
ownership and decoded linear skin reflectance. The latter catches incorrect
sRGB encoding in otherwise plausible-looking near-white maps.

The seam checks exercise all retained levels and fractional LODs. An independent
adversarial fixture replaces all neighboring regions with a different ORM value,
regenerates mips, and verifies unchanged production sampler results at region
edges, corners and interior points. Footprint-driven tests also retain the
rubber/metal distinction at extreme minification. These are actual shared
material-sampler checks, not image-content checksum assertions.

A standalone test build needs no SDL or graphics context:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Iengine/include -Iengine/third_party/stb \
  tests/character_surface_tests.cpp engine/src/character_surface.cpp \
  engine/src/material_sampling.cpp engine/src/texture.cpp \
  engine/src/math.cpp engine/src/log.cpp \
  -o /tmp/fury_character_surface_tests
/tmp/fury_character_surface_tests
```

Use `--dump /tmp/fury-character-atlas` to export the production base-color, normal
and ORM pixels as PPM files.

### OpenGL pixel regression

The engine-linked test also accepts `--gl`. When SDL headers are unavailable,
the standalone sampler build keeps working and `--gl` returns skip code 77.
The renderer test requires an actual OpenGL 3.3 context and never substitutes a
CPU backend. A missing display/context also returns 77. For a Linux environment
with SDL's offscreen driver and Mesa llvmpipe:

```sh
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  MESA_SHADER_CACHE_DIR=/tmp/fury-character-gl-cache \
  ./build/tests/fury_character_surface_tests --gl
```

Quarter-pixel geometry covering a known pixel center requests LOD above nine.
Changing all neighboring atlas regions to hostile color and ORM values leaves
the actual rendered pixel unchanged, within one byte. The test also queries the
uploaded color, normal and ORM textures to confirm their maximum mip is six.
As a sensitivity control, a deliberately unsafe full mip chain must visibly
contaminate that same pixel. This control catches an upload path that silently
regenerates the atlas down to 1 × 1.

The offscreen llvmpipe run produced protected/unsafe green-channel values of
185/121 for face, 183/82 for hair, 185/84 for skin, 184/121 for jacket, 186/84 for
eyes, 184/83 for rubber and 185/84 for hardware. Separate actual lighting checks
produced eye/skin/jacket highlights of 54/47/35 and a hardware response of 142,
versus 152 when the metallic factor was incorrectly disabled. These measured
values substantiate the filtering and channel tests; different drivers may
round by a byte. The tests check the physical/channel relationship and tolerances,
not hardcoded image checksums.

These sampler and OpenGL pixel checks do not replace whole posed-character
framebuffer review, where silhouette, cap unwrap quality and UV placement can
still affect the result.
