#include "fury/character_surface.hpp"
#include "fury/material_sampling.hpp"

#if __has_include(<SDL.h>) && !defined(FURY_CHARACTER_SURFACE_NO_GL_TESTS)
#define FURY_CHARACTER_SURFACE_GL_TESTS 1
#include "fury/gl_loader.hpp"
#include "fury/renderer.hpp"
#include <SDL.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace fury;
constexpr std::array<CharacterSurface,10> surfaces{{
  CharacterSurface::Face,CharacterSurface::Jacket,CharacterSurface::Hair,
  CharacterSurface::Skin,CharacterSurface::Shirt,CharacterSurface::Pants,
  CharacterSurface::Leather,CharacterSurface::Eyes,CharacterSurface::Rubber,
  CharacterSurface::Metal
}};
constexpr std::array<const char*,10> names{{
  "face","jacket","hair","skin","shirt","pants","leather","eyes","rubber","metal"
}};
void require(bool condition,const char* message) {
  if(!condition) throw std::runtime_error(message);
}
bool close(float a,float b,float epsilon=1e-6f) { return std::abs(a-b)<=epsilon; }
bool close(Vec4 a,Vec4 b,float epsilon=1e-6f) {
  return close(a.x,b.x,epsilon) && close(a.y,b.y,epsilon) &&
         close(a.z,b.z,epsilon) && close(a.w,b.w,epsilon);
}
Vec4 sample(const RgbaImage& image,const std::vector<RgbaImage>& mips,
            CharacterSurface surface,float u,float v,bool srgb,float lod=0) {
  return sample_material_texture_lod(image,mips,character_surface_uv(surface,u,v),srgb,lod);
}
void layout_and_api() {
  std::vector<unsigned> coverage(1024*1024,0);
  for(unsigned i=0;i<surfaces.size();++i) {
    const auto& r=character_surface_region(surfaces[i]);
    require(r.size==(i<2?512:256) && r.gutter==32,"Face/jacket have 512px tiles; remaining surfaces have 256px tiles and 32px gutters");
    require(r.x>=0 && r.y>=0 && r.x+r.size<=1024 && r.y+r.size<=1024,"Every stable region lies inside the 1024px atlas");
    for(int y=r.y;y<r.y+r.size;++y) for(int x=r.x;x<r.x+r.size;++x)
      ++coverage[std::size_t(y)*1024+x];
    const auto a=character_surface_uv(surfaces[i],0,0),b=character_surface_uv(surfaces[i],1,1);
    require(a.x>float(r.x+32)/1024 && a.y>float(r.y+32)/1024 &&
            b.x<float(r.x+r.size-32)/1024 && b.y<float(r.y+r.size-32)/1024,
            "Local UV endpoints remain strictly inside the content's edge texels");
    const auto outside=character_surface_uv(surfaces[i],-40,900);
    require(close(outside.x,a.x) && close(outside.y,b.y),"Finite out-of-range UVs clamp inside the selected material");
    const auto middle=character_surface_uv(surfaces[i],.5f,.5f);
    require(close(middle.x,(a.x+b.x)*.5f) && close(middle.y,(a.y+b.y)*.5f),
            "UV helper is affine within a surface so interpolation and tangent frames stay valid");
    Vec2 cap{};
    for(int vertex=0;vertex<=24;++vertex) {
      const auto uv=character_surface_uv(surfaces[i],float(vertex)/24.f,.73f);
      cap.x+=uv.x/25.f; cap.y+=uv.y/25.f;
    }
    require(cap.x>=a.x && cap.x<=b.x && cap.y>=a.y && cap.y<=b.y &&
            close(cap.x,middle.x) && close(cap.y,character_surface_uv(surfaces[i],.5f,.73f).y),
            "A loft cap's averaged ring UV stays in the same region and matches the local-coordinate average");
  }
  require(std::all_of(coverage.begin(),coverage.end(),[](unsigned n) { return n==1; }),
          "Regions cover the atlas exactly once with no overlaps or undefined texels");
  bool bad_enum=false,bad_uv=false;
  try { character_surface_region(CharacterSurface::Count); } catch(const std::out_of_range&) { bad_enum=true; }
  try { character_surface_uv(CharacterSurface::Face,std::numeric_limits<float>::infinity(),0); }
  catch(const std::invalid_argument&) { bad_uv=true; }
  require(bad_enum && bad_uv,"Invalid regions and nonfinite UVs fail explicitly");
}
std::size_t validate_map(const RgbaImage& image,const std::vector<RgbaImage>& mips,bool normals) {
  require(image.valid() && image.width==1024 && image.height==1024,"Every character PBR map is a complete 1024px RGBA image");
  require(mips.size()==6,"Mip chain is bounded at the last region-safe 16px level");
  std::size_t bytes=0;
  for(unsigned level=0;level<=mips.size();++level) {
    const auto& map=level?mips[level-1]:image;
    require(map.valid() && map.width==(1024>>level) && map.height==(1024>>level),
            "Additional mip zero is half resolution; every supplied level has valid dimensions");
    bytes+=map.pixels.size();
    for(std::size_t p=0;p<map.pixels.size();p+=4) {
      require(map.pixels[p+3]==255,"Every mip remains opaque, including gutters");
      if(normals) {
        const Vec3 n{map.pixels[p]/127.5f-1.f,map.pixels[p+1]/127.5f-1.f,map.pixels[p+2]/127.5f-1.f};
        require(std::isfinite(length(n)) && std::abs(length(n)-1.f)<.008f && n.z>.985f,
                "Normal map texels remain normalized, outward and physically restrained through minification");
      }
    }
  }
  return bytes;
}
void channels_and_physical_response(const MaterialTextures& t) {
  std::size_t bytes=validate_map(t.base_color,t.base_color_mips,false);
  bytes+=validate_map(t.normal,t.normal_mips,true);
  bytes+=validate_map(t.metallic_roughness,t.metallic_roughness_mips,false);
  require(bytes<=16u*1024u*1024u,"Shared atlas plus retained mip chains stays below 16MiB");
  require(!t.emissive.valid() && t.emissive_mips.empty(),"Character surfaces never bake emission into their maps");
  constexpr std::array<float,10> rough_min{{.37f,.74f,.35f,.42f,.81f,.77f,.50f,.15f,.84f,.25f}};
  constexpr std::array<float,10> rough_max{{.51f,.84f,.44f,.50f,.91f,.87f,.61f,.20f,.92f,.33f}};
  for(unsigned i=0;i<surfaces.size();++i) {
    double rough_sum=0,color_sum=0,normal_energy=0;
    float min_rough=1,max_rough=0;
    for(int y=0;y<=48;++y) for(int x=0;x<=48;++x) {
      const float u=float(x)/48.f,v=float(y)/48.f;
      const auto orm=sample(t.metallic_roughness,t.metallic_roughness_mips,surfaces[i],u,v,false);
      require(close(orm.x,1) && close(orm.w,1),"ORM reserves fully unoccluded R and opaque alpha; no directional AO is painted");
      require(orm.y>=rough_min[i] && orm.y<=rough_max[i],"Each material samples within its documented physical roughness range");
      if(surfaces[i]==CharacterSurface::Metal)
        require(orm.z>.93f && orm.z<.95f,"Hardware uses metallic B, independently of its roughness G");
      else require(close(orm.z,0),"Skin, cloth, hair, leather, eyes and rubber remain dielectric");
      const auto color=sample(t.base_color,t.base_color_mips,surfaces[i],u,v,true);
      require(color.x>.80f && color.y>.79f && color.z>.80f && color.x<=1 && color.y<=1 && color.z<=1,
              "Linear base-color multipliers remain bounded and preserve authored vertex tint");
      const auto n=sample(t.normal,t.normal_mips,surfaces[i],u,v,false);
      normal_energy+=std::abs(n.x*2-1)+std::abs(n.y*2-1);
      rough_sum+=orm.y; color_sum+=(color.x+color.y+color.z)/3;
      min_rough=std::min(min_rough,orm.y); max_rough=std::max(max_rough,orm.y);
    }
    constexpr double count=49*49;
    if(surfaces[i]==CharacterSurface::Skin)
      require(color_sum/count>.95 && color_sum/count<.99,
              "Skin's sampled linear reflectance catches accidental double sRGB decoding or missing encoding");
    if(surfaces[i]==CharacterSurface::Jacket || surfaces[i]==CharacterSurface::Leather || surfaces[i]==CharacterSurface::Hair)
      require(normal_energy/count>.009 && max_rough-min_rough>.02f,
              "Cloth, leather and hair contain actual normal/roughness variation, not flat placeholder maps");
    std::cout<<names[i]<<": mean roughness "<<rough_sum/count<<", linear tint "<<color_sum/count<<'\n';
  }
  const auto lips=sample(t.base_color,t.base_color_mips,CharacterSurface::Face,.25f,.25f,true);
  const auto temple=sample(t.base_color,t.base_color_mips,CharacterSurface::Face,.05f,.45f,true);
  require(lips.x/lips.y>temple.x/temple.y+.06f,"Forward lip region changes pigment hue without a painted catchlight");
  const auto face_front=character_surface_uv(CharacterSurface::Face,.25f,.56f);
  const auto face_back=character_surface_uv(CharacterSurface::Face,.75f,.56f);
  require(face_front.x<face_back.x && face_front.y==face_back.y,"Face front and back follow the documented theta-wrap convention");
}
void filtering_and_seams(const MaterialTextures& t) {
  for(auto surface:surfaces) {
    for(float lod:{0.f,.5f,1.f,2.f,3.5f,4.f,5.f,6.f,30.f}) {
      for(float v:{0.f,.13f,.5f,.87f,1.f}) {
        const auto a=sample(t.base_color,t.base_color_mips,surface,0,v,true,lod);
        const auto b=sample(t.base_color,t.base_color_mips,surface,1,v,true,lod);
        if(!close(a,b,lod==0?.00001f:.025f))
          throw std::runtime_error(std::string("Wrapped tint seam: ")+names[unsigned(surface)]+" LOD "+
              std::to_string(lod)+" V "+std::to_string(v)+" R "+std::to_string(a.x)+"/"+std::to_string(b.x));
        const auto na=sample(t.normal,t.normal_mips,surface,0,v,false,lod);
        const auto nb=sample(t.normal,t.normal_mips,surface,1,v,false,lod);
        require(close(na,nb,lod==0?.00001f:.026f),"Wrapped normal maps have no exceptional seam at any retained LOD");
      }
    }
    const auto coarse=sample(t.metallic_roughness,t.metallic_roughness_mips,surface,.98f,.98f,false,6);
    const auto far=sample(t.metallic_roughness,t.metallic_roughness_mips,surface,.98f,.98f,false,1000);
    require(close(coarse,far),"Extreme distance clamps to the region-safe final mip instead of combining materials");
  }
  // A destructive neighboring-material fixture proves isolation independently of
  // the production image's gentle colors. Every unrelated rectangle turns vivid
  // magenta with metallic B=1; even fractional-LOD edge samples must not change.
  for(auto surface:surfaces) {
    const auto& r=character_surface_region(surface);
    RgbaImage sentinel=t.metallic_roughness;
    for(int y=0;y<1024;++y) for(int x=0;x<1024;++x) {
      if(x>=r.x && x<r.x+r.size && y>=r.y && y<r.y+r.size) continue;
      const auto p=(std::size_t(y)*1024+x)*4;
      sentinel.pixels[p]=0; sentinel.pixels[p+1]=0; sentinel.pixels[p+2]=255;
    }
    auto chain=build_mip_chain(sentinel,TextureEncoding::Linear);
    std::vector<RgbaImage> mips;
    for(int level=1;level<=6;++level) mips.push_back(std::move(chain[std::size_t(level)]));
    for(float lod:{0.f,.5f,1.7f,3.f,4.9f,5.5f,6.f,100.f})
      for(float u:{0.f,.001f,.5f,.999f,1.f}) for(float v:{0.f,.001f,.5f,.999f,1.f}) {
        const auto original=sample(t.metallic_roughness,t.metallic_roughness_mips,surface,u,v,false,lod);
        const auto modified=sample(sentinel,mips,surface,u,v,false,lod);
        require(close(original,modified),"Bilinear/trilinear samples never touch adjacent atlas materials, including corners at maximum LOD");
      }
  }
  // Read actual production minified texels: cloth remains rough dielectric;
  // its immediate metal neighbor remains conductive, even under huge footprints.
  const auto rubber=sample_material_texture(t.metallic_roughness,t.metallic_roughness_mips,
      character_surface_uv(CharacterSurface::Rubber,1,1),false,{{9,9},{-9,9}});
  const auto metal=sample_material_texture(t.metallic_roughness,t.metallic_roughness_mips,
      character_surface_uv(CharacterSurface::Metal,0,0),false,{{9,9},{-9,9}});
  require(rubber.y>.84f && rubber.z==0 && metal.y<.33f && metal.z>.93f,
          "Actual footprint-driven material samples preserve dielectric/conductor separation at extreme minification");
}
void material_contract(const std::shared_ptr<const MaterialTextures>& t) {
  const auto material=make_character_surface_material();
  require(material.textures==t && character_surface_textures()==t,"All character instances share one immutable atlas allocation");
  require(material.metallic==1 && material.roughness==1 && material.normal_scale==1,
          "Material factors preserve the atlas's independent roughness and metallic channels");
  require(material.albedo.x==1 && material.albedo.y==1 && material.albedo.z==1 &&
          material.world_uv_scale==0 && material.texture==TextureSlot::None,
          "Material uses the authored mesh UVs and vertex colors without projected legacy textures");
  require(material.emissive==0 && material.opacity==1 && !material.alpha_blend && material.alpha_cutoff<0,
          "The character material is opaque and nonemissive in every backend");
}
#if FURY_CHARACTER_SURFACE_GL_TESTS
constexpr int gl_size=64;
constexpr float gl_center_x=1.f/gl_size,gl_center_y=-1.f/gl_size;
using Pixel=std::array<int,3>;
int display(float value) { return int(std::pow(value/(1.f+value),1.f/2.2f)*255.f+.5f); }
Mesh surface_quad(CharacterSurface surface,float pixels,bool constant_uv=false) {
  const float r=pixels/gl_size;
  const auto uv0=character_surface_uv(surface,constant_uv?.5f:0.f,constant_uv?.5f:0.f);
  const auto uv1=character_surface_uv(surface,constant_uv?.5f:1.f,constant_uv?.5f:1.f);
  Mesh mesh;
  mesh.vertices={
    {{gl_center_x-r,gl_center_y-r,0},{0,0,1},{1,1,1},uv0},
    {{gl_center_x+r,gl_center_y-r,0},{0,0,1},{1,1,1},{uv1.x,uv0.y}},
    {{gl_center_x+r,gl_center_y+r,0},{0,0,1},{1,1,1},uv1},
    {{gl_center_x-r,gl_center_y+r,0},{0,0,1},{1,1,1},{uv0.x,uv1.y}}
  };
  mesh.indices={0,1,2,0,2,3};
  return mesh;
}
struct GlFixture {
  std::unique_ptr<IRenderBackend> backend;
  Lighting light;
  explicit GlFixture(SDL_Window* window):backend(create_gl_backend()) {
    require(backend && backend->create(window,gl_size,gl_size),"Actual GL backend initializes after a successful context probe");
    require(backend->kind()==RenderBackendKind::OpenGL,"Atlas GL test never silently falls back to another renderer");
    backend->set_msaa_samples(0);
    light.ambient={1,1,1}; light.sun_intensity=0; light.sun_color={1,1,1};
    light.sun_direction={0,0,-1}; light.ao_strength=0; light.point_light_count=0;
    light.fog_start=100; light.fog_end=200;
    light.enable_bloom=light.enable_reflections=light.enable_shadows=false;
  }
  Pixel draw(const Mesh& mesh,const Material& material) {
    backend->set_view_proj(Mat4::identity(),Mat4::identity());
    backend->set_camera_position({gl_center_x,gl_center_y,3});
    backend->set_lighting(light); backend->set_time(0);
    backend->begin_frame({0,0,0,255});
    backend->draw_mesh(mesh,Mat4::identity(),material);
    int w=0,h=0; std::vector<std::uint8_t> image;
    require(backend->read_rgb_framebuffer(image,w,h) && w==gl_size && h==gl_size,
            "Read a complete actual OpenGL RGB framebuffer");
    const auto p=(std::size_t(gl_size/2)*gl_size+gl_size/2)*3;
    return {image[p],image[p+1],image[p+2]};
  }
};
void bounded_mips(RgbaImage& base,std::vector<RgbaImage>& mips,TextureEncoding encoding,bool bounded) {
  auto chain=build_mip_chain(base,encoding);
  mips.clear();
  const auto count=bounded?std::size_t(character_surface_max_mip):chain.size()-1;
  for(std::size_t level=1;level<=count;++level) mips.push_back(std::move(chain[level]));
}
void gl_region_isolation(GlFixture& fixture,const std::shared_ptr<const MaterialTextures>& original) {
  auto get_parameter=reinterpret_cast<void(*)(gl::GLenum,gl::GLenum,gl::GLint*)>(
      SDL_GL_GetProcAddress("glGetTexParameteriv"));
  require(get_parameter!=nullptr,"Query the actual GL texture maximum level");
  for(auto surface:{CharacterSurface::Face,CharacterSurface::Hair,CharacterSurface::Skin,
                   CharacterSurface::Jacket,CharacterSurface::Eyes,CharacterSurface::Rubber,CharacterSurface::Metal}) {
    const auto& r=character_surface_region(surface);
    auto hostile=std::make_shared<MaterialTextures>(*original);
    for(int y=0;y<1024;++y) for(int x=0;x<1024;++x) {
      if(x>=r.x && x<r.x+r.size && y>=r.y && y<r.y+r.size) continue;
      const auto p=(std::size_t(y)*1024+x)*4;
      hostile->base_color.pixels[p]=255; hostile->base_color.pixels[p+1]=0; hostile->base_color.pixels[p+2]=255;
      hostile->metallic_roughness.pixels[p+1]=0; hostile->metallic_roughness.pixels[p+2]=255;
    }
    bounded_mips(hostile->base_color,hostile->base_color_mips,TextureEncoding::SRGB,true);
    bounded_mips(hostile->metallic_roughness,hostile->metallic_roughness_mips,TextureEncoding::Linear,true);
    auto material=make_character_surface_material(); material.normal_scale=0;
    // Quarter-pixel geometry covers a known pixel center, while helper-fragment
    // derivatives request LOD >9. Ordinary distant cards can hide the bug by
    // never requesting levels where complete-atlas filtering crosses regions.
    auto tiny=surface_quad(surface,.25f);
    fixture.light.ambient={1,1,1}; fixture.light.sun_intensity=0;
    const auto expected=fixture.draw(tiny,material);
    if(expected[1]<=170) throw std::runtime_error("Subpixel fixture needs a real lit pixel: RGB "+
        std::to_string(expected[0])+","+std::to_string(expected[1])+","+std::to_string(expected[2]));
    material.textures=hostile;
    const auto protected_pixel=fixture.draw(tiny,material);
    for(int c=0;c<3;++c)
      require(std::abs(expected[c]-protected_pixel[c])<=1,
              "Actual GL pixels retain their surface color at severe minification despite hostile neighboring materials");
    for(int map:{0,3,4}) {
      gl::ActiveTexture(gl::GL_TEXTURE0+map);
      gl::GLint max_level=-1;
      get_parameter(gl::GL_TEXTURE_2D,gl::GL_TEXTURE_MAX_LEVEL,&max_level);
      require(max_level==6,"Actual uploaded color, normal and ORM textures retain their six-level atlas cap");
    }
    gl::ActiveTexture(gl::GL_TEXTURE0);
    fixture.light.ambient={0,0,0}; fixture.light.sun_intensity=1;
    material.albedo={0,0,0}; material.textures=original;
    const auto expected_response=fixture.draw(tiny,material);
    material.textures=hostile;
    const auto protected_response=fixture.draw(tiny,material);
    for(int c=0;c<3;++c)
      require(std::abs(expected_response[c]-protected_response[c])<=1,
              "Actual GL roughness/metallic response cannot receive neighboring ORM values at severe minification");

    // Positive control: deliberately unsafe full mips must visibly contaminate
    // the same pixel, proving the fixture would catch the original upload bug.
    auto unsafe=std::make_shared<MaterialTextures>(*hostile);
    bounded_mips(unsafe->base_color,unsafe->base_color_mips,TextureEncoding::SRGB,false);
    material.textures=unsafe; material.albedo={1,1,1};
    fixture.light.ambient={1,1,1}; fixture.light.sun_intensity=0;
    const auto contaminated=fixture.draw(tiny,material);
    require(expected[1]-contaminated[1]>20,
            "The full-chain control visibly bleeds neighboring regions, proving this GL pixel regression is sensitive");
    std::cout<<"GL minification "<<names[unsigned(surface)]<<": protected green "<<protected_pixel[1]
             <<", unsafe full-chain green "<<contaminated[1]<<'\n';
  }
}
void gl_surface_response(GlFixture& fixture,const std::shared_ptr<const MaterialTextures>& textures) {
  fixture.light.ambient={0,0,0}; fixture.light.sun_intensity=1;
  std::array<int,3> response{};
  const std::array<CharacterSurface,3> dielectrics{{CharacterSurface::Eyes,CharacterSurface::Skin,CharacterSurface::Jacket}};
  for(unsigned i=0;i<dielectrics.size();++i) {
    auto quad=surface_quad(dielectrics[i],40,true);
    auto material=make_character_surface_material(); material.albedo={0,0,0}; material.normal_scale=0;
    const auto orm=sample(textures->metallic_roughness,textures->metallic_roughness_mips,dielectrics[i],.5f,.5f,false);
    response[i]=fixture.draw(quad,material)[0];
    require(std::abs(response[i]-display(.04f*(1.f-.85f*orm.y)))<=2,
            "Real eye/skin/cloth highlights use the atlas's linear G roughness and dielectric B channel");
  }
  require(response[0]>response[1]+5 && response[1]>response[2]+5,
          "Smooth eyes, skin and rough clothing remain visibly distinct in actual GL lighting");
  auto quad=surface_quad(CharacterSurface::Metal,40,true);
  auto material=make_character_surface_material(); material.normal_scale=0; material.albedo={.45f,.45f,.45f};
  const auto orm=sample(textures->metallic_roughness,textures->metallic_roughness_mips,CharacterSurface::Metal,.5f,.5f,false);
  const auto color=sample(textures->base_color,textures->base_color_mips,CharacterSurface::Metal,.5f,.5f,true);
  const float base=.45f*color.x;
  const float direct=base*(1.f-.9f*orm.z)+(.04f*(1.f-orm.z)+base*orm.z)*(1.f-.85f*orm.y);
  const int metal=fixture.draw(quad,material)[0];
  require(std::abs(metal-display(direct))<=2,"Hardware's real GL pixel combines sRGB-decoded color with linear metallic B");
  material.metallic=0;
  const int disabled=fixture.draw(quad,material)[0];
  require(disabled>metal+5,"Disabling the metallic factor visibly changes hardware, catching a zero-metallic integration regression");
  std::cout<<"GL highlight pixels eyes/skin/jacket="<<response[0]<<'/'<<response[1]<<'/'<<response[2]
           <<"; hardware="<<metal<<", metallic disabled="<<disabled<<'\n';
}
int gl_tests(const std::shared_ptr<const MaterialTextures>& textures) {
  if(SDL_Init(SDL_INIT_VIDEO)!=0) { std::cout<<"SKIP: SDL video unavailable: "<<SDL_GetError()<<'\n'; return 77; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0); SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);
  SDL_Window* window=SDL_CreateWindow("Character atlas GL tests",0,0,gl_size,gl_size,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
  if(!window) { std::cout<<"SKIP: GL window unavailable: "<<SDL_GetError()<<'\n'; SDL_Quit(); return 77; }
  SDL_GLContext probe=SDL_GL_CreateContext(window);
  if(!probe) { std::cout<<"SKIP: GL context unavailable: "<<SDL_GetError()<<'\n'; SDL_DestroyWindow(window); SDL_Quit(); return 77; }
  SDL_GL_DeleteContext(probe);
  int result=0;
  try {
    GlFixture fixture(window);
    std::cout<<"Character atlas GL driver: "<<reinterpret_cast<const char*>(gl::GetString(gl::GL_RENDERER))<<'\n';
    gl_region_isolation(fixture,textures); gl_surface_response(fixture,textures);
    const auto error=reinterpret_cast<gl::GLenum(*)()>(SDL_GL_GetProcAddress("glGetError"));
    require(error && error()==0,"Character atlas GL regressions leave no graphics API errors");
    std::cout<<"Character atlas GL pixel tests passed\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
  SDL_DestroyWindow(window); SDL_Quit();
  return result;
}
#else
int gl_tests(const std::shared_ptr<const MaterialTextures>&) {
  std::cout<<"SKIP: build with SDL headers and Fury::Engine for --gl pixel tests\n";
  return 77;
}
#endif
void write_ppm(const std::filesystem::path& path,const RgbaImage& image) {
  std::ofstream out(path,std::ios::binary);
  out<<"P6\n"<<image.width<<' '<<image.height<<"\n255\n";
  for(std::size_t p=0;p<image.pixels.size();p+=4)
    out.write(reinterpret_cast<const char*>(image.pixels.data()+p),3);
  require(bool(out),"Write reviewable atlas pixels");
}
}  // namespace

int main(int argc,char** argv) {
  try {
    layout_and_api();
    const auto textures=character_surface_textures();
    channels_and_physical_response(*textures);
    filtering_and_seams(*textures);
    material_contract(textures);
    if(argc==2 && std::string(argv[1])=="--gl") return gl_tests(textures);
    if(argc==3 && std::string(argv[1])=="--dump") {
      const std::filesystem::path directory=argv[2];
      std::filesystem::create_directories(directory);
      write_ppm(directory/"character_base_color.ppm",textures->base_color);
      write_ppm(directory/"character_normal.ppm",textures->normal);
      write_ppm(directory/"character_orm.ppm",textures->metallic_roughness);
    } else if(argc!=1) throw std::runtime_error("Usage: character_surface_tests [--dump directory | --gl]");
    std::cout<<"Character surface atlas tests passed\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"Character surface atlas test failed: "<<error.what()<<'\n';
    return 1;
  }
}
