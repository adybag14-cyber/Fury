#include "fury/character_profile.hpp"
#include "fury/character_surface.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>
namespace fury {
namespace {
constexpr float pi = 3.14159265358979323846f;
using B = CharacterBone;
using S = CharacterSurface;
float sq(float x) { return x * x; }
float g(float x, float center, float width) {
  return std::exp(-sq((x - center) / width));
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * (3 - 2 * t);
}
CharacterInfluence wt(B a, B b, float first) {
  return {std::uint8_t(a), std::uint8_t(b), first};
}
CharacterInfluence wt(B a) { return wt(a, a, 1); }
struct P {
  Vec3 p;
  Vec3 c;
  CharacterInfluence w;
  Vec2 uv;
};
struct Sculpt {
  CharacterModel &m;
  float h;
  bool near;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> smooth_seams;
  std::uint32_t vertex(const P &p) {
    auto i = std::uint32_t(m.bind_mesh.vertices.size());
    m.bind_mesh.vertices.push_back({p.p * h, {}, p.c, p.uv});
    m.influences.push_back(p.w);
    return i;
  }
  void tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    auto &v = m.bind_mesh.indices;
    v.insert(v.end(), {a, b, c});
  }
  void grid(int columns, int rows, const std::function<P(float, float)> &point,
            bool seam = true, bool caps = true) {
    auto base = std::uint32_t(m.bind_mesh.vertices.size());
    for (int j = 0; j <= rows; ++j)
      for (int i = 0; i <= columns; ++i)
        vertex(point(float(i) / columns, float(j) / rows));
    for (int j = 0; j < rows; ++j)
      for (int i = 0; i < columns; ++i) {
        auto a = base + j * (columns + 1) + i, b = a + columns + 1;
        tri(a, b, b + 1);
        tri(a, b + 1, a + 1);
      }
    if (seam)
      for (int j = 0; j <= rows; ++j)
        smooth_seams.push_back(
            {base + j * (columns + 1), base + j * (columns + 1) + columns});
    if (caps)
      for (int end = 0; end < 2; ++end) {
        P center = point(0, float(end));
        center.p = {};
        for (int i = 0; i < columns; ++i)
          center.p += point(float(i) / columns, float(end)).p;
        center.p = center.p * (1.f / float(columns));
        auto c = vertex(center),
             ring = std::uint32_t(m.bind_mesh.vertices.size());
        for (int i = 0; i <= columns; ++i)
          vertex(point(float(i) / columns, float(end)));
        for (int i = 0; i < columns; ++i) {
          if (end)
            tri(c, ring + i + 1, ring + i);
          else
            tri(c, ring + i, ring + i + 1);
        }
      }
  }
  void ellipsoid(Vec3 center, Vec3 radius, Vec3 color, S surface, B bone,
                 int columns = 20, int rows = 10) {
    grid(columns, rows, [&](float u, float v) {
      float a = 2 * pi * u, e = lerp(-pi * .49f, pi * .49f, v);
      return P{center + Vec3{radius.x * std::cos(a) * std::cos(e),
                             radius.y * std::sin(e),
                             radius.z * std::sin(a) * std::cos(e)},
               color, wt(bone), character_surface_uv(surface, u, v)};
    });
  }
  void tube(const std::vector<Vec3> &path, const std::vector<float> &radii,
            float flatten, Vec3 color, S surface,
            const std::function<CharacterInfluence(float)> &weights,
            int sides = 8, Vec3 preferred_axis = {}) {
    if (path.size() < 2 || path.size() != radii.size())
      throw std::invalid_argument("Invalid sculpt curve");
    const int rows = int(path.size()) - 1;
    std::vector<Vec3> first_axis(path.size()), second_axis(path.size());
    for (int j = 0; j <= rows; ++j) {
      const Vec3 tangent =
          normalize(path[std::min(rows, j + 1)] - path[std::max(0, j - 1)]);
      Vec3 reference = j ? first_axis[j - 1]
                         : (dot(preferred_axis, preferred_axis) > 0.f
                                ? preferred_axis
                                : (std::fabs(tangent.y) < .8f ? Vec3{0, 1, 0}
                                                              : Vec3{1, 0, 0}));
      Vec3 projected = reference - tangent * dot(reference, tangent);
      if (dot(projected, projected) < 1e-12f) {
        reference = std::fabs(tangent.x) < .8f ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        projected = reference - tangent * dot(reference, tangent);
      }
      first_axis[j] = normalize(projected);
      second_axis[j] = normalize(cross(first_axis[j], tangent));
    }
    grid(sides, rows, [&](float u, float v) {
      int j = std::min(rows, int(std::lround(v * rows)));
      float angle = 2 * pi * u;
      return P{path[j] + first_axis[j] * (std::cos(angle) * radii[j]) +
                   second_axis[j] * (std::sin(angle) * radii[j] * flatten),
               color, weights(v), character_surface_uv(surface, u, v)};
    });
  }
  void curve(const std::vector<Vec3> &points, float radius, Vec3 color,
             S surface, B bone, int sides = 6, float flatten = 1.f) {
    tube(
        points, std::vector<float>(points.size(), radius), flatten, color,
        surface, [&](float) { return wt(bone); }, sides);
  }
  void finish() {
    auto &mesh = m.bind_mesh;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
      auto &a = mesh.vertices[mesh.indices[i]];
      auto &b = mesh.vertices[mesh.indices[i + 1]];
      auto &c = mesh.vertices[mesh.indices[i + 2]];
      auto n = cross(b.position - a.position, c.position - a.position);
      a.normal += n;
      b.normal += n;
      c.normal += n;
    }
    for (auto pair : smooth_seams) {
      auto n =
          mesh.vertices[pair.first].normal + mesh.vertices[pair.second].normal;
      mesh.vertices[pair.first].normal = n;
      mesh.vertices[pair.second].normal = n;
    }
    for (auto &v : mesh.vertices) {
      const float largest =
          std::max({std::fabs(v.normal.x), std::fabs(v.normal.y),
                    std::fabs(v.normal.z)});
      if (largest > 0.f && std::isfinite(largest)) {
        Vec3 scaled = v.normal * (1.f / largest);
        v.normal = scaled * (1.f / std::sqrt(dot(scaled, scaled)));
      }
    }
  }
};
struct ProfileRow {
  float y, x, z, cz;
};
constexpr std::array<ProfileRow, 12> head_rows{{{.355f, .007f, .012f, .012f},
                                                {.366f, .023f, .031f, .009f},
                                                {.381f, .033f, .044f, .003f},
                                                {.399f, .039f, .054f, 0.f},
                                                {.418f, .043f, .060f, -.001f},
                                                {.438f, .0445f, .060f, -.002f},
                                                {.452f, .043f, .060f, -.003f},
                                                {.465f, .044f, .062f, -.004f},
                                                {.479f, .040f, .058f, -.005f},
                                                {.489f, .031f, .047f, -.006f},
                                                {.494f, .020f, .032f, -.007f},
                                                {.497f, .003f, .004f, -.007f}}};
