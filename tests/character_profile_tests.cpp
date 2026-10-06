#include "fury/character_profile.hpp"
#include "fury/character_animation.hpp"
#include "fury/character_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace fury;
constexpr std::array<float, 3> heights{{1.35f, 1.75f, 2.10f}};
constexpr std::size_t surface_count = static_cast<std::size_t>(CharacterSurface::Count);

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
// Dense-asset validation visits millions of vertices; construct diagnostic
// strings only for a failed element so sanitizer runs stay inexpensive.
void require_element(bool condition, const std::string& context, const char* message,
                     std::size_t element) {
  if (!condition) throw std::runtime_error(context + message + std::to_string(element));
}
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool exact(float a, float b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
bool exact(Vec3 a, Vec3 b) { return exact(a.x, b.x) && exact(a.y, b.y) && exact(a.z, b.z); }
bool exact(const CharacterInfluence& a, const CharacterInfluence& b) {
  return a.first == b.first && a.second == b.second && exact(a.first_weight, b.first_weight);
}
bool exact(const Vertex& a, const Vertex& b) {
  return exact(a.position, b.position) && exact(a.normal, b.normal) && exact(a.color, b.color) &&
         exact(a.uv.x, b.uv.x) && exact(a.uv.y, b.uv.y) && exact(a.opacity, b.opacity);
}
bool exact(const Mesh& a, const Mesh& b) {
  if (a.indices != b.indices || a.vertices.size() != b.vertices.size()) return false;
  for (std::size_t i = 0; i < a.vertices.size(); ++i)
    if (!exact(a.vertices[i], b.vertices[i])) return false;
  return true;
}
bool exact(const CharacterRig& a, const CharacterRig& b) {
  if (!exact(a.height, b.height)) return false;
  for (std::size_t i = 0; i < character_bone_count; ++i)
    if (a.joints[i].parent != b.joints[i].parent ||
        !exact(a.joints[i].bind_position, b.joints[i].bind_position)) return false;
  return true;
}
bool exact(const CharacterAppearance& a, const CharacterAppearance& b) {
  return exact(a.skin, b.skin) && exact(a.hair, b.hair) && exact(a.eyes, b.eyes) &&
         exact(a.shirt, b.shirt) && exact(a.trousers, b.trousers) && exact(a.shoes, b.shoes) &&
         exact(a.accent, b.accent) && exact(a.shoulder_scale, b.shoulder_scale) &&
         exact(a.waist_scale, b.waist_scale) && exact(a.face_scale, b.face_scale) &&
         a.hair_style == b.hair_style && a.face_style == b.face_style;
}
bool exact(const CharacterModel& a, const CharacterModel& b) {
  if (!exact(a.bind_mesh, b.bind_mesh) || !exact(a.rig, b.rig) ||
      !exact(a.appearance, b.appearance) || a.seed != b.seed || a.role != b.role ||
      a.lod != b.lod || a.influences.size() != b.influences.size()) return false;
  for (std::size_t i = 0; i < a.influences.size(); ++i)
    if (!exact(a.influences[i], b.influences[i])) return false;
  return true;
}
struct Bounds {
  Vec3 low{1000, 1000, 1000}, high{-1000, -1000, -1000};
  void add(Vec3 p) {
    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
  }
};
int surface_at(Vec2 uv) {
  if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return -1;
  for (std::size_t i = 0; i < surface_count; ++i) {
    const auto s = static_cast<CharacterSurface>(i);
    const auto lo = character_surface_uv(s, 0, 0), hi = character_surface_uv(s, 1, 1);
    if (uv.x >= lo.x - 1.e-7f && uv.x <= hi.x + 1.e-7f &&
        uv.y >= lo.y - 1.e-7f && uv.y <= hi.y + 1.e-7f) return static_cast<int>(i);
  }
  return -1;
}
std::string label(const CharacterModel& m) {
  return std::string(m.lod == CharacterLod::Near ? "near" : "far") +
         " height " + std::to_string(m.rig.height);
}

void check_geometry(const CharacterProfile& profile) {
  const auto& m = profile.model;
  const auto& mesh = m.bind_mesh;
  const float h = m.rig.height;
  const auto context = label(m) + ": ";
  require(profile.individualized && m.role == CharacterRole::Commuter &&
              m.seed == character_seed("NpcCivA"), context + "actual Mira identity/role");
  require(!mesh.vertices.empty() && !mesh.indices.empty() && mesh.indices.size() % 3 == 0,
          context + "nonempty indexed triangles");
  require(m.influences.size() == mesh.vertices.size(), context + "one influence per vertex");
  Bounds bounds;
  std::array<std::size_t, surface_count> surfaces{};
  std::array<bool, character_bone_count> used_bones{};
  std::array<float, 2> soles{{h, h}};
  std::vector<int> vertex_surfaces;
  vertex_surfaces.reserve(mesh.vertices.size());
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const auto& v = mesh.vertices[i];
    const auto& w = m.influences[i];
    const auto where = context + "vertex " + std::to_string(i);
    require(finite(v.position) && finite(v.normal) && finite(v.color), where + " is finite");
    require(std::fabs(length(v.normal) - 1.f) < .0002f, where + " has a unit normal");
    require(v.color.x >= 0 && v.color.y >= 0 && v.color.z >= 0 &&
                v.color.x <= 1 && v.color.y <= 1 && v.color.z <= 1 && exact(v.opacity, 1.f),
            where + " has bounded opaque material values");
    const int s = surface_at(v.uv);
    require(s >= 0, where + " UV is inside an atlas content region, beyond its gutter");
    vertex_surfaces.push_back(s);
    ++surfaces[static_cast<std::size_t>(s)];
    require(w.first < character_bone_count && w.second < character_bone_count &&
                std::isfinite(w.first_weight) && w.first_weight >= 0 && w.first_weight <= 1,
            where + " has valid normalized two-bone weights");
    if (w.first_weight > 0) used_bones[w.first] = true;
    if (w.first_weight < 1) used_bones[w.second] = true;
    if (w.first == bone_index(CharacterBone::LeftFoot)) soles[0] = std::min(soles[0], v.position.y);
    if (w.first == bone_index(CharacterBone::RightFoot)) soles[1] = std::min(soles[1], v.position.y);
    bounds.add(v.position);
  }
  for (std::size_t i = 0; i < surfaces.size(); ++i)
    require(surfaces[i] > 0, context + "modeled atlas surface " + std::to_string(i));
  for (bool used : used_bones) require(used, context + "every rig joint deforms actual vertices");
  const Vec3 extent = (bounds.high - bounds.low) * (1.f / h);
  require(extent.x > .28f && extent.x < .50f && extent.z > .12f && extent.z < .25f,
          context + "sensible nonzero body, fingers, bag and shoe extents");
  require(std::fabs(bounds.low.y / h + .5f) < .00001f &&
              std::fabs(bounds.high.y / h - .5f) < .001f,
          context + "requested height is preserved from ground to hair crown");
  for (float sole : soles) require(std::fabs(sole / h + .5f) < .00001f,
                                  context + "both feet have grounded sole geometry");
  double volume = 0;
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
    const auto where = context + "triangle " + std::to_string(i / 3);
    require(a < mesh.vertices.size() && b < mesh.vertices.size() && c < mesh.vertices.size(),
            where + " indices in range");
    require(a != b && a != c && b != c, where + " three distinct vertex indices");
    require(vertex_surfaces[a] == vertex_surfaces[b] && vertex_surfaces[b] == vertex_surfaces[c],
            where + " cannot interpolate across material atlas regions");
    const auto& va = mesh.vertices[a];
    const auto& vb = mesh.vertices[b];
    const auto& vc = mesh.vertices[c];
    const auto face = cross(vb.position - va.position, vc.position - va.position);
    require(length(face) > h * h * 1.e-12f, where + " nondegenerate geometry");
    require(dot(face, va.normal + vb.normal + vc.normal) > 0,
            where + " winding agrees with surface normals");
    volume += dot(va.position, cross(vb.position, vc.position)) / 6.;
  }
  // This sums authored overlapping shells, not a boolean union/body-volume measurement.
  require(volume > h * h * h * .003 && volume < h * h * h * .07,
          context + "outward shells enclose a positive plausible signed volume");
  for (std::size_t i = 0; i < character_bone_count; ++i) {
    const auto& joint = m.rig.joints[i];
    require(joint.parent >= -1 && joint.parent < static_cast<int>(i) && finite(joint.bind_position),
            context + "finite acyclic parent-first rig");
  }
  std::cout << context << mesh.vertices.size() << " vertices / " << mesh.indices.size() / 3
            << " triangles, extent " << extent.x << "," << extent.y << "," << extent.z
            << ", signed shell volume/h^3 " << volume / (h * h * h) << '\n';
}

