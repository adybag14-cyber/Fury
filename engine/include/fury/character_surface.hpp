#pragma once

#include "fury/texture.hpp"

#include <cstdint>
#include <memory>

namespace fury {

/// Stable regions of the original, vertex-tinted character material atlas.
enum class CharacterSurface : std::uint8_t {
  Face, Jacket, Hair, Skin, Shirt, Pants, Leather, Eyes, Rubber, Metal, Count
};

constexpr int character_surface_atlas_size = 1024;
constexpr int character_surface_gutter = 32;
/// The 16x16 final atlas level preserves isolation between the smallest regions.
/// Do not append smaller mips: they cannot represent these separate materials.
constexpr int character_surface_max_mip = 6;

struct CharacterSurfaceRegion {
  int x, y, size;
  int gutter{character_surface_gutter};
};

/// Pixel rectangles, including gutters. Throws std::out_of_range for Count or
/// an invalid enum. Image row/UV V zero is the atlas top; local V is not flipped.
const CharacterSurfaceRegion& character_surface_region(CharacterSurface surface);
/// Clamp finite local UVs to [0,1], then map to the interior texel centers.
/// Throws std::invalid_argument for nonfinite UVs. Wrapped geometry must have
/// separate U=0 and U=1 vertices, never a triangle crossing atlas rectangles.
/// Face local U=.25 is forward; V=0 is chin, V=1 is crown.
Vec2 character_surface_uv(CharacterSurface surface, float u, float v);

/// One immutable process-wide set: sRGB base-color multipliers, tangent normals,
/// and linear ORM (R=1, G=roughness, B=metallic), all with six additional mips.
/// Base color preserves the mesh's linear vertex tint; it is not an untinted
/// photographic skin/clothing palette. No baked lighting, emission, or alpha.
std::shared_ptr<const MaterialTextures> character_surface_textures();
/// Complete atlas material: white albedo, roughness/metallic factors both one,
/// normal scale one, UV0 mapping, opaque. The atlas makes only hardware metallic.
Material make_character_surface_material();

}  // namespace fury
