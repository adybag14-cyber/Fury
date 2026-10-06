#pragma once
#include <fury/character_animation.hpp>
#include <fury/character_profile.hpp>
#include <fury/npc.hpp>
#include <fury/scene.hpp>
#include <map>
#include <string>
namespace vaultline {
fury::CharacterRole
npc_character_role(const std::string &id,
                   fury::NpcKind kind = fury::NpcKind::Civilian);
struct NpcPresentationStats {
  std::size_t actors{}, near_triangles{}, far_triangles{}, pose_updates{},
      posed_vertices{}, deferred_updates{};
};
struct NpcMotionSample {
  fury::Vec3 position;
  float yaw{}, actual_speed{};
  double travel_distance{};
  float move_weight{}, turn_rate{}, crouch{};
  bool visible{true};
};
/// Presentation only: never changes agent paths, collision, names or mission
/// state.
class NpcPresentation {
public:
  explicit NpcPresentation(bool enabled = true, bool individual = true)
      : m_enabled(enabled), m_individual(individual) {}
  bool enabled() const { return m_enabled; }
  void install(fury::Scene &scene, fury::Entity &entity, float height,
               fury::CharacterRole role, const fury::Vec3 &legacy_color);
  void advance(const std::string &id, const NpcMotionSample &motion, float dt,
               const fury::Vec3 &camera, float lod_mid_distance);
  void talk(const std::string &id, const fury::Vec3 &listener,
            float seconds = 4.2f);
  NpcPresentationStats statistics() const;
  bool write_audit(const std::string &path) const;

private:
  struct Actor {
    fury::CharacterModel near_model, far_model;
    fury::Mesh *near_mesh{};
    fury::Mesh *far_mesh{};
    fury::CharacterAnimationState animation;
    fury::Vec3 listener{};
    double previous_travel{};
    float pose_elapsed{}, talk_remaining{};
    bool has_travel{}, was_visible{true}, individualized{};
    float lod_distance{};
    fury::Material profile_material;
  };
  bool m_enabled, m_individual;
  std::map<std::string, Actor> m_actors;
  std::size_t m_pose_updates{}, m_posed_vertices{}, m_deferred_updates{};
};
} // namespace vaultline