void check_lods_and_materials(const CharacterProfile& near, const CharacterProfile& far) {
  const auto& a = near.model;
  const auto& b = far.model;
  require(exact(a.rig, b.rig) && exact(a.appearance, b.appearance) && a.seed == b.seed,
          "Near/far Mira retains bit-identical rig and identity");
  require(a.bind_mesh.indices.size() / 3 <= 40000 && b.bind_mesh.indices.size() / 3 <= 10000,
          "Individual asset respects reviewed CPU cap: 40,000 near / 10,000 far triangles");
  require(b.bind_mesh.indices.size() * 2 < a.bind_mesh.indices.size() &&
              b.bind_mesh.vertices.size() * 2 < a.bind_mesh.vertices.size(),
          "Far LOD eliminates more than half of actual vertices and triangles");
  require(!exact(a.bind_mesh, b.bind_mesh), "LOD changes real geometry");
  std::array<std::size_t, surface_count> av{}, bv{};
  for (const auto& v : a.bind_mesh.vertices) ++av.at(static_cast<std::size_t>(surface_at(v.uv)));
  for (const auto& v : b.bind_mesh.vertices) ++bv.at(static_cast<std::size_t>(surface_at(v.uv)));
  for (auto surface : {CharacterSurface::Face, CharacterSurface::Hair, CharacterSurface::Skin,
                       CharacterSurface::Jacket, CharacterSurface::Leather}) {
    const auto i = static_cast<std::size_t>(surface);
    require(av[i] > bv[i] * 3 / 2, "Near LOD retains additional anatomy, hair and clothing geometry");
  }
  const auto texture = character_surface_textures();
  for (const auto* p : {&near, &far}) {
    const auto& material = p->material;
    require(material.textures == texture && material.textures->base_color.valid() &&
                material.textures->normal.valid() && material.textures->metallic_roughness.valid(),
            "Actual profile binds shared base color, normal and roughness/metallic maps");
    require(exact(material.albedo, {1, 1, 1}) && material.roughness == 1 && material.metallic == 1 &&
                material.normal_scale == 1 && material.world_uv_scale == 0 &&
                material.opacity == 1 && !material.alpha_blend && material.alpha_cutoff < 0,
            "Profile material preserves vertex tint, UV0 PBR and opaque geometry");
    require(std::isfinite(p->lod_distance) && p->lod_distance > 0 &&
                p->lod_distance == near.lod_distance,
            "Both LOD profiles agree on a finite positive individual switch distance");
  }
}

