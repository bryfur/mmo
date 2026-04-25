#include "server/systems/quest_system.hpp"
#include "server/ecs/game_components.hpp"
#include <entt/entt.hpp>
#include <filesystem>
#include <gtest/gtest.h>

using namespace mmo::server;
using namespace mmo::server::systems;

namespace {

std::string find_data_dir() {
    namespace fs = std::filesystem;
    fs::path cur = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = cur / "data";
        if (fs::exists(candidate / "classes.json")) {
            return candidate.string();
        }
        if (cur.has_parent_path()) {
            cur = cur.parent_path();
        } else {
            break;
        }
    }
    return "data";
}

entt::entity make_player(entt::registry& r) {
    auto e = r.create();
    r.emplace<ecs::PlayerLevel>(e);
    r.emplace<ecs::QuestState>(e);
    r.emplace<ecs::Transform>(e);
    r.emplace<ecs::Inventory>(e);
    r.emplace<ecs::EntityInfo>(e);
    return e;
}

// Manually inject an active quest with a single kill objective so we can
// test on_monster_killed without going through accept_quest (which requires
// a full GameConfig and matching NPC giver type).
void inject_kill_quest(entt::registry& r, entt::entity player, const std::string& quest_id, const std::string& type,
                       const std::string& target, int required) {
    auto& qs = r.get<ecs::QuestState>(player);
    ecs::ActiveQuest aq;
    aq.quest_id = quest_id;
    ecs::QuestObjectiveProgress obj;
    obj.type = type;
    obj.target = target;
    obj.current = 0;
    obj.required = required;
    obj.complete = false;
    aq.objectives.push_back(obj);
    qs.active_quests.push_back(std::move(aq));
}

} // namespace

// ============================================================================
// Kill objective basics
// ============================================================================

TEST(QuestSystem, KillObjectiveIncrementsAndCompletes) {
    entt::registry r;
    auto p = make_player(r);
    inject_kill_quest(r, p, "q1", "kill", "wolf", 3);

    GameConfig cfg;
    cfg.load(find_data_dir());

    auto changes = on_monster_killed(r, p, "wolf", cfg);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].current, 1);
    EXPECT_FALSE(changes[0].objective_complete);

    on_monster_killed(r, p, "wolf", cfg);
    auto changes2 = on_monster_killed(r, p, "wolf", cfg);
    ASSERT_GE(changes2.size(), 1u);
    // Third kill should close the objective and the quest.
    EXPECT_TRUE(r.get<ecs::QuestState>(p).active_quests[0].all_complete);
}

TEST(QuestSystem, KillObjectiveIgnoresWrongMonsterType) {
    entt::registry r;
    auto p = make_player(r);
    inject_kill_quest(r, p, "q1", "kill", "wolf", 3);

    GameConfig cfg;
    cfg.load(find_data_dir());

    auto changes = on_monster_killed(r, p, "goblin_scout", cfg);
    EXPECT_TRUE(changes.empty());
    EXPECT_EQ(r.get<ecs::QuestState>(p).active_quests[0].objectives[0].current, 0);
}

TEST(QuestSystem, KillObjectiveCountsAnyMonsterWhenTargetIsMonster) {
    entt::registry r;
    auto p = make_player(r);
    // "monster" or "npc_enemy" acts as a wildcard target.
    inject_kill_quest(r, p, "q1", "kill", "monster", 2);

    GameConfig cfg;
    cfg.load(find_data_dir());

    on_monster_killed(r, p, "goblin_scout", cfg);
    on_monster_killed(r, p, "forest_imp", cfg);
    EXPECT_TRUE(r.get<ecs::QuestState>(p).active_quests[0].all_complete);
}

// ============================================================================
// kill_in_area
// ============================================================================

TEST(QuestSystem, KillInAreaRequiresGameConfigObjectiveForFiltering) {
    // This test confirms the behavior when no matching QuestConfig exists:
    // the kill_in_area filter is skipped and the kill is counted. We use a
    // synthetic quest id that isn't in quests.json.
    entt::registry r;
    auto p = make_player(r);
    inject_kill_quest(r, p, "synthetic_test_quest", "kill_in_area", "npc_enemy", 1);

    GameConfig cfg;
    cfg.load(find_data_dir());

    auto changes = on_monster_killed(r, p, "goblin_scout", cfg, 0.0f, 0.0f);
    // With no matching quest config, we can't apply the area filter, and
    // on_monster_killed should simply skip the area check. Behavior is: kill
    // counts and objective advances.
    ASSERT_FALSE(changes.empty());
    EXPECT_EQ(changes[0].current, 1);
}
