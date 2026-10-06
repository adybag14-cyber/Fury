#include "../apps/vaultline/npc_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
bool count_allocations = false;
std::size_t allocations = 0;
}
#if defined(_MSC_VER)
# define TEST_NOINLINE __declspec(noinline)
#else
# define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(std::size_t size) {
  if (count_allocations) ++allocations;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
TEST_NOINLINE void* operator new[](std::size_t size) { return ::operator new(size); }
TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
TEST_NOINLINE void operator delete[](void* p) noexcept { std::free(p); }
TEST_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
TEST_NOINLINE void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using namespace fury;
using vaultline::NpcMotionSample;
using vaultline::NpcPresentation;
int checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
bool same(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool same_mesh(const Mesh& a, const Mesh& b, float tolerance = 0.f) {
  if (a.indices != b.indices || a.vertices.size() != b.vertices.size()) return false;
  for (std::size_t i = 0; i < a.vertices.size(); ++i) {
    const auto& x = a.vertices[i];
    const auto& y = b.vertices[i];
    if (length(x.position - y.position) > tolerance || length(x.normal - y.normal) > tolerance ||
        !same(x.color, y.color) || x.uv.x != y.uv.x || x.uv.y != y.uv.y ||
        x.opacity != y.opacity) return false;
  }
  return true;
}
float max_displacement(const Mesh& a, const Mesh& b, float min_y = -100.f) {
  require(a.vertices.size() == b.vertices.size(), "Compared actors must have matching topology");
  float result = 0.f;
  for (std::size_t i = 0; i < a.vertices.size(); ++i)
    if (a.vertices[i].position.y >= min_y)
      result = std::max(result, length(a.vertices[i].position - b.vertices[i].position));
  return result;
}
void finite_mesh(const Mesh& mesh) {
  for (const auto& v : mesh.vertices) {
    require(std::isfinite(v.position.x) && std::isfinite(v.position.y) &&
                std::isfinite(v.position.z) && std::isfinite(v.normal.x) &&
                std::isfinite(v.normal.y) && std::isfinite(v.normal.z),
            "Wrapper must never submit nonfinite skinned vertices or normals");
  }
}
void same_gameplay(const Entity& a, const Entity& b) {
  require(a.name == b.name && a.tag == b.tag, "Presentation preserves actor name and gameplay tag");
  require(same(a.transform.position, b.transform.position) &&
              same(a.transform.rotation_euler, b.transform.rotation_euler) &&
              same(a.transform.scale, b.transform.scale),
          "Presentation never writes authoritative entity transforms");
  require(a.solid == b.solid && same(a.collider.center, b.collider.center) &&
              same(a.collider.half_extents, b.collider.half_extents),
          "Presentation never changes collision or solidity");
  require(a.visible == b.visible && a.detail == b.detail,
          "Presentation leaves schedule/mission visibility and detail policy intact");
}
void same_material(const Material& a, const Material& b) {
  require(same(a.albedo, b.albedo) && a.metallic == b.metallic &&
              a.roughness == b.roughness && a.emissive == b.emissive &&
              a.texture == b.texture && a.detail_texture == b.detail_texture &&
              a.detail_rotation == b.detail_rotation && a.detail_use_mesh_uvs == b.detail_use_mesh_uvs &&
              a.world_uv_scale == b.world_uv_scale && a.uv_scroll_u == b.uv_scroll_u &&
              a.uv_scroll_v == b.uv_scroll_v && a.wetness == b.wetness &&
              a.transmission == b.transmission && a.index_of_refraction == b.index_of_refraction &&
              a.opacity == b.opacity && a.alpha_cutoff == b.alpha_cutoff &&
              a.normal_scale == b.normal_scale && a.double_sided == b.double_sided &&
              a.alpha_blend == b.alpha_blend && same(a.emissive_color, b.emissive_color) &&
              a.textures == b.textures, "Disabled presentation preserves every legacy material field");
}
Mesh legacy_mesh() {
  Mesh mesh;
  mesh.vertices = {{{-.2f, -.8f, 0.f}}, {{.2f, -.8f, 0.f}}, {{0.f, .8f, 0.f}}};
  mesh.indices = {0, 1, 2};
  return mesh;
}
Entity make_entity(const std::string& name = "NpcPresentationFixture") {
  Entity e;
  e.name = name;
  e.tag = "mission_fixture";
  e.transform.position = {13.f, .875f, -8.f};
  e.transform.rotation_euler = {.01f, .6f, -.01f};
  e.transform.scale = {1.1f, 1.f, .95f};
  e.solid = true;
  e.collider = {{.1f, -.2f, .3f}, {.4f, .875f, .4f}};
  e.detail = true;
  e.material.albedo = {.15f, .45f, .65f};
  e.material.metallic = .1f;
  e.material.roughness = .6f;
  e.material.texture = TextureSlot::Checker;
  e.material.detail_texture = TextureSlot::Concrete;
  e.material.detail_rotation = 2;
  e.material.detail_use_mesh_uvs = true;
  e.material.world_uv_scale = 2.f;
  e.material.uv_scroll_u = .3f;
  e.material.uv_scroll_v = .1f;
  e.material.wetness = .2f;
  e.material.transmission = .1f;
  e.material.index_of_refraction = 1.45f;
  e.material.opacity = .9f;
  e.material.alpha_cutoff = .2f;
  e.material.normal_scale = .8f;
  e.material.double_sided = false;
  e.material.alpha_blend = true;
  e.material.emissive = .05f;
  e.material.emissive_color = {.2f, .4f, .7f};
  return e;
}
struct Fixture {
  Scene scene;
  NpcPresentation presentation;
  Entity* entity{};
  NpcMotionSample sample;
  explicit Fixture(bool enabled = true, const std::string& id = "NpcPresentationFixture")
      : presentation(enabled) {
    scene.entities().reserve(4);
    Entity e = make_entity(id);
    e.mesh = scene.add_mesh(legacy_mesh());
    e.lod_mesh = scene.add_mesh(legacy_mesh());
    entity = &scene.add_entity(std::move(e));
    presentation.install(scene, *entity, 1.75f, CharacterRole::Commuter, {.1f, .2f, .3f});
    sample.position = entity->transform.position;
    sample.yaw = entity->transform.rotation_euler.y;
  }
  void tick(float dt = 1.f / 60.f, float camera_distance = 5.f, float lod = 30.f) {
    presentation.advance(entity->name, sample, dt, sample.position + Vec3{camera_distance, 0, 0}, lod);
  }
};
void test_role_mapping() {
  using R = CharacterRole;
  using K = NpcKind;
  const auto role = vaultline::npc_character_role;
  require(role("PlayerBody", K::Civilian) == R::Player &&
              role("GhostLoop", K::Civilian) == R::Ghost &&
              role("CrewRook", K::Civilian) == R::CrewTech &&
              role("CrewSparrow", K::Civilian) == R::CrewScout, "Player, ghost and crew keep explicit visual roles");
  require(role("NpcGuard", K::Guard) == R::Security && role("NpcFence", K::Fence) == R::Fence &&
              role("NpcEnforcer", K::Enforcer) == R::Enforcer, "Gameplay kinds map to security/fence/enforcer wardrobe");
  require(role("NpcCivD", K::Civilian) == R::Dock && role("NpcCivE", K::Civilian) == R::Dock &&
              role("NpcCivB", K::Civilian) == R::Market && role("NpcCivAsh", K::Civilian) == R::Market &&
              role("NpcTeller", K::Civilian) == R::BankStaff &&
              role("NpcCivA", K::Civilian) == R::Commuter, "Authored civilian IDs map to district wardrobes");
}
void test_disabled_preserves_legacy() {
  Scene scene;
  Entity e = make_entity();
  e.visible = false;
  e.mesh = scene.add_mesh(legacy_mesh());
  e.lod_mesh = scene.add_mesh(legacy_mesh());
  const Entity before = e;
  const Mesh near = *e.mesh, far = *e.lod_mesh;
  NpcPresentation disabled(false);
  disabled.install(scene, e, 1.75f, CharacterRole::Commuter, {.9f, .8f, .7f});
  disabled.talk(e.name, {1, 2, 3});
  for (int i = 0; i < 100; ++i) disabled.advance(e.name, {}, .1f, {}, 30.f);
  same_gameplay(before, e);
  same_material(before.material, e.material);
  require(e.mesh == before.mesh && e.lod_mesh == before.lod_mesh && scene.meshes().size() == 2,
          "Disabled install retains legacy mesh pointers and scene allocations");
  require(same_mesh(near, *e.mesh) && same_mesh(far, *e.lod_mesh), "Disabled update never deforms legacy geometry");
  const auto stats = disabled.statistics();
  require(!disabled.enabled() && stats.actors == 0 && stats.pose_updates == 0 &&
              stats.posed_vertices == 0 && stats.deferred_updates == 0,
          "Disabled presentation incurs no registered actor or pose work");
}
void test_gameplay_and_respawn_contracts() {
  Fixture f;
  same_gameplay(make_entity(), *f.entity);
  Entity sentinel = make_entity("VaultDoor");
  sentinel.tag = "vault";
  sentinel.visible = false;
  f.scene.add_entity(sentinel);
  const Entity original = *f.entity;
  const Mesh initial_near = *f.entity->mesh, initial_far = *f.entity->lod_mesh;
  const auto solids = f.scene.collect_solids();
  const auto near_id = f.entity->mesh->geometry_identity, far_id = f.entity->lod_mesh->geometry_identity;
  const auto* near_data = f.entity->mesh->vertices.data();
  const auto* far_data = f.entity->lod_mesh->vertices.data();
  const auto* near_indices = f.entity->mesh->indices.data();
  const auto* far_indices = f.entity->lod_mesh->indices.data();
  const auto near_capacity = f.entity->mesh->vertices.capacity(), far_capacity = f.entity->lod_mesh->vertices.capacity();
  f.entity->mesh->gpu_vao = 11; f.entity->mesh->gpu_vbo = 12; f.entity->mesh->gpu_ibo = 13;
  f.entity->lod_mesh->gpu_vao = 21; f.entity->lod_mesh->gpu_vbo = 22; f.entity->lod_mesh->gpu_ibo = 23;
  f.entity->mesh->gpu_uploaded = f.entity->lod_mesh->gpu_uploaded = true;
  f.presentation.talk(f.entity->name, f.sample.position + Vec3{3, 1, 2});
  f.sample.actual_speed = 2.f; f.sample.move_weight = 1.f;
  allocations = 0; count_allocations = true;
  for (int frame = 0; frame < 360; ++frame) {
    f.sample.travel_distance += 2.0 / 60.0;
    f.tick(1.f / 60.f, float(frame % 80));
  }
  count_allocations = false;
  require(allocations == 0, "Wrapper hot path performs zero heap allocations");
  require(max_displacement(initial_near, *f.entity->mesh) > .02f, "Moving wrapper genuinely changes posed vertices");
  require(f.scene.meshes().size() == 4 && f.presentation.statistics().actors == 1,
          "One registered actor creates exactly two detail meshes");
  for (int respawn = 0; respawn < 20; ++respawn) {
    Entity replacement = original;
    replacement.mesh = nullptr; replacement.lod_mesh = nullptr;
    replacement.transform.position.x += float(respawn);
    allocations = 0; count_allocations = true;
    f.presentation.install(f.scene, replacement, 1.75f, CharacterRole::Commuter, {1, 0, 0});
    count_allocations = false;
    require(allocations == 0, "Registered actor respawn reuses allocations");
    require(replacement.mesh == original.mesh && replacement.lod_mesh == original.lod_mesh,
            "Respawn reuses stable renderer mesh pointers");
    require(same_mesh(initial_near, *replacement.mesh, 2e-6f) && same_mesh(initial_far, *replacement.lod_mesh, 2e-6f),
            "Respawn restores deterministic identity and seed-specific idle pose");
    require(f.scene.meshes().size() == 4 && f.presentation.statistics().actors == 1,
            "Repeated respawn cannot add geometry or duplicate presentation actors");
  }
  require(f.entity->mesh->geometry_identity == near_id && f.entity->lod_mesh->geometry_identity == far_id &&
              f.entity->mesh->vertices.data() == near_data && f.entity->lod_mesh->vertices.data() == far_data &&
              f.entity->mesh->indices.data() == near_indices && f.entity->lod_mesh->indices.data() == far_indices &&
              f.entity->mesh->vertices.capacity() == near_capacity && f.entity->lod_mesh->vertices.capacity() == far_capacity,
          "Animation and respawn preserve geometry identity, storage and topology");
  require(f.entity->mesh->gpu_vao == 11 && f.entity->mesh->gpu_vbo == 12 && f.entity->mesh->gpu_ibo == 13 &&
              f.entity->lod_mesh->gpu_vao == 21 && f.entity->lod_mesh->gpu_vbo == 22 && f.entity->lod_mesh->gpu_ibo == 23 &&
              f.entity->mesh->gpu_uploaded && f.entity->lod_mesh->gpu_uploaded,
          "Animation and respawn preserve renderer handles and upload ownership");
  same_gameplay(original, *f.entity);
  same_gameplay(sentinel, *f.scene.find_by_name("VaultDoor"));
  const auto after_solids = f.scene.collect_solids();
  require(solids.size() == after_solids.size(), "Presentation leaves collision population untouched");
  for (std::size_t i = 0; i < solids.size(); ++i)
    require(same(solids[i].center, after_solids[i].center) && same(solids[i].half_extents, after_solids[i].half_extents),
            "Presentation leaves collected gameplay AABBs untouched");
  require(same(f.entity->material.albedo, {1, 1, 1}) && f.entity->material.metallic == 0 &&
              std::fabs(f.entity->material.roughness - .72f) < 1e-6f &&
              f.entity->material.texture == TextureSlot::None && f.entity->material.detail_texture == TextureSlot::None,
          "Detailed actor uses neutral material so skin, hair and clothing retain vertex palette");
  for (float height : {1.7f, 0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    bool rejected = false;
    try { f.presentation.install(f.scene, *f.entity, height, CharacterRole::Commuter, {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Registered actor rejects changed or invalid height");
  }
  bool rejected = false;
  try { f.presentation.install(f.scene, *f.entity, 1.75f, CharacterRole::Security, {}); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "Registered actor cannot silently change role");
}
void test_motion_talk_and_determinism() {
  Fixture idle, moving, talking, replica;
  const Mesh bind_idle = *idle.entity->mesh;
  moving.sample.actual_speed = 2.f; moving.sample.move_weight = 1.f;
  talking.presentation.talk(talking.entity->name, talking.sample.position + Vec3{3, 1, 3}, 1.f);
  for (int i = 0; i < 45; ++i) {
    moving.sample.travel_distance += 2.0 / 60.0;
    idle.tick(); moving.tick(); talking.tick(); replica.tick();
  }
  require(max_displacement(bind_idle, *idle.entity->mesh) > .001f, "Idle has nonzero breathing and weight shift");
  require(max_displacement(*idle.entity->mesh, *moving.entity->mesh) > .05f,
          "Measured travel selects a visibly distinct locomotion pose");
  require(max_displacement(*idle.entity->mesh, *talking.entity->mesh, .2f) > .015f,
          "Conversation changes upper-body attention and gestures");
  require(same_mesh(*idle.entity->mesh, *replica.entity->mesh), "Same stable ID and input history reproduce exact mesh output");
  for (int i = 0; i < 600; ++i) { idle.tick(); talking.tick(); }
  require(max_displacement(*idle.entity->mesh, *talking.entity->mesh) < .0001f,
          "Expired conversation smoothly returns to the deterministic idle pose");
  Fixture other(true, "DifferentPersistentActor");
  require(other.entity->mesh->geometry_identity != idle.entity->mesh->geometry_identity &&
              other.entity->mesh != idle.entity->mesh, "Different stable actors never share mutable geometry");
  Fixture frozen, speed_only;
  speed_only.sample.actual_speed = 5.f; speed_only.sample.move_weight = 0.f;
  for (int i = 0; i < 60; ++i) { frozen.tick(); speed_only.tick(); }
  require(same_mesh(*frozen.entity->mesh, *speed_only.entity->mesh),
          "Requested speed without travel or move weight cannot create a walk cycle");
}
void test_lod_cadence_and_visibility() {
  Fixture near, mid, far;
  const auto n0 = near.entity->mesh->geometry_revision, nf0 = near.entity->lod_mesh->geometry_revision;
  const auto m0 = mid.entity->mesh->geometry_revision, mf0 = mid.entity->lod_mesh->geometry_revision;
  const auto f0 = far.entity->mesh->geometry_revision, ff0 = far.entity->lod_mesh->geometry_revision;
  for (int i = 0; i < 120; ++i) { near.tick(); mid.tick(1.f / 60.f, 30.f); far.tick(1.f / 60.f, 60.f); }
  require(near.entity->mesh->geometry_revision - n0 == 60 && near.entity->lod_mesh->geometry_revision == nf0,
          "Near cadence skins only high detail at 30 Hz");
  require(mid.entity->mesh->geometry_revision - m0 == 30 && mid.entity->lod_mesh->geometry_revision - mf0 == 30,
          "LOD overlap skins both persistent meshes at 15 Hz");
  require(far.entity->mesh->geometry_revision == f0 && far.entity->lod_mesh->geometry_revision - ff0 == 15,
          "Far cadence skins only proxy at bounded approximately 8 Hz");
  const auto stats = near.presentation.statistics();
  require(stats.pose_updates == 60 && stats.deferred_updates == 60 &&
              stats.posed_vertices == 60 * near.entity->mesh->vertices.size() &&
              stats.near_triangles == near.entity->mesh->indices.size() / 3 &&
              stats.far_triangles == near.entity->lod_mesh->indices.size() / 3,
          "Audit statistics report actual pose, vertex, deferred and geometry work");
  Fixture hidden, visible;
  hidden.sample.visible = false;
  const auto hidden_rev = hidden.entity->mesh->geometry_revision;
  for (int i = 0; i < 60; ++i) { hidden.tick(); visible.tick(); }
  require(hidden.entity->mesh->geometry_revision == hidden_rev && hidden.presentation.statistics().pose_updates == 0,
          "Hidden actor advances animation without CPU skinning");
  hidden.sample.visible = true;
  hidden.tick(1.f / 120.f); visible.tick(1.f / 120.f);
  require(hidden.entity->mesh->geometry_revision == hidden_rev + 1,
          "Reappearing actor immediately refreshes its current pose below normal interval");
  visible.tick(.25f); hidden.tick(.25f);
  require(same_mesh(*hidden.entity->mesh, *visible.entity->mesh, 2e-6f),
          "Hidden actor's animation clock remains continuous when it reappears");
  Fixture no_lod;
  const auto proxy_revision = no_lod.entity->lod_mesh->geometry_revision;
  for (int i = 0; i < 30; ++i) no_lod.tick(.1f, 90.f, 0.f);
  require(no_lod.entity->mesh->geometry_revision > 1 && no_lod.entity->lod_mesh->geometry_revision == proxy_revision,
          "Disabled renderer LOD always updates near mesh even far from camera");
}
void test_invalid_lod_policy() {
  Fixture checked, disabled;
  for (float threshold : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
    checked.tick(.25f, 60.f, threshold);
    disabled.tick(.25f, 60.f, 0.f);
  }
  require(same_mesh(*checked.entity->mesh, *disabled.entity->mesh) &&
              checked.presentation.statistics().pose_updates == 3,
          "Nonfinite LOD threshold safely falls back to continuously posed near geometry");
}
void test_invalid_samples_and_recovery() {
  const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
  Fixture reference, checked;
  const Mesh original = *checked.entity->mesh;
  for (float dt : {0.f, -1.f, nan, inf}) checked.tick(dt);
  for (int input = 0; input < 7; ++input) {
    NpcMotionSample bad = checked.sample;
    Vec3 camera = bad.position;
    if (input == 0) bad.position.x = nan;
    if (input == 1) bad.position.z = inf;
    if (input == 2) bad.yaw = inf;
    if (input == 3) bad.yaw = nan;
    if (input == 4) bad.travel_distance = nan;
    if (input == 5) bad.travel_distance = std::numeric_limits<double>::infinity();
    if (input == 6) camera.y = nan;
    checked.presentation.advance(checked.entity->name, bad, .25f, camera, 30.f);
  }
  checked.presentation.advance("unknown_actor", {}, .25f, {}, 30.f);
  checked.presentation.talk("unknown_actor", {}, 1.f);
  checked.presentation.talk(checked.entity->name, {nan, 0, 0}, 1.f);
  checked.presentation.talk(checked.entity->name, {}, inf);
  require(same_mesh(original, *checked.entity->mesh) && checked.presentation.statistics().pose_updates == 0 &&
              checked.presentation.statistics().deferred_updates == 0,
          "Nonpositive/nonfinite dt, malformed spatial samples and unknown IDs have no side effects");
  for (int i = 0; i < 60; ++i) { reference.tick(); checked.tick(); }
  require(same_mesh(*reference.entity->mesh, *checked.entity->mesh), "Ignored malformed input does not corrupt later valid animation");
  checked.presentation.talk(checked.entity->name, checked.sample.position + Vec3{3, 1, 2}, 1.f);
  checked.sample.actual_speed = inf; checked.sample.move_weight = nan;
  checked.sample.turn_rate = nan; checked.sample.crouch = inf;
  checked.sample.yaw = std::numeric_limits<float>::max();
  checked.sample.travel_distance = 1e200;
  for (int i = 0; i < 10; ++i) checked.tick(.25f);
  finite_mesh(*checked.entity->mesh); finite_mesh(*checked.entity->lod_mesh);
  checked.sample = reference.sample;
  checked.sample.actual_speed = 2.f; checked.sample.move_weight = 1.f;
  for (int i = 0; i < 90; ++i) { checked.sample.travel_distance += 1.0 / 30.0; checked.tick(); }
  finite_mesh(*checked.entity->mesh);
  Fixture hitch, capped;
  hitch.tick(5.f); capped.tick(.25f);
  require(same_mesh(*hitch.entity->mesh, *capped.entity->mesh), "Long update hitch is capped to the same .25-second animation step");
}
void test_audit_output() {
  Fixture f(true, "Fixture\"With\\Escape");
  for (int i = 0; i < 10; ++i) f.tick();
  const auto path = std::filesystem::temp_directory_path() /
      ("fury-npc-presentation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
  require(f.presentation.write_audit(path.string()), "Audit writes successfully to a writable path");
  std::ifstream in(path);
  std::stringstream contents; contents << in.rdbuf();
  const auto json = contents.str();
  require(json.find("\"enabled\":true") != std::string::npos && json.find("\"actors\":1") != std::string::npos &&
              json.find("\"seed\":" + std::to_string(character_seed(f.entity->name))) != std::string::npos &&
              json.find("Fixture\\\"With\\\\Escape") != std::string::npos,
          "Audit includes actor count, stable identity seed and correctly escaped ID");
  in.close(); std::filesystem::remove(path);
  require(!f.presentation.write_audit((path / "missing" / "audit.json").string()),
          "Audit reports unwritable destination instead of claiming success");
}
}  // namespace
int main() {
  try {
    test_role_mapping();
    test_disabled_preserves_legacy();
    test_gameplay_and_respawn_contracts();
    test_motion_talk_and_determinism();
    test_lod_cadence_and_visibility();
    test_invalid_lod_policy();
    test_invalid_samples_and_recovery();
    test_audit_output();
    std::cout << "npc_presentation: " << checks << " checks passed; cached identity/respawn, zero-allocation update, "
              << "legacy parity, gameplay isolation, idle/move/talk, LOD, visibility and finite guards\n";
    return 0;
  } catch (const std::exception& error) {
    count_allocations = false;
    std::cerr << "npc_presentation FAILED after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