CharacterAnimationSample sample_for(int mode, int frame, float h, std::uint32_t seed) {
  CharacterAnimationSample sample;
  sample.phase_cycles = static_cast<double>(frame) / 32.;
  sample.idle_time = static_cast<double>(frame) / 8.;
  sample.seed = seed;
  if (mode == 1 || mode == 2 || mode == 5) {
    sample.speed = (mode == 2 ? 2.8f : mode == 5 ? .55f : .8f) * h;
    sample.move_weight = 1;
  }
  if (mode == 3) {
    sample.talk_weight = sample.attention_weight = 1;
    sample.attention_yaw = .45f;
    sample.attention_pitch = -.10f;
  }
  if (mode == 4 || mode == 5) sample.crouch_weight = 1;
  return sample;
}
void check_animation(const CharacterModel& model) {
  const float h = model.rig.height;
  const auto bind = model.bind_mesh;
  Mesh output = bind;
  output.gpu_vao = 101; output.gpu_vbo = 102; output.gpu_ibo = 103; output.gpu_uploaded = true;
  const auto* vertices = output.vertices.data();
  const auto* indices = output.indices.data();
  const auto vertex_capacity = output.vertices.capacity(), index_capacity = output.indices.capacity();
  const auto identity = output.geometry_identity;
  constexpr const char* modes[]{"idle", "walk", "run", "talk/attention", "crouch", "crouch-walk"};
  std::size_t poses = 0;
  for (int mode = 0; mode < 6; ++mode) {
    std::vector<Vec3> previous(output.vertices.size());
    float largest_change = 0;
    for (int frame = 0; frame <= 32; ++frame) {
      const auto sample = sample_for(mode, frame, h, model.seed);
      const auto pose = sample_character_animation(model.rig, sample);
      const auto context = label(model) + " " + modes[mode] + " frame " + std::to_string(frame) + ": ";
      for (const auto& matrix : pose.skin_matrices)
        for (float value : matrix.m) require(std::isfinite(value), context + "finite skin matrix");
      apply_character_pose(model, pose, output);
      require(output.vertices.data() == vertices && output.indices.data() == indices &&
                  output.vertices.capacity() == vertex_capacity && output.indices.capacity() == index_capacity &&
                  output.vertices.size() == bind.vertices.size() && output.indices == bind.indices &&
                  output.geometry_identity == identity && output.gpu_vao == 101 &&
                  output.gpu_vbo == 102 && output.gpu_ibo == 103 && output.gpu_uploaded,
              context + "skinning keeps topology, storage and renderer handles stable");
      const auto revision = output.geometry_revision;
      require(!apply_character_pose(model, pose, output) && output.geometry_revision == revision,
              context + "identical pose does not rebuild or dirty geometry twice");
      for (std::size_t i = 0; i < output.vertices.size(); ++i) {
        const auto& v = output.vertices[i];
        const auto& source = bind.vertices[i];
        require_element(finite(v.position) && finite(v.normal) && std::fabs(length(v.normal) - 1) < .0002f,
                        context, "finite skinned vertex/unit normal ", i);
        require_element(length(v.position) < h * 1.1f, context, "no exploding deformed vertex ", i);
        require_element(exact(v.color, source.color) && exact(v.uv.x, source.uv.x) &&
                    exact(v.uv.y, source.uv.y) && exact(v.opacity, source.opacity),
                        context, "skinning preserves exact color/UV/opacity at vertex ", i);
        const auto bone = model.influences[i].first;
        if (bone == bone_index(CharacterBone::LeftFoot) || bone == bone_index(CharacterBone::RightFoot))
          require_element(v.position.y >= -.5001f * h, context,
                          "actual shoe geometry stays above ground at vertex ", i);
        if (frame) {
          const float displacement = length(v.position - previous[i]);
          require_element(displacement < h * .14f, context, "adjacent sampled frames do not snap at vertex ", i);
          largest_change = std::max(largest_change, displacement);
        }
        previous[i] = v.position;
      }
      for (std::size_t i = 0; i < output.indices.size(); i += 3) {
        const auto a = output.vertices[output.indices[i]].position;
        const auto b = output.vertices[output.indices[i + 1]].position;
        const auto c = output.vertices[output.indices[i + 2]].position;
        require_element(length(cross(b - a, c - a)) > h * h * 1.e-13f,
                        context, "skinning cannot collapse triangle ", i / 3);
      }
      ++poses;
    }
    require(largest_change > h * .000001f, std::string(modes[mode]) + " changes actual model vertices");
  }
  require(exact(bind, model.bind_mesh), "Animation never mutates the immutable bind geometry");
  std::cout << label(model) << ": " << poses << " actual skinned idle/walk/run/talk/crouch/crouch-walk poses\n";
}