ProfileRow head_row(float y) {
  for (std::size_t i = 1; i < head_rows.size(); ++i)
    if (y <= head_rows[i].y) {
      const auto a = head_rows[i - 1], b = head_rows[i],
                 previous = head_rows[i >= 2 ? i - 2 : 0],
                 next = head_rows[std::min(i + 1, head_rows.size() - 1)];
      float t = std::clamp((y - a.y) / (b.y - a.y), 0.f, 1.f), t2 = t * t,
            t3 = t2 * t, span = b.y - a.y;
      auto interpolate = [&](float av, float bv, float pv, float nv) {
        float first = (bv - pv) / (b.y - previous.y) * span,
              second = (nv - av) / (next.y - a.y) * span;
        return (2 * t3 - 3 * t2 + 1) * av + (t3 - 2 * t2 + t) * first +
               (-2 * t3 + 3 * t2) * bv + (t3 - t2) * second;
      };
      return {y, std::max(.0005f, interpolate(a.x, b.x, previous.x, next.x)),
              std::max(.0005f, interpolate(a.z, b.z, previous.z, next.z)),
              interpolate(a.cz, b.cz, previous.cz, next.cz)};
    }
  return head_rows.back();
}
float face_depth(float x, float y) {
  auto r = head_row(y);
  float z = r.cz + r.z * std::sqrt(std::max(0.f, 1 - sq(x / r.x)));
  // Coherent sculpted planes: brow/orbits, zygomatic cheek, nasal bridge/ala,
  // muzzle, philtrum, mouth recess, chin and the submental turn are one
  // surface.
  for (float side : {-1.f, 1.f}) {
    z -= .0054f * g(x, side * .0177f, .0108f) * g(y, .434f, .0072f);
    z += .0031f * g(x, side * .019f, .013f) * g(y, .446f, .005f);
    z += .0029f * g(x, side * .027f, .012f) * g(y, .415f, .009f);
    z -= .0015f * g(x, side * .028f, .011f) * g(y, .395f, .010f);
    z += .0028f * g(x, side * .0064f, .0042f) * g(y, .405f, .004f);
  }
  z += .0082f * g(x, 0, .0043f) * g(y, .424f, .016f);
  z += .011f * g(x, 0, .0055f) * g(y, .408f, .0056f);
  z += .0027f * g(x, 0, .016f) * g(y, .391f, .009f);
  z -= .0012f * g(x, 0, .0022f) * g(y, .399f, .0038f);
  z -= .0015f * g(x, 0, .015f) * g(y, .387f, .002f);
  z += .0075f * g(x, 0, .018f) * g(y, .372f, .006f);
  return z;
}
float head_angle(
    float u) { // Allocate most samples to the face, fewer under rear hair.
  if (u < .12f)
    return u / .12f * pi / 6;
  if (u < .63f)
    return pi / 6 + (u - .12f) / .51f * (2 * pi / 3);
  return 5 * pi / 6 + (u - .63f) / .37f * (7 * pi / 6);
}
void make_head(Sculpt &b) {
  auto skin = b.m.appearance.skin;
  int columns = b.near ? 88 : 28, rows = b.near ? 44 : 18;
  b.grid(columns, rows, [&](float u, float v) {
    float angle = head_angle(u),
          y = lerp(head_rows.front().y, head_rows.back().y, v);
    auto r = head_row(y);
    float x = r.x * std::cos(angle), z = r.cz + r.z * std::sin(angle);
    float front = smooth((std::sin(angle) - .25f) / .7f);
    z = lerp(z, face_depth(x, y), front);
    Vec3 color = skin;
    return P{{x, y, z},
             color,
             wt(B::Head),
             character_surface_uv(S::Face, angle / (2 * pi), v)};
  });
  for (float side : {-1.f, 1.f}) {
    float ex = side * .0177f, ey = .434f;
    float ez = face_depth(ex, ey) - .0064f;
    // Anatomical globe below the socket surface; lids wrap its visible
    // aperture.
    b.ellipsoid({ex, ey, ez}, {.0082f, .0070f, .0080f}, {.78f, .79f, .74f},
                S::Eyes, B::Head, b.near ? 24 : 10, b.near ? 10 : 6);
    // Iris is a curved, shallow disc, with a dark limbal ring and pupil.
    auto disc = [&](float radius, float z, Vec3 color) {
      b.ellipsoid({ex, ey, z}, {radius, radius, .00065f}, color, S::Eyes,
                  B::Head, b.near ? 20 : 10, b.near ? 4 : 3);
    };
    disc(.0035f, ez + .00758f, {.038f, .043f, .028f});
    disc(.0030f, ez + .00791f, b.m.appearance.eyes);
    disc(.0014f, ez + .00832f, {.009f, .010f, .009f});
    for (bool upper : {false, true}) {
      std::vector<Vec3> lid;
      int n = b.near ? 20 : 8;
      for (int i = 0; i <= n; ++i) {
        float t = float(i) / n, dx = lerp(-.0080f, .0080f, t),
              dy = (upper ? .0034f : -.0023f) *
                       std::pow(std::max(0.f, std::sin(pi * t)), .75f) +
                   side * dx * .055f;
        float z = ez + .0080f * std::sqrt(std::max(.05f, 1 - sq(dx / .0086f) -
                                                             sq(dy / .0075f)));
        lid.push_back({ex + dx, ey + dy, z + .00020f});
      }
      b.curve(lid, upper ? .00090f : .00066f, skin * (upper ? .92f : 1.02f),
              S::Skin, B::Head, b.near ? 8 : 4, .75f);
    }
    std::vector<Vec3> crease, brow;
    int n = b.near ? 22 : 10;
    for (int i = 0; i <= n; ++i) {
      float t = float(i) / n, x = ex + lerp(-.0102f, .0115f, t),
            y = .447f + .002f * std::sin(pi * t) - side * (x - ex) * .12f;
      brow.push_back({x, y, face_depth(x, y) + .00035f});
      float cy = .4415f + .0028f * std::sin(pi * t);
      crease.push_back({x, cy, face_depth(x, cy) + .0001f});
    }
    b.curve(brow, b.near ? .00085f : .0010f, b.m.appearance.hair * .8f, S::Hair,
            B::Head, b.near ? 6 : 4, .42f);
    if (b.near)
      b.curve(crease, .00025f, skin * .8f, S::Skin, B::Head, 4, .4f);
    // Nasal wings surround inset nostril volumes, not painted triangles.
    b.ellipsoid(
        {side * .0051f, .4038f, face_depth(side * .0051f, .4038f) - .0005f},
        {.0018f, .00095f, .00135f}, skin * .30f, S::Skin, B::Head,
        b.near ? 16 : 8, 6);
    // Sculpted pinna with helix and concha; intentionally asymmetric inner
    // ridges.
    Vec3 ear{side * .0448f, .425f, -.004f};
    b.ellipsoid(ear, {.0070f, .0165f, .0075f}, skin * .96f, S::Skin, B::Head,
                b.near ? 20 : 8, b.near ? 12 : 6);
    std::vector<Vec3> helix;
    int er = b.near ? 24 : 10;
    for (int i = 0; i <= er; ++i) {
      float a = 2 * pi * float(i) / er;
      helix.push_back(ear + Vec3{side * (.0047f + .0013f * std::sin(a)),
                                 .014f * std::cos(a), .0062f * std::sin(a)});
    }
    b.curve(helix, .0014f, skin, S::Skin, B::Head, b.near ? 6 : 4);
    b.ellipsoid(ear + Vec3{side * .0063f, -.001f, .001f},
                {.0008f, .0068f, .0038f}, skin * .65f, S::Skin, B::Head,
                b.near ? 12 : 6, b.near ? 6 : 4);
    std::vector<Vec3> anti;
    for (int i = 0; i <= 10; ++i) {
      float t = float(i) / 10;
      anti.push_back(ear + Vec3{side * .007f, lerp(-.006f, .009f, t),
                                -.001f + .002f * std::sin(pi * t)});
    }
    if (b.near)
      b.curve(anti, .0008f, skin * .98f, S::Skin, B::Head, 5);
  }
  // Lips have a cupid bow, a rounded lower vermilion and an embedded mouth
  // seam.
  for (int part = 0; part < 3; ++part) {
    std::vector<Vec3> path;
    std::vector<float> radii;
    int n = b.near ? 32 : 14;
    for (int i = 0; i <= n; ++i) {
      float t = float(i) / n, x = lerp(-.0145f, .0145f, t),
            shape = std::pow(std::max(0.f, std::sin(pi * t)), .75f);
      float y = .3888f;
      if (part == 0)
        y += .0014f * shape + .00065f * g(std::fabs(x), .0045f, .0027f);
      if (part == 1)
        y -= .0019f * shape;
      path.push_back(
          {x, y,
           face_depth(x, y) + (.0018f + (part == 1 ? .0006f : 0.f)) * shape});
      radii.push_back((part == 2   ? .00025f
                       : part == 0 ? .0011f
                                   : .00155f) *
                      std::max(.12f, shape));
    }
    Vec3 color = part == 2 ? skin * .32f
                           : Vec3{skin.x * .94f, skin.y * .70f, skin.z * .70f};
    b.tube(
        path, radii, part == 2 ? .5f : 1.25f, color, S::Skin,
        [](float) { return wt(B::Head); }, b.near ? 8 : 5);
  }
}
float hair_bottom(float angle) {
  float f = std::max(0.f, std::sin(angle)),
        back = std::max(0.f, -std::sin(angle));
  return .428f + .040f * f - .056f * back - .010f * f * std::cos(angle);
}
Vec3 hair_point(float u, float v) {
  float a = 2 * pi * u, y = lerp(hair_bottom(a), .5f, v);
  auto r = head_row(std::min(y, .494f));
  float top = smooth((y - .494f) / .006f);
  float wave = .0012f * std::sin(a * 9 + v * 4) * std::sin(pi * v);
  float rx = lerp(r.x + .0035f + wave, .001f, top),
        rz = lerp(r.z + .0045f + wave, .001f, top);
  return {rx * std::cos(a), y, r.cz + rz * std::sin(a)};
}
void make_hair(Sculpt &b) {
  Vec3 hair = b.m.appearance.hair;
  hair = hair * .75f + Vec3{.026f, .012f, .007f};
  b.grid(b.near ? 64 : 24, b.near ? 16 : 8, [&](float u, float v) {
    float tone = .93f + .07f * std::sin(2 * pi * u * 7 + v * 4);
    return P{hair_point(u, v), hair * tone, wt(B::Head),
             character_surface_uv(S::Hair, u, v)};
  });
  // Directed overlapping locks continue over crown and down the rear
  // silhouette.
  int locks = b.near ? 28 : 6;
  for (int k = 0; k < locks; ++k) {
    std::vector<Vec3> path;
    std::vector<float> radii;
    int count = b.near ? 10 : 6;
    float root = .08f + float(k) / locks * .88f;
    for (int i = 0; i <= count; ++i) {
      float t = float(i) / count, v = lerp(.88f, .035f, t),
            u = root + .075f * std::sin(t * pi) * std::sin(root * 2 * pi);
      auto p = hair_point(u, v);
      Vec3 radial = normalize(Vec3{p.x, 0, p.z + .006f});
      p += radial * .0007f;
      path.push_back(p);
      radii.push_back(.00075f * (.4f + .6f * std::sin(pi * t)));
    }
    b.tube(
        path, radii, .55f, hair * (.89f + .10f * std::sin(float(k))), S::Hair,
        [](float) { return wt(B::Head); }, 4);
  }
}
CharacterInfluence torso_weight(float y) {
  if (y < .1f)
    return wt(B::Pelvis, B::Spine, 1 - smooth((y - .045f) / .055f));
  if (y < .21f)
    return wt(B::Spine, B::Chest, 1 - smooth((y - .10f) / .11f));
  return wt(B::Chest);
}
float jacket_rx(float y) {
  return .087f + .018f * smooth((y - .10f) / .15f) - .004f * g(y, .14f, .045f);
}
float jacket_rz(float y) {
  return .055f + .012f * g(y, .235f, .07f) + .004f * g(y, .07f, .025f);
}
float jacket_z(float x, float y, bool front = true) {
  float radius = jacket_rx(y);
  float z = jacket_rz(y) * std::sqrt(std::max(.08f, 1 - sq(x / radius)));
  return (front ? 1.f : -1.f) * z;
}
void make_torso(Sculpt &b) {
  auto &a = b.m.appearance;
  Vec3 jacket{.19f, .31f, .40f}, pants{.095f, .12f, .125f};
  b.grid(b.near ? 40 : 18, b.near ? 24 : 10, [&](float u, float v) {
    float y = lerp(.045f, .321f, v), angle = 2 * pi * u,
          front = std::max(0.f, std::sin(angle));
    float rx = jacket_rx(y), rz = jacket_rz(y);
    float fold = .0024f * g(y, .085f, .029f) *
                     std::sin((y - .04f) * 160 + angle * 1.4f) +
                 .0018f * g(y, .225f, .06f) * std::sin(angle * 5 + y * 12);
    float yy = y - .021f * std::pow(std::fabs(std::cos(angle)), 2.f) *
                       smooth((v - .80f) / .20f);
    float x = (rx + fold) * std::cos(angle), z = (rz + fold) * std::sin(angle);
    Vec3 c = jacket * (.97f + .03f * front);
    return P{
        {x, yy, z}, c, torso_weight(yy), character_surface_uv(S::Jacket, u, v)};
  });
  // Anatomical trapezius/neck, with a shaped base and taper below the jaw.
  b.grid(b.near ? 32 : 16, b.near ? 12 : 6, [&](float u, float v) {
    float y = lerp(.306f, .381f, v), angle = 2 * pi * u,
          r = lerp(.041f, .026f, smooth(v));
    float z = r * .84f * std::sin(angle) - .002f;
    z += .0011f * std::cos(angle * 2) * std::sin(pi * v);
    return P{{r * std::cos(angle), y, z},
             a.skin,
             wt(B::Neck, B::Head, 1 - smooth((v - .35f) / .65f)),
             character_surface_uv(S::Skin, u, v)};
  });
  // Collar stands away from the neck; its folded lip remains readable in rear
  // view.
  std::vector<Vec3> collar;
  const int collar_steps = b.near ? 48 : 24;
  for (int i = 0; i <= collar_steps; ++i) {
    float t = float(i) / collar_steps, angle = 2 * pi * t;
    collar.push_back({.041f * std::cos(angle),
                      .321f + .010f * std::max(0.f, -std::sin(angle)),
                      .038f * std::sin(angle)});
  }
  b.curve(collar, .0042f, jacket * .85f, S::Jacket, B::Chest, b.near ? 8 : 5,
          .70f);
  std::vector<Vec3> zip, seamL, seamR;
  const int zip_steps = b.near ? 24 : 10;
  for (int i = 0; i <= zip_steps; ++i) {
    float y = lerp(.052f, .311f, float(i) / zip_steps);
    float z = jacket_z(0, y) + .0011f;
    zip.push_back({0, y, z});
    seamL.push_back({-.006f, y, jacket_z(-.006f, y) + .0008f});
    seamR.push_back({.006f, y, jacket_z(.006f, y) + .0008f});
  }
  b.tube(
      zip, std::vector<float>(zip.size(), .0010f), .50f, {.34f, .35f, .34f},
      S::Metal, [](float v) { return torso_weight(lerp(.052f, .311f, v)); },
      b.near ? 6 : 4);
  for (auto *seam : {&seamL, &seamR})
    b.tube(
        *seam, std::vector<float>(seam->size(), .00045f), .65f, jacket * .68f,
        S::Jacket, [](float v) { return torso_weight(lerp(.052f, .311f, v)); },
        4);
  for (float side : {-1.f, 1.f}) {
    std::vector<Vec3> pocket;
    int n = b.near ? 24 : 12;
    for (int i = 0; i <= n; ++i) {
      float t = float(i) / n, angle = 2 * pi * t;
      float x =
          side * .052f +
          .027f * std::copysign(std::pow(std::fabs(std::cos(angle)), .35f),
                                std::cos(angle));
      float y = .115f + .034f * std::copysign(
                                    std::pow(std::fabs(std::sin(angle)), .35f),
                                    std::sin(angle));
      pocket.push_back({x, y, jacket_z(x, y) + .0013f});
    }
    b.tube(
        pocket, std::vector<float>(pocket.size(), .00055f), .7f, jacket * .72f,
        S::Jacket, [](float) { return wt(B::Spine); }, b.near ? 5 : 4);
    if (b.near) {
      std::vector<Vec3> panel;
      for (int i = 0; i <= 20; ++i) {
        float y = lerp(.16f, .277f, float(i) / 20),
              x = side * (.063f - .014f * std::sin(pi * float(i) / 20));
        panel.push_back({x, y, jacket_z(x, y) + .0009f});
      }
      b.curve(panel, .0004f, jacket * .78f, S::Jacket, B::Chest, 4);
    }
  }
  // Seat and hip taper. Garment construction is curved, with a narrow
  // waistband.
  b.grid(b.near ? 40 : 20, b.near ? 12 : 7, [&](float u, float v) {
    float y = lerp(-.048f, .068f, v), angle = 2 * pi * u;
    float rx = .080f + .012f * std::sin(pi * v),
          rz = .049f + .013f * std::sin(pi * v);
    return P{{rx * std::cos(angle), y, rz * std::sin(angle) - .004f},
             pants,
             wt(B::Pelvis),
             character_surface_uv(S::Pants, u, v)};
  });
}
void make_limbs(Sculpt &b) {
  auto skin = b.m.appearance.skin;
  Vec3 jacket{.19f, .31f, .40f}, pants{.095f, .12f, .125f};
  for (float sign : {-1.f, 1.f}) {
    bool right = sign > 0;
    B upper = right ? B::RightUpperArm : B::LeftUpperArm,
      fore = right ? B::RightForearm : B::LeftForearm,
      hand = right ? B::RightHand : B::LeftHand;
    float shoulder = sign * .108f, elbow = sign * .126f, wrist = sign * .148f;
    b.grid(b.near ? 24 : 12, b.near ? 24 : 10, [&](float u, float v) {
      float y = lerp(-.042f, .302f, v), angle = 2 * pi * u;
      float x = y < .12f ? lerp(wrist, elbow, smooth((y + .042f) / .162f))
                         : lerp(elbow, shoulder, smooth((y - .12f) / .16f));
      x = lerp(x, sign * .085f, smooth((y - .27f) / .032f));
      float radius =
          .018f + .011f * g(y, .025f, .07f) + .013f * g(y, .24f, .09f);
      radius *= 1 - .25f * smooth((y - .275f) / .027f);
      radius += .0015f * g(y, .125f, .035f) * std::sin(y * 210 + angle * 1.4f) +
                .0011f * g(y, -.028f, .018f) * std::sin(y * 370 + angle);
      auto weight = y < .10f   ? wt(fore)
                    : y < .15f ? wt(fore, upper, 1 - smooth((y - .10f) / .05f))
                               : wt(upper, B::Chest, smooth((.32f - y) / .08f));
      return P{
          {x + radius * std::cos(angle), y, radius * .91f * std::sin(angle)},
          jacket,
          weight,
          character_surface_uv(S::Jacket, u, v)};
    });
    // A shaped palm, four independently tapered fingers and opposable thumb.
    b.grid(b.near ? 20 : 10, b.near ? 8 : 4, [&](float u, float v) {
      float y = lerp(-.108f, -.036f, v), angle = 2 * pi * u,
            rx = lerp(.019f, .015f, smooth((v - .5f) / .5f)),
            rz = .0105f + .002f * std::sin(pi * v);
      return P{{wrist + rx * std::cos(angle), y, rz * std::sin(angle) + .0015f},
               skin,
               wt(hand),
               character_surface_uv(S::Skin, u, v)};
    });
    const float offsets[4] = {-.014f, -.0045f, .005f, .014f},
                lengths[4] = {.043f, .050f, .046f, .035f};
    for (int finger = 0; finger < 4; ++finger) {
      std::vector<Vec3> path;
      std::vector<float> radii;
      int n = b.near ? 10 : 4;
      for (int i = 0; i <= n; ++i) {
        float t = float(i) / n;
        float x = wrist + sign * offsets[finger] * (1 + .14f * t),
              y = -.099f - lengths[finger] * t,
              z = .002f + .0035f * std::sin(t * pi * .8f);
        path.push_back({x, y, z});
        radii.push_back((.0045f - .0014f * t) *
                        (1 + .08f * std::sin(t * 4 * pi)));
      }
      b.tube(
          path, radii, .88f, skin, S::Skin, [&](float) { return wt(hand); },
          b.near ? 8 : 4);
      if (b.near) {
        auto p = path.back();
        p.y += .006f;
        p.z += .0033f;
        b.ellipsoid(p, {.0030f, .0055f, .00055f},
                    skin * .90f + Vec3{.05f, .03f, .03f}, S::Skin, hand, 8, 4);
      }
    }
    std::vector<Vec3> thumb{{wrist - sign * .012f, -.063f, .002f},
                            {wrist - sign * .024f, -.073f, .008f},
                            {wrist - sign * .028f, -.089f, .012f},
                            {wrist - sign * .030f, -.100f, .015f}};
    b.tube(
        thumb, {.007f, .006f, .0051f, .0039f}, .9f, skin, S::Skin,
        [&](float) { return wt(hand); }, b.near ? 8 : 4);
    B thigh = right ? B::RightThigh : B::LeftThigh,
      shin = right ? B::RightShin : B::LeftShin,
      foot = right ? B::RightFoot : B::LeftFoot;
    float x = sign * .057f;
    b.grid(b.near ? 24 : 12, b.near ? 26 : 12, [&](float u, float v) {
      float y = lerp(-.459f, .007f, v), angle = 2 * pi * u;
      float radius = .027f + .009f * g(y, -.34f, .08f) +
                     .018f * smooth((y + .225f) / .22f);
      radius += .0017f * g(y, -.224f, .027f) * std::sin(y * 220 + angle * .6f) +
                .0013f * g(y, -.443f, .023f) * std::sin(y * 310 + angle * 1.8f);
      auto weight = y < -.249f ? wt(shin)
                    : y < -.204f
                        ? wt(shin, thigh, 1 - smooth((y + .249f) / .045f))
                        : wt(thigh, B::Pelvis, smooth((.045f - y) / .10f));
      return P{{x + radius * std::cos(angle), y,
                radius * 1.10f * std::sin(angle) - .001f},
               pants,
               weight,
               character_surface_uv(S::Pants, u, v)};
    });
    // Sole and toe box share the animator's exact supported heel/toe envelope.
    b.grid(b.near ? 28 : 14, 3, [&](float u, float v) {
      float a = 2 * pi * u, y = lerp(-.5f, -.483f, v);
      return P{{x + .032f * std::cos(a), y, .033f + .067f * std::sin(a)},
               {.07f, .072f, .068f},
               wt(foot),
               character_surface_uv(S::Rubber, u, v)};
    });
    b.grid(b.near ? 32 : 16, b.near ? 10 : 5, [&](float u, float v) {
      float a = 2 * pi * u, y = lerp(-.483f, -.430f, v),
            rx = lerp(.0315f, .022f, smooth(v)),
            rz = lerp(.066f, .027f, smooth(v)),
            cz = lerp(.033f, 0.f, smooth(v));
      return P{{x + rx * std::cos(a), y, cz + rz * std::sin(a)},
               {.17f, .12f, .083f},
               wt(foot),
               character_surface_uv(S::Leather, u, v)};
    });
    for (int lace = 0; lace < (b.near ? 5 : 3); ++lace) {
      float z = .035f + lace * .006f, y = -.445f - lace * .004f;
      std::vector<Vec3> p{
          {x - .012f, y, z}, {x, y + .0018f, z + .002f}, {x + .012f, y, z}};
      b.curve(p, .00085f, {.39f, .37f, .30f}, S::Shirt, foot, b.near ? 6 : 4);
    }
  }
}
void make_bag(Sculpt &b) {
  Vec3 leather{.26f, .16f, .078f};
  b.grid(b.near ? 32 : 16, b.near ? 12 : 6, [&](float u, float v) {
    float a = 2 * pi * u, y = lerp(-.10f, .025f, v),
          round = .88f + .12f * std::sin(pi * v);
    float x = .139f + .065f * round *
                          std::copysign(std::pow(std::fabs(std::cos(a)), .38f),
                                        std::cos(a));
    float z = .024f + .027f * round *
                          std::copysign(std::pow(std::fabs(std::sin(a)), .38f),
                                        std::sin(a));
    return P{{x, y, z},
             leather,
             wt(B::Pelvis),
             character_surface_uv(S::Leather, u, v)};
  });
  for (bool front : {true, false}) {
    std::vector<Vec3> path;
    std::vector<float> widths;
    const int strap_steps = b.near ? 20 : 12;
    for (int i = 0; i <= strap_steps; ++i) {
      float t = float(i) / strap_steps, y = lerp(.304f, .018f, t),
            x = lerp(-.079f, .139f, t);
      float z = jacket_z(x, y, front) + (front ? .003f : -.003f);
      if (t > .79f)
        z = lerp(z, front ? .053f : -.003f, smooth((t - .79f) / .21f));
      path.push_back({x, y, z});
      widths.push_back(.009f);
    }
    b.tube(
        path, widths, .13f, leather * .86f, S::Leather,
        [](float t) { return torso_weight(lerp(.304f, .018f, t)); },
        b.near ? 8 : 6);
  }
  // Top shoulder bridge and curved front flap/seam are modeled, including the
  // rear strap.
  b.tube({{-.079f, .302f, -.044f},
          {-.079f, .308f, -.026f},
          {-.079f, .311f, 0},
          {-.079f, .308f, .026f},
          {-.079f, .302f, .044f}},
         std::vector<float>(5, .009f), .08f, leather * .86f, S::Leather,
         [](float) { return wt(B::Chest); }, 8, {1, 0, 0});
  std::vector<Vec3> flap;
  int flap_steps = b.near ? 24 : 12;
  for (int i = 0; i <= flap_steps; ++i) {
    float a = 2 * pi * float(i) / flap_steps;
    flap.push_back(
        {.139f + .056f * std::copysign(std::pow(std::fabs(std::cos(a)), .4f),
                                       std::cos(a)),
         -.018f + .034f * std::copysign(std::pow(std::fabs(std::sin(a)), .4f),
                                        std::sin(a)),
         .0515f});
  }
  b.curve(flap, .00085f, leather * .58f, S::Leather, B::Pelvis, b.near ? 5 : 4);
  b.ellipsoid({.139f, -.035f, .053f}, {.006f, .004f, .0015f},
              {.42f, .35f, .20f}, S::Metal, B::Pelvis, 16, 6);
}
CharacterRig mira_rig(float height) {
  CharacterRig r;
  r.height = height;
  auto joint = [&](B bone, int parent, Vec3 p) {
    r.joints[bone_index(bone)] = {parent, p * height};
  };
  auto child = [&](B bone, B parent, Vec3 p) {
    joint(bone, int(bone_index(parent)), p);
  };
  joint(B::Pelvis, -1, {0, 0, 0});
  child(B::Spine, B::Pelvis, {0, .13f, 0});
  child(B::Chest, B::Spine, {0, .272f, 0});
  child(B::Neck, B::Chest, {0, .333f, 0});
  child(B::Head, B::Neck, {0, .394f, 0});
  for (float sign : {-1.f, 1.f}) {
    bool q = sign > 0;
    B upper = q ? B::RightUpperArm : B::LeftUpperArm,
      fore = q ? B::RightForearm : B::LeftForearm,
      hand = q ? B::RightHand : B::LeftHand,
      thigh = q ? B::RightThigh : B::LeftThigh,
      shin = q ? B::RightShin : B::LeftShin,
      foot = q ? B::RightFoot : B::LeftFoot;
    child(upper, B::Chest, {sign * .108f, .300f, 0});
    child(fore, upper, {sign * .126f, .12f, 0});
    child(hand, fore, {sign * .148f, -.044f, 0});
    child(thigh, B::Pelvis, {sign * .057f, 0, 0});
    child(shin, thigh, {sign * .057f, -.225f, 0});
    child(foot, shin, {sign * .057f, -.46f, 0});
  }
  return r;
}
} // namespace
CharacterProfile make_character_profile(float height, CharacterRole role,
                                        std::string_view identity,
                                        CharacterLod lod) {
  CharacterProfile profile;
  if (identity != "NpcCivA" || role != CharacterRole::Commuter) {
    profile.model =
        make_character_model(height, role, character_seed(identity), lod);
    return profile;
  }
  if (!std::isfinite(height) || height <= 0.f ||
      (lod != CharacterLod::Near && lod != CharacterLod::Far))
    throw std::invalid_argument("Invalid individual character request");
  auto &m = profile.model;
  m.role = role;
  m.lod = lod;
  m.seed = character_seed(identity);
  m.appearance = character_appearance(m.seed);
  m.rig = mira_rig(height);
  m.bind_mesh.vertices.reserve(lod == CharacterLod::Near ? 36000 : 12000);
  m.bind_mesh.indices.reserve(lod == CharacterLod::Near ? 180000 : 60000);
  m.influences.reserve(m.bind_mesh.vertices.capacity());
  Sculpt sculpt{m, height, lod == CharacterLod::Near, {}};
  make_torso(sculpt);
  make_limbs(sculpt);
  make_head(sculpt);
  make_hair(sculpt);
  make_bag(sculpt);
  sculpt.finish();
  profile.material = make_character_surface_material();
  profile.lod_distance = 12.f;
  profile.individualized = true;
  return profile;
}
} // namespace fury
