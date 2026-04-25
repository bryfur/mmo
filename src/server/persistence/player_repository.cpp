#include "player_repository.hpp"

namespace mmo::server::persistence {

std::optional<PlayerSnapshot> PlayerRepository::load(const std::string& name) {
    PlayerSnapshot snap;

    {
        Statement q(db_, "SELECT player_class, level, xp, gold, "
                         "       pos_x, pos_y, pos_z, rotation, "
                         "       health, max_health, mana, max_mana, mana_regen, "
                         "       talent_points, last_seen, was_dead "
                         "FROM players WHERE name = ?;");
        q.bind_text(1, name);
        if (!q.step()) {
            return std::nullopt;
        }

        snap.name = name;
        snap.player_class = q.column_int(0);
        snap.level = q.column_int(1);
        snap.xp = q.column_int(2);
        snap.gold = q.column_int(3);
        snap.pos_x = static_cast<float>(q.column_double(4));
        snap.pos_y = static_cast<float>(q.column_double(5));
        snap.pos_z = static_cast<float>(q.column_double(6));
        snap.rotation = static_cast<float>(q.column_double(7));
        snap.health = static_cast<float>(q.column_double(8));
        snap.max_health = static_cast<float>(q.column_double(9));
        snap.mana = static_cast<float>(q.column_double(10));
        snap.max_mana = static_cast<float>(q.column_double(11));
        snap.mana_regen = static_cast<float>(q.column_double(12));
        snap.talent_points = q.column_int(13);
        snap.last_seen_unix = q.column_int64(14);
        snap.was_dead = q.column_int(15) != 0;
    }

    {
        Statement q(db_, "SELECT slot, item_id, count FROM player_inventory "
                         "WHERE name = ? ORDER BY slot;");
        q.bind_text(1, name);
        while (q.step()) {
            int slot = q.column_int(0);
            // Pad up to slot index so the resulting vector preserves slot
            // positions (server expects slots[i] to map to slot i).
            while (static_cast<int>(snap.inventory.size()) < slot) {
                snap.inventory.push_back({});
            }
            snap.inventory.push_back({q.column_text(1), q.column_int(2)});
        }
    }

    {
        Statement q(db_, "SELECT weapon_id, armor_id FROM player_equipment WHERE name = ?;");
        q.bind_text(1, name);
        if (q.step()) {
            snap.equipped_weapon = q.column_text(0);
            snap.equipped_armor = q.column_text(1);
        }
    }

    {
        Statement q(db_, "SELECT talent_id FROM player_talents WHERE name = ?;");
        q.bind_text(1, name);
        while (q.step()) {
            snap.unlocked_talents.push_back(q.column_text(0));
        }
    }

    {
        Statement q(db_, "SELECT quest_id FROM player_completed_quests WHERE name = ?;");
        q.bind_text(1, name);
        while (q.step()) {
            snap.completed_quests.push_back(q.column_text(0));
        }
    }

    {
        Statement q(db_, "SELECT quest_id, objectives_json FROM player_active_quests WHERE name = ?;");
        q.bind_text(1, name);
        while (q.step()) {
            snap.active_quests.push_back({q.column_text(0), q.column_text(1)});
        }
    }

    return snap;
}

void PlayerRepository::save(const PlayerSnapshot& snap) {
    db_.begin();
    try {
        {
            Statement q(db_, "INSERT OR REPLACE INTO players "
                             "(name, player_class, level, xp, gold, "
                             " pos_x, pos_y, pos_z, rotation, "
                             " health, max_health, mana, max_mana, mana_regen, "
                             " talent_points, last_seen, was_dead) "
                             "VALUES (?,?,?,?,?, ?,?,?,?, ?,?,?,?,?, ?,?,?);");
            q.bind_text(1, snap.name);
            q.bind_int(2, snap.player_class);
            q.bind_int(3, snap.level);
            q.bind_int(4, snap.xp);
            q.bind_int(5, snap.gold);
            q.bind_double(6, snap.pos_x);
            q.bind_double(7, snap.pos_y);
            q.bind_double(8, snap.pos_z);
            q.bind_double(9, snap.rotation);
            q.bind_double(10, snap.health);
            q.bind_double(11, snap.max_health);
            q.bind_double(12, snap.mana);
            q.bind_double(13, snap.max_mana);
            q.bind_double(14, snap.mana_regen);
            q.bind_int(15, snap.talent_points);
            q.bind_int64(16, snap.last_seen_unix);
            q.bind_int(17, snap.was_dead ? 1 : 0);
            q.step();
        }

        // Replace child collections wholesale: simpler than diffing, and
        // these tables are small (<= 20 rows per player).
        auto delete_for_name = [&](const char* table) {
            std::string sql = "DELETE FROM ";
            sql += table;
            sql += " WHERE name = ?;";
            Statement q(db_, sql);
            q.bind_text(1, snap.name);
            q.step();
        };
        delete_for_name("player_inventory");
        delete_for_name("player_equipment");
        delete_for_name("player_talents");
        delete_for_name("player_completed_quests");
        delete_for_name("player_active_quests");

        {
            Statement q(db_, "INSERT INTO player_inventory (name, slot, item_id, count) "
                             "VALUES (?,?,?,?);");
            for (size_t i = 0; i < snap.inventory.size(); ++i) {
                if (snap.inventory[i].item_id.empty()) {
                    continue;
                }
                q.reset();
                q.bind_text(1, snap.name);
                q.bind_int(2, static_cast<int>(i));
                q.bind_text(3, snap.inventory[i].item_id);
                q.bind_int(4, snap.inventory[i].count);
                q.step();
            }
        }

        {
            Statement q(db_, "INSERT INTO player_equipment (name, weapon_id, armor_id) "
                             "VALUES (?,?,?);");
            q.bind_text(1, snap.name);
            q.bind_text(2, snap.equipped_weapon);
            q.bind_text(3, snap.equipped_armor);
            q.step();
        }

        {
            Statement q(db_, "INSERT INTO player_talents (name, talent_id) VALUES (?,?);");
            for (const auto& tid : snap.unlocked_talents) {
                q.reset();
                q.bind_text(1, snap.name);
                q.bind_text(2, tid);
                q.step();
            }
        }

        {
            Statement q(db_, "INSERT INTO player_completed_quests (name, quest_id) VALUES (?,?);");
            for (const auto& qid : snap.completed_quests) {
                q.reset();
                q.bind_text(1, snap.name);
                q.bind_text(2, qid);
                q.step();
            }
        }

        {
            Statement q(db_, "INSERT INTO player_active_quests (name, quest_id, objectives_json) "
                             "VALUES (?,?,?);");
            for (const auto& aq : snap.active_quests) {
                q.reset();
                q.bind_text(1, snap.name);
                q.bind_text(2, aq.quest_id);
                q.bind_text(3, aq.objectives_json);
                q.step();
            }
        }

        db_.commit();
    } catch (...) {
        db_.rollback();
        throw;
    }
}

bool PlayerRepository::exists(const std::string& name) {
    Statement q(db_, "SELECT 1 FROM players WHERE name = ? LIMIT 1;");
    q.bind_text(1, name);
    return q.step();
}

} // namespace mmo::server::persistence