void generic_identity_fallback() {
  struct Identity { const char* id; CharacterRole role; };
  using R = CharacterRole;
  constexpr Identity roster[]{{"NpcCivB", R::Market}, {"NpcCivC", R::Commuter},
      {"NpcGuard", R::Security}, {"NpcTeller", R::BankStaff}, {"NpcDeskGuard", R::Security},
      {"NpcExtraGuard", R::Security}, {"NpcEnforcer", R::Enforcer},
      {"NpcBankCust", R::Commuter}, {"NpcAlleyHmpd", R::Security}, {"NpcCivAsh", R::Market},
      {"NpcCivD", R::Dock}, {"NpcCivE", R::Dock}, {"NpcFence", R::Fence},
      {"CrewRook", R::CrewTech}, {"CrewSparrow", R::CrewScout}, {"PlayerBody", R::Player},
      {"GhostLoop", R::Ghost}, {"unknown_identity", R::Enforcer},
      {"npcciva", R::Commuter}, {"NpcCivA_extra", R::Commuter}, {"", R::Commuter}};
  std::size_t cases = 0;
  auto check = [&](float h, R role, std::string_view id, CharacterLod lod) {
    const auto profile = make_character_profile(h, role, id, lod);
    const auto generic = make_character_model(h, role, character_seed(id), lod);
    require(!profile.individualized && profile.lod_distance == 0 && !profile.material.textures,
            "Unregistered identity/role retains default generic profile behavior");
    require(exact(profile.model, generic), "Generic fallback preserves every geometry/rig/weight/appearance bit: " +
                                              std::string(id) + " " + character_role_name(role));
    ++cases;
  };
  for (float h : heights) for (auto lod : {CharacterLod::Near, CharacterLod::Far}) {
    for (const auto& identity : roster) check(h, identity.role, identity.id, lod);
    for (unsigned r = 1; r < static_cast<unsigned>(R::Count); ++r)
      check(h, static_cast<R>(r), "NpcCivA", lod);
    check(h, R::Commuter, std::string_view("NpcCivA\0extra", 13), lod);
  }
  std::cout << cases << " bit-exact generic identity/role/height/LOD fallback cases\n";
}
void input_validation() {
  for (auto id : {"NpcCivA", "unknown_identity"})
    for (auto lod : {CharacterLod::Near, CharacterLod::Far})
      for (float height : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        bool threw = false;
        try { make_character_profile(height, CharacterRole::Commuter, id, lod); }
        catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Invalid height is rejected for individual and generic identities");
      }
  for (auto id : {"NpcCivA", "unknown_identity"}) {
    bool bad_role = false, bad_lod = false;
    try { make_character_profile(1.75f, CharacterRole::Count, id, CharacterLod::Near); }
    catch (const std::invalid_argument&) { bad_role = true; }
    try { make_character_profile(1.75f, CharacterRole::Commuter, id, static_cast<CharacterLod>(255)); }
    catch (const std::invalid_argument&) { bad_lod = true; }
    require(bad_role && bad_lod, "Invalid role and LOD fail explicitly");
  }
}
}  // namespace

int main() {
  try {
    input_validation();
    generic_identity_fallback();
    for (float height : heights) {
      const auto near = make_character_profile(height, CharacterRole::Commuter, "NpcCivA", CharacterLod::Near);
      const auto far = make_character_profile(height, CharacterRole::Commuter, "NpcCivA", CharacterLod::Far);
      check_geometry(near); check_geometry(far);
      check_lods_and_materials(near, far);
      const auto repeated = make_character_profile(height, CharacterRole::Commuter, "NpcCivA", CharacterLod::Near);
      require(exact(near.model, repeated.model) && near.material.textures == repeated.material.textures,
              "Mira generation is bit-deterministic and reuses immutable textures");
      check_animation(near.model); check_animation(far.model);
    }
    std::cout << "Individual character profile checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Individual character profile check failed: " << e.what() << '\n';
    return 1;
  }
}
