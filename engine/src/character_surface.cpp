#include "fury/character_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fury {
namespace {
constexpr float tau = 6.2831853071795864769f;
constexpr std::array<CharacterSurfaceRegion, 10> regions{{
  {0,0,512}, {512,0,512},
  {0,512,256}, {256,512,256}, {512,512,256}, {768,512,256},
  {0,768,256}, {256,768,256}, {512,768,256}, {768,768,256}
}};
static_assert(regions.size() == static_cast<unsigned>(CharacterSurface::Count),
              "Every character surface needs a stable atlas rectangle");

float clamp01(float value) { return std::clamp(value,0.f,1.f); }
float lerp(float a,float b,float t) { return a+(b-a)*t; }
float smooth(float x) { return x*x*x*(x*(x*6.f-15.f)+10.f); }
int wrap(int x,int period) { return (x%period+period)%period; }
std::uint32_t hash(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
  x *= 0x846ca68bu; return x^(x >> 16);
}
float lattice(int x,int y,int nx,int ny,std::uint32_t seed) {
  const auto key=std::uint32_t(wrap(x,nx))*0x9e3779b9u ^
                 std::uint32_t(wrap(y,ny))*0x85ebca6bu ^ seed;
  return float(hash(key)&0xffffu)/65535.f;
}
// Periodic smooth fields contribute low-amplitude material irregularity, never
// a high-contrast noise overlay or a directional illumination term.
float noise(float u,float v,int nx,int ny,std::uint32_t seed) {
  const float x=u*float(nx),y=v*float(ny);
  const int ix=int(std::floor(x)),iy=int(std::floor(y));
  const float a=smooth(x-float(ix)),b=smooth(y-float(iy));
  return lerp(lerp(lattice(ix,iy,nx,ny,seed),lattice(ix+1,iy,nx,ny,seed),a),
              lerp(lattice(ix,iy+1,nx,ny,seed),lattice(ix+1,iy+1,nx,ny,seed),a),b)-.5f;
}
float bell(float u,float v,float cu,float cv,float ru,float rv) {
  float dx=u-cu; dx-=std::round(dx);
  const float x=dx/ru,y=(v-cv)/rv;
  return std::exp(-.5f*(x*x+y*y));
}
struct SurfaceSample {
  Vec3 linear_color{.96f,.96f,.96f};
  float height{0.f}; // metres, used only to derive restrained tangent normals
  float roughness{.7f};
  float metallic{0.f};
};
SurfaceSample skin(float u,float v,bool face) {
  const float broad=noise(u,v,7,9,0x147bu);
  const float fine=noise(u,v,61,67,0x7539u);
  SurfaceSample s;
  const float value=.971f+.016f*broad+.006f*fine;
  s.linear_color={value,value-.006f,value-.009f};
  s.height=.000007f*fine+.000002f*noise(u,v,113,109,0x312bu);
  s.roughness=.458f+.033f*broad+.026f*fine;
  if(face) {
    const float cheeks=bell(u,v,.17f,.42f,.043f,.082f)+bell(u,v,.33f,.42f,.043f,.082f);
    const float lips=bell(u,v,.25f,.25f,.035f,.021f);
    const float nose=bell(u,v,.25f,.46f,.020f,.065f);
    const float sockets=bell(u,v,.182f,.56f,.034f,.028f)+bell(u,v,.318f,.56f,.034f,.028f);
    // Pigment changes, not painted shadows. Sculpted lips/eyelids remain geometry.
    s.linear_color.x*=1.f-.055f*lips-.009f*sockets;
    s.linear_color.y*=1.f-.037f*cheeks-.15f*lips-.018f*nose-.012f*sockets;
    s.linear_color.z*=1.f-.024f*cheeks-.12f*lips-.012f*nose-.012f*sockets;
    s.roughness-=.048f*nose+.024f*bell(u,v,.25f,.75f,.065f,.08f)+.029f*lips;
  }
  return s;
}
SurfaceSample cloth(float u,float v,CharacterSurface kind) {
  const bool jacket=kind==CharacterSurface::Jacket;
  const bool shirt=kind==CharacterSurface::Shirt;
  const float count=jacket?86.f:(shirt?41.f:45.f);
  const float irregular=noise(u,v,13,17,jacket?0xa92bu:0xf31du);
  const float warp=std::sin(tau*(u*count+.14f*std::sin(tau*v*5.f)));
  const float weft=std::sin(tau*(v*(count+5.f)+.11f*std::sin(tau*u*4.f)));
  const float twill=std::sin(tau*(u*(jacket?51.f:31.f)+v*(jacket?47.f:29.f)));
  const float fiber=noise(u,v,jacket?173:89,jacket?149:83,0x286fu);
  const float yarn=.55f*warp+.35f*weft+.10f*twill;
  const float value=.943f+.027f*irregular+.010f*yarn+.005f*fiber;
  SurfaceSample s;
  s.linear_color={value,value,value};
  s.height=(jacket?.000075f:.000055f)*yarn+.000016f*fiber;
  s.roughness=(jacket?.79f:(shirt?.86f:.82f))+.040f*irregular+.015f*yarn;
  return s;
}
SurfaceSample sample(CharacterSurface surface,float u,float v) {
  switch(surface) {
    case CharacterSurface::Face: return skin(u,v,true);
    case CharacterSurface::Skin: return skin(u,v,false);
    case CharacterSurface::Jacket:
    case CharacterSurface::Shirt:
    case CharacterSurface::Pants: return cloth(u,v,surface);
    case CharacterSurface::Hair: {
      // Shallow, gently curved strand bundles. Highlights come from the renderer.
      const float bend=.18f*std::sin(tau*v)+.07f*std::sin(tau*(3.f*v+u));
      const float strand=std::sin(tau*(29.f*u+bend));
      const float fine=std::sin(tau*(67.f*u+.16f*std::sin(tau*v*2.f)));
      const float grain=noise(u,v,37,7,0x1be3u);
      const float value=.924f+.037f*strand+.009f*fine+.020f*grain;
      return {{value,value,value},.000065f*strand+.000012f*fine,
              .39f+.038f*grain+.013f*strand,0.f};
    }
    case CharacterSurface::Leather: {
      const float grain=noise(u,v,43,47,0xcb17u);
      const float broad=noise(u,v,11,13,0xa045u);
      const float crease=std::pow(std::max(0.f,-grain-.12f),2.f);
      const float value=.946f+.023f*broad+.022f*grain-.030f*crease;
      return {{value,value,value},.000090f*grain-.000060f*crease,
              .552f+.055f*grain+.018f*broad,0.f};
    }
    case CharacterSurface::Eyes: {
      // Geometry/vertex tint distinguish sclera, iris and pupil. No painted
      // catchlight or built-in pupil that could fight their authored placement.
      const float edge=clamp01((std::hypot(u-.5f,v-.5f)-.24f)*1.6f);
      return {{.995f,.991f-.015f*edge,.983f-.011f*edge},0.f,.165f+.026f*edge,0.f};
    }
    case CharacterSurface::Rubber: {
      const float grain=noise(u,v,53,59,0xd857u);
      const float value=.953f+.021f*grain;
      return {{value,value,value},.000021f*grain,.88f+.042f*grain,0.f};
    }
    case CharacterSurface::Metal: {
      const float grain=noise(u,v,5,67,0x1357u);
      const float brushing=std::sin(tau*(61.f*v+.05f*std::sin(tau*u*2.f)));
      const float value=.976f+.009f*grain+.004f*brushing;
      return {{value,value,value},.0000009f*brushing,.29f+.045f*grain+.008f*brushing,.94f};
    }
    default: throw std::out_of_range("Invalid character surface");
  }
}
Vec2 physical_span(CharacterSurface surface) {
  switch(surface) {
    case CharacterSurface::Face: return {.38f,.24f};
    case CharacterSurface::Jacket: return {.75f,.70f};
    case CharacterSurface::Hair: return {.36f,.22f};
    case CharacterSurface::Skin: return {.18f,.27f};
    case CharacterSurface::Shirt: return {.70f,.60f};
    case CharacterSurface::Pants: return {.30f,.75f};
    case CharacterSurface::Leather: return {.22f,.28f};
    case CharacterSurface::Eyes: return {.026f,.016f};
    case CharacterSurface::Rubber: return {.16f,.30f};
    case CharacterSurface::Metal: return {.045f,.045f};
    default: throw std::out_of_range("Invalid character surface");
  }
}
std::uint8_t byte(float value) { return std::uint8_t(clamp01(value)*255.f+.5f); }
float srgb(float linear) {
  linear=clamp01(linear);
  return linear<=.0031308f?linear*12.92f:1.055f*std::pow(linear,1.f/2.4f)-.055f;
}
RgbaImage allocate() {
  constexpr auto size=std::size_t(character_surface_atlas_size);
  return {character_surface_atlas_size,character_surface_atlas_size,
          std::vector<std::uint8_t>(size*size*4,255)};
}
std::size_t offset(int x,int y) {
  return (std::size_t(y)*character_surface_atlas_size+std::size_t(x))*4;
}
void extrude(RgbaImage& image,const CharacterSurfaceRegion& region) {
  const int lo=region.gutter,hi=region.size-region.gutter-1;
  for(int y=0;y<region.size;++y) for(int x=0;x<region.size;++x) {
    if(x>=lo && x<=hi && y>=lo && y<=hi) continue;
    // U repeats around lofts, so its gutter continues the opposite edge instead
    // of duplicating a constant stripe. V ends (chin/crown, cuffs/hems) clamp.
    // Preserve the explicit duplicate U=1 endpoint when only V needs padding.
    const int sx=(x>=lo && x<=hi)?x:lo+wrap(x-lo,hi-lo);
    const auto source=offset(region.x+sx,region.y+std::clamp(y,lo,hi));
    const auto target=offset(region.x+x,region.y+y);
    std::copy_n(image.pixels.data()+source,4,image.pixels.data()+target);
  }
}
void make_region(MaterialTextures& textures,CharacterSurface surface) {
  const auto& region=character_surface_region(surface);
  const int n=region.size-2*region.gutter;
  std::vector<float> heights(std::size_t(n)*n);
  for(int y=0;y<n;++y) for(int x=0;x<n;++x) {
    // Duplicate periodic U endpoints exactly, including their quantized normal.
    // Face, skin, cloth and hair fields all repeat at U=1 without a tint jump.
    const float u=x==n-1?0.f:float(x)/float(n-1),v=float(y)/float(n-1);
    const auto s=sample(surface,u,v);
    heights[std::size_t(y)*n+x]=s.height;
    const auto out=offset(region.x+region.gutter+x,region.y+region.gutter+y);
    auto* color=textures.base_color.pixels.data()+out;
    color[0]=byte(srgb(s.linear_color.x));
    color[1]=byte(srgb(s.linear_color.y));
    color[2]=byte(srgb(s.linear_color.z));
    auto* orm=textures.metallic_roughness.pixels.data()+out;
    orm[0]=255; orm[1]=byte(s.roughness); orm[2]=byte(s.metallic);
  }
  const Vec2 span=physical_span(surface);
  auto h=[&](int x,int y) {
    return heights[std::size_t(std::clamp(y,0,n-1))*n+wrap(x,n-1)];
  };
  for(int y=0;y<n;++y) for(int x=0;x<n;++x) {
    const float dx=(h(x+1,y)-h(x-1,y))*float(n-1)/(2.f*span.x);
    // One-sided derivative at the local V ends, centered in the interior.
    const float divisor=(y==0 || y==n-1)?span.y:2.f*span.y;
    const float dy=(h(x,y+1)-h(x,y-1))*float(n-1)/divisor;
    const float inv=1.f/std::sqrt(dx*dx+dy*dy+1.f);
    auto* normal=textures.normal.pixels.data()+
      offset(region.x+region.gutter+x,region.y+region.gutter+y);
    normal[0]=byte(.5f-.5f*dx*inv);
    normal[1]=byte(.5f-.5f*dy*inv);
    normal[2]=byte(.5f+.5f*inv);
  }
  extrude(textures.base_color,region);
  extrude(textures.normal,region);
  extrude(textures.metallic_roughness,region);
}
void finish(RgbaImage& image,std::vector<RgbaImage>& mips,TextureEncoding encoding) {
  auto levels=build_mip_chain(std::move(image),encoding);
  image=std::move(levels.front());
  mips.reserve(character_surface_max_mip);
  for(int level=1;level<=character_surface_max_mip;++level)
    mips.push_back(std::move(levels[std::size_t(level)]));
}
std::shared_ptr<const MaterialTextures> generate() {
  auto textures=std::make_shared<MaterialTextures>();
  textures->source="fury:original-character-surfaces/v1;atlas=1024;gutter=32;max-lod=6;license=MIT";
  textures->base_color=allocate();
  textures->normal=allocate();
  textures->metallic_roughness=allocate();
  for(unsigned i=0;i<regions.size();++i) make_region(*textures,CharacterSurface(i));
  finish(textures->base_color,textures->base_color_mips,TextureEncoding::SRGB);
  finish(textures->normal,textures->normal_mips,TextureEncoding::Normal);
  finish(textures->metallic_roughness,textures->metallic_roughness_mips,TextureEncoding::Linear);
  return textures;
}
}  // namespace

const CharacterSurfaceRegion& character_surface_region(CharacterSurface surface) {
  const auto index=static_cast<unsigned>(surface);
  if(index>=regions.size()) throw std::out_of_range("Invalid character surface");
  return regions[index];
}
Vec2 character_surface_uv(CharacterSurface surface,float u,float v) {
  const auto& region=character_surface_region(surface);
  if(!std::isfinite(u) || !std::isfinite(v))
    throw std::invalid_argument("Character surface UVs must be finite");
  const float span=float(region.size-2*region.gutter-1);
  const float x=float(region.x+region.gutter)+.5f+clamp01(u)*span;
  const float y=float(region.y+region.gutter)+.5f+clamp01(v)*span;
  return {x/character_surface_atlas_size,y/character_surface_atlas_size};
}
std::shared_ptr<const MaterialTextures> character_surface_textures() {
  static const auto textures=generate();
  return textures;
}
Material make_character_surface_material() {
  Material material;
  material.albedo={1,1,1}; material.metallic=1.f; material.roughness=1.f;
  material.normal_scale=1.f; material.world_uv_scale=0.f;
  material.textures=character_surface_textures();
  return material;
}

}  // namespace fury
