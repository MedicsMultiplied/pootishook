#include "Settings.hpp"
#include "init.hpp"
#include "HookTools.hpp"
#include "interfaces.hpp"
#include "navparser.hpp"
#include "playerresource.h"
#include "localplayer.hpp"
#include "sdk.hpp"
#include "entitycache.hpp"
#include "CaptureLogic.hpp"
#include "PlayerTools.hpp"
#include "Aimbot.hpp"
#include "navparser.hpp"
#include "MiscAimbot.hpp"
#include "Misc.hpp"
#include "conditions.hpp"
#include "itemtypes.hpp"

namespace hacks::tf2::NavBot
{
#define TEAM_NEUTRAL 0  // Add at top with other defines

static settings::Boolean enabled("navbot.enabled", "false");
static settings::Boolean search_health("navbot.search-health", "true");
static settings::Boolean search_ammo("navbot.search-ammo", "true");
static settings::Boolean search_spells("navbot.search-spells", "true");
static settings::Boolean search_powerups("navbot.search-powerups", "true");
static settings::Boolean search_gargoyles("navbot.search-gargoyles", "true");
static settings::Boolean stay_near("navbot.stay-near", "true");
static settings::Boolean capture_objectives("navbot.capture-objectives", "true");
static settings::Boolean snipe_sentries("navbot.snipe-sentries", "true");
static settings::Boolean snipe_sentries_shortrange("navbot.snipe-sentries.shortrange", "false");
static settings::Boolean escape_danger("navbot.escape-danger", "true");
static settings::Boolean yolo_mode("navbot.yolo-mode", "false");
static settings::Boolean yolo_mode_rage("navbot.yolo-mode-rage", "false");
static settings::Boolean escape_danger_ctf_cap("navbot.escape-danger.ctf-cap", "false");
static settings::Boolean enable_slight_danger_when_capping("navbot.escape-danger.slight-danger.capping", "false");
static settings::Boolean autojump("navbot.autojump.enabled", "false");
static settings::Boolean primary_only("navbot.primary-only", "true");
static settings::Boolean defend_during_roam("navbot.defend-while-roaming", "true");
static settings::Int force_slot("navbot.force-slot", "0");
static settings::Float jump_distance("navbot.autojump.trigger-distance", "300");
static settings::Int blacklist_delay("navbot.proximity-blacklist.delay", "500");
static settings::Boolean blacklist_dormat("navbot.proximity-blacklist.dormant", "false");
static settings::Int blacklist_delay_dormat("navbot.proximity-blacklist.delay-dormant", "1000");
static settings::Int blacklist_slightdanger_limit("navbot.proximity-blacklist.slight-danger.amount", "2");
static settings::Boolean engie_mode("navbot.engineer-mode", "false");
static settings::Boolean avoid_stickies("navbot.avoid-stickies", "true");
static settings::Boolean avoid_projectiles("navbot.avoid-projectiles", "true");
static settings::Float sticky_danger_range("navbot.sticky-danger-range", "600");
static settings::Float projectile_danger_range("navbot.projectile-danger-range", "600");
static settings::Float smoothness("navbot.smoothness", "90");
#if ENABLE_VISUALS
static settings::Boolean draw_danger("navbot.draw-danger", "false");
#endif

// Allow for custom danger configs, mainly for debugging purposes
static settings::Boolean danger_config_custom("navbot.danger-config.enabled", "false");
static settings::Boolean danger_config_custom_prefer_far("navbot.danger-config.perfer_far", "true");
static settings::Float danger_config_custom_min_full_danger("navbot.danger-config.min_full_danger", "300");
static settings::Float danger_config_custom_min_slight_danger("navbot.danger-config.min_slight_danger", "500");
static settings::Float danger_config_custom_max_slight_danger("navbot.danger-config.max_slight_danger", "3000");

// Controls the bot parameters like distance from enemy
struct bot_class_config
{
    float min_full_danger;
    float min_slight_danger;
    float max;
    bool prefer_far;
};

constexpr bot_class_config CONFIG_SHORT_RANGE         = { 140.0f, 400.0f, 600.0f, false };
constexpr bot_class_config CONFIG_MID_RANGE           = { 200.0f, 500.0f, 3000.0f, true };
constexpr bot_class_config CONFIG_LONG_RANGE          = { 300.0f, 500.0f, 4000.0f, true };
constexpr bot_class_config CONFIG_ENGINEER            = { 200.0f, 500.0f, 3000.0f, false };
constexpr bot_class_config CONFIG_GUNSLINGER_ENGINEER = { 50.0f, 300.0f, 2000.0f, false };
bot_class_config selected_config                      = CONFIG_MID_RANGE;

static Timer health_cooldown{};
static Timer ammo_cooldown{};
static Timer spell_cooldown{};
static Timer powerup_cooldown{};
static Timer gargoyle_cooldown{};
// Should we search health at all?
bool shouldSearchHealth(bool low_priority = false)
{
    if (!search_health)
        return false;
    // Check if being gradually healed in any way
    if (HasCondition<TFCond_Healing>(LOCAL_E))
        return false;

    // Priority too high
    if (navparser::NavEngine::current_priority > health)
        return false;
    float health_percent = LOCAL_E->m_iHealth() / (float) g_pPlayerResource->GetMaxHealth(LOCAL_E);
    // Get health when below 65%, or below 80% and just patroling
    return health_percent < 0.64f || (low_priority && (navparser::NavEngine::current_priority <= patrol || navparser::NavEngine::current_priority == lowprio_health) && health_percent <= 0.80f);
}

// Should we search ammo at all?
bool shouldSearchAmmo()
{
    if (!search_ammo)
        return false;
    if (CE_BAD(LOCAL_W))
        return false;
    // Priority too high
    if (navparser::NavEngine::current_priority > ammo)
        return false;

    int *weapon_list = (int *) ((uint64_t) (RAW_ENT(LOCAL_E)) + netvar.hMyWeapons);
    if (!weapon_list)
        return false;
    if (g_pLocalPlayer->holding_sniper_rifle && CE_INT(LOCAL_E, netvar.m_iAmmo + 4) <= 5)
        return true;
    for (int i = 0; weapon_list[i]; ++i)
    {
        int handle = weapon_list[i];
        int eid    = HandleToIDX(handle);
        if (eid > MAX_PLAYERS && eid <= HIGHEST_ENTITY)
        {
            IClientEntity *weapon = g_IEntityList->GetClientEntity(eid);
            if (weapon and re::C_BaseCombatWeapon::IsBaseCombatWeapon(weapon) && re::C_TFWeaponBase::UsesPrimaryAmmo(weapon) && !re::C_TFWeaponBase::HasPrimaryAmmo(weapon))
                return true;
        }
    }
    return false;
}

// Should we search spells at all?
bool shouldSearchSpells()
{
    if (!*enabled)
        return false;
    if (navparser::NavEngine::current_priority > Priority_list::spells)
        return false;
    if (HasCondition<TFCond_HalloweenKart>(LOCAL_E) || HasCondition<TFCond_HalloweenKartDash>(LOCAL_E))
        return false;
    return true;
}

// Should we search powerups at all?
bool shouldSearchPowerups()
{
    if (!*enabled)
        return false;
    if (navparser::NavEngine::current_priority > Priority_list::prio_powerups)
        return false;
    return true;
}

// Should we search gargoyles at all?
bool shouldSearchGargoyles()
{
    if (!*enabled)
        return false;
    if (navparser::NavEngine::current_priority > Priority_list::gargoyles)
        return false;
    return true;
}

// Get Valid Dispensers (Used for health/ammo)
std::vector<CachedEntity *> getDispensers()
{
    std::vector<CachedEntity *> entities;
    for (auto const &ent : entity_cache::valid_ents)
    {
        if (ent->m_iClassID() != CL_CLASS(CObjectDispenser) || ent->m_iTeam() != g_pLocalPlayer->team)
            continue;
        if (CE_BYTE(ent, netvar.m_bCarryDeploy) || CE_BYTE(ent, netvar.m_bHasSapper) || CE_BYTE(ent, netvar.m_bBuilding))
            continue;

        // This fixes the fact that players can just place dispensers in unreachable locations
        auto local_nav = navparser::NavEngine::findClosestNavSquare(ent->m_vecOrigin());
        if (local_nav->getNearestPoint(ent->m_vecOrigin().AsVector2D()).DistTo(ent->m_vecOrigin()) > 300.0f || local_nav->getNearestPoint(ent->m_vecOrigin().AsVector2D()).z - ent->m_vecOrigin().z > navparser::PLAYER_JUMP_HEIGHT)
            continue;
        entities.push_back(ent);
    }
    // Sort by distance, closer is better
    std::sort(entities.begin(), entities.end(), [](CachedEntity *a, CachedEntity *b) { return a->m_flDistance() < b->m_flDistance(); });
    return entities;
}

// Get entities of given itemtypes (Used for health/ammo)
std::vector<CachedEntity *> getEntities(const std::vector<k_EItemType> &itemtypes)
{
    std::vector<CachedEntity *> entities;
    for (auto const &ent : entity_cache::valid_ents)
    {
        for (auto &itemtype : itemtypes)
        {
            if (ent->m_ItemType() == itemtype)
            {
                entities.push_back(ent);
                break;
            }
        }
    }
    // Sort by distance, closer is better
    std::sort(entities.begin(), entities.end(), [](CachedEntity *a, CachedEntity *b) { return a->m_flDistance() < b->m_flDistance(); });
    return entities;
}

// Find health if needed
bool getHealth(bool low_priority = false)
{
    Priority_list priority = low_priority ? lowprio_health : health;
    if (!health_cooldown.check(1000))
        return navparser::NavEngine::current_priority == priority;
    if (shouldSearchHealth(low_priority))
    {
        // Already pathing, only try to repath every 2s
        if (navparser::NavEngine::current_priority == priority)
        {
            static Timer repath_timer;
            if (!repath_timer.test_and_set(2000))
                return true;
        }
        auto healthpacks = getEntities({ ITEM_HEALTH_SMALL, ITEM_HEALTH_MEDIUM, ITEM_HEALTH_LARGE });
        auto dispensers  = getDispensers();

        auto total_ents = healthpacks;

        // Add dispensers and sort list again
        if (!dispensers.empty())
        {
            total_ents.reserve(healthpacks.size() + dispensers.size());
            total_ents.insert(total_ents.end(), dispensers.begin(), dispensers.end());
            std::sort(total_ents.begin(), total_ents.end(), [](CachedEntity *a, CachedEntity *b) { return a->m_flDistance() < b->m_flDistance(); });
        }

        for (auto healthpack : total_ents)
        {
            // If we succeeed, don't try to path to other packs
            if (navparser::NavEngine::navTo(healthpack->m_vecOrigin(), priority, true, healthpack->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) > 200.0f * 200.0f))
            {
                // Check if we are close enough to the health pack to pick it up
                if (healthpack->m_vecOrigin().DistTo(g_pLocalPlayer->v_Origin) < 75.0f)
                {
                    // Try to touch the health pack
                    auto local_nav = navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin);
                    if (local_nav)
                    {
                        Vector path_point = local_nav->getNearestPoint(healthpack->m_vecOrigin().AsVector2D());
                        path_point.z = healthpack->m_vecOrigin().z;
                        
                        // Walk towards the health pack
                        WalkTo(path_point);
                    }
                }
                return true;
            }
        }
        health_cooldown.update();
    }
    else if (navparser::NavEngine::current_priority == priority)
        navparser::NavEngine::cancelPath();
    return false;
}

static bool was_force = false;
// Find ammo if needed
bool getAmmo(bool force = false)
{
    if (!force && !ammo_cooldown.check(1000))
        return navparser::NavEngine::current_priority == ammo;
    if (force || shouldSearchAmmo())
    {
        // Already pathing, only try to repath every 2s
        if (navparser::NavEngine::current_priority == ammo)
        {
            static Timer repath_timer;
            if (!repath_timer.test_and_set(2000))
                return true;
        }
        else
            was_force = false;
        auto ammopacks  = getEntities({ ITEM_AMMO_SMALL, ITEM_AMMO_MEDIUM, ITEM_AMMO_LARGE });
        auto dispensers = getDispensers();

        auto total_ents = ammopacks;

        // Add dispensers and sort list again
        if (!dispensers.empty())
        {
            total_ents.reserve(ammopacks.size() + dispensers.size());
            total_ents.insert(total_ents.end(), dispensers.begin(), dispensers.end());
            std::sort(total_ents.begin(), total_ents.end(), [](CachedEntity *a, CachedEntity *b) { return a->m_flDistance() < b->m_flDistance(); });
        }
        for (auto ammopack : total_ents)
        {
            // If we succeeed, don't try to path to other packs
            if (navparser::NavEngine::navTo(ammopack->m_vecOrigin(), ammo, true, ammopack->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) > 200.0f * 200.0f))
            {
                // Check if we are close enough to the ammo pack to pick it up
                if (ammopack->m_vecOrigin().DistTo(g_pLocalPlayer->v_Origin) < 75.0f)
                {
                    // Try to touch the ammo pack
                    auto local_nav = navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin);
                    if (local_nav)
                    {
                        Vector path_point = local_nav->getNearestPoint(ammopack->m_vecOrigin().AsVector2D());
                        path_point.z = ammopack->m_vecOrigin().z;
                        
                        // Walk towards the ammo pack
                        WalkTo(path_point);
                    }
                }
                was_force = force;
                return true;
            }
        }
        ammo_cooldown.update();
    }
    else if (navparser::NavEngine::current_priority == ammo && !was_force)
        navparser::NavEngine::cancelPath();
    return false;
}

// Find spells if needed
bool getSpells()
{
    if (!shouldSearchSpells())
        return false;
    // Already have a spell
    if (HasCondition<TFCond_HalloweenKart>(LOCAL_E) || HasCondition<TFCond_HalloweenKartDash>(LOCAL_E))
        return navparser::NavEngine::current_priority == Priority_list::spells;

    // No spells - search for one
    if (!navparser::NavEngine::isPathing())
    {
        // Don't try to search for spells if we're doing something else
        if (navparser::NavEngine::current_priority == Priority_list::spells)
        {
            // Get all spell entities
            std::vector<k_EItemType> spell_types = { HALLOWEEN_GHOST };
            auto spell_entities = getEntities(spell_types);
            // Sort by distance, discard empty ones
            std::vector<CachedEntity *> valid_spells;
            for (auto spell : spell_entities)
            {
                if (CE_VALID(spell))
                    valid_spells.push_back(spell);
            }

            // Sort by distance, closest to highest
            std::sort(valid_spells.begin(), valid_spells.end(), [](CachedEntity *a, CachedEntity *b) { return a->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) < b->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin); });

            for (auto spell : valid_spells)
            {
                // Try to nav to spell
                if (navparser::NavEngine::navTo(spell->m_vecOrigin(), Priority_list::spells, true, spell->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) > 200.0f * 200.0f))
                    return true;
            }
            return false;
        }
        // We cannot nav to spell atm
        return false;
    }
    // In the process of navigating to a spell
    else if (navparser::NavEngine::current_priority == Priority_list::spells)
        return true;
    return false;
}

// Find powerups if needed
bool getPowerups()
{
    if (!shouldSearchPowerups())
        return false;
    // Already have a powerup
    if (HasCondition<TFCond_RuneStrength>(LOCAL_E) || HasCondition<TFCond_RuneHaste>(LOCAL_E) || HasCondition<TFCond_RuneRegen>(LOCAL_E) || HasCondition<TFCond_RuneResist>(LOCAL_E) || HasCondition<TFCond_RuneVampire>(LOCAL_E) || HasCondition<TFCond_RuneWarlock>(LOCAL_E) || HasCondition<TFCond_RunePrecision>(LOCAL_E) || HasCondition<TFCond_RuneAgility>(LOCAL_E))
        return navparser::NavEngine::current_priority == Priority_list::prio_powerups;

    // No powerups - search for one
    if (!navparser::NavEngine::isPathing())
    {
        // Don't try to search for powerups if we're doing something else
        if (navparser::NavEngine::current_priority == Priority_list::prio_powerups)
        {
            // Get all powerup entities
            std::vector<k_EItemType> powerup_types = { ITEM_POWERUP_KING };
            auto powerup_entities = getEntities(powerup_types);
            // Sort by distance, discard empty ones
            std::vector<CachedEntity *> valid_powerups;
            for (auto powerup : powerup_entities)
            {
                if (CE_VALID(powerup))
                    valid_powerups.push_back(powerup);
            }

            // Sort by distance, closest to highest
            std::sort(valid_powerups.begin(), valid_powerups.end(), [](CachedEntity *a, CachedEntity *b) { return a->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) < b->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin); });

            for (auto powerup : valid_powerups)
            {
                // Try to nav to powerup
                if (navparser::NavEngine::navTo(powerup->m_vecOrigin(), Priority_list::prio_powerups, true, powerup->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) > 200.0f * 200.0f))
                    return true;
            }
            return false;
        }
        // We cannot nav to powerup atm
        return false;
    }
    // In the process of navigating to a powerup
    else if (navparser::NavEngine::current_priority == Priority_list::prio_powerups)
        return true;
    return false;
}

// Find gargoyles if needed
bool getGargoyles()
{
    if (!shouldSearchGargoyles())
        return false;

    // No gargoyles - search for one
    if (!navparser::NavEngine::isPathing())
    {
        // Don't try to search for gargoyles if we're doing something else
        if (navparser::NavEngine::current_priority == Priority_list::gargoyles)
        {
            // Get all gargoyle entities
            std::vector<k_EItemType> gargoyle_types = { HALLOWEEN_GHOST };
            auto gargoyle_entities = getEntities(gargoyle_types);
            // Sort by distance, discard empty ones
            std::vector<CachedEntity *> valid_gargoyles;
            for (auto gargoyle : gargoyle_entities)
            {
                if (CE_VALID(gargoyle))
                    valid_gargoyles.push_back(gargoyle);
            }

            // Sort by distance, closest to highest
            std::sort(valid_gargoyles.begin(), valid_gargoyles.end(), [](CachedEntity *a, CachedEntity *b) { return a->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) < b->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin); });

            for (auto gargoyle : valid_gargoyles)
            {
                // Try to nav to gargoyle
                if (navparser::NavEngine::navTo(gargoyle->m_vecOrigin(), Priority_list::gargoyles, true, gargoyle->m_vecOrigin().DistToSqr(g_pLocalPlayer->v_Origin) > 200.0f * 200.0f))
                    return true;
            }
            return false;
        }
        // We cannot nav to gargoyle atm
        return false;
    }
    // In the process of navigating to a gargoyle
    else if (navparser::NavEngine::current_priority == Priority_list::gargoyles)
        return true;
    return false;
}

// Vector of sniper spot positions we can nav to
std::vector<Vector> sniper_spots;

// Used for time between refreshing sniperspots
static Timer refresh_sniperspots_timer{};
void refreshSniperSpots()
{
    if (!refresh_sniperspots_timer.test_and_set(60000))
        return;

    sniper_spots.clear();

    // Search all nav areas for valid sniper spots
    for (auto &area : navparser::NavEngine::getNavFile()->m_areas)
        for (auto &hiding_spot : area.m_hidingSpots)
            // Spots actually marked for sniping
            if (hiding_spot.IsExposed() || hiding_spot.IsGoodSniperSpot() || hiding_spot.IsIdealSniperSpot())
                sniper_spots.emplace_back(hiding_spot.m_pos);
}

std::pair<CachedEntity *, float> getNearestPlayerDistance()
{
    float distance         = FLT_MAX;
    CachedEntity *best_ent = nullptr;
    
    try 
    {
        for (auto const &ent: entity_cache::player_cache)
        {
            if (!ent || CE_BAD(ent))
                continue;
                
            // Additional validation
            if (!ent->InternalEntity() || !ent->InternalEntity()->GetRefEHandle().IsValid())
                continue;
                
            if (!ent->m_vecDormantOrigin() || !g_pPlayerResource->isAlive(ent->m_IDX))
                continue;
                
            if (!ent->m_bEnemy() || !player_tools::shouldTarget(ent))
                continue;
                
            if (IsPlayerInvisible(ent))
                continue;
                
            float dist = g_pLocalPlayer->v_Origin.DistTo(*ent->m_vecDormantOrigin());
            if (dist < distance)
            {
                distance = dist;
                best_ent = ent;
            }
        }
    }
    catch (...)
    {
        return { nullptr, FLT_MAX };
    }
    
    return { best_ent, distance };
}

static std::vector<Vector> building_spots;

inline bool HasGunslinger(CachedEntity *ent)
{
    return HasWeapon(ent, 142);
}

inline bool isEngieMode()
{
    return *engie_mode && g_pLocalPlayer->clazz == tf_engineer;
}

bool BlacklistedFromBuilding(CNavArea *area)
{
    // FIXME: Better way of doing this ?
    for (auto blacklisted_area : *navparser::NavEngine::getFreeBlacklist())
    {
        if (blacklisted_area.first == area && blacklisted_area.second.value == navparser::BlacklistReason_enum::BAD_BUILDING_SPOT)
            return true;
    }
    return false;
}

static Timer refresh_buildingspots_timer;
void refreshBuildingSpots(bool force = false)
{
    if (!isEngieMode())
        return;
    if (force || refresh_buildingspots_timer.test_and_set(HasGunslinger(LOCAL_E) ? 1000 : 5000))
    {
        building_spots.clear();
        std::optional<Vector> target;

        auto our_flag = flagcontroller::getFlag(g_pLocalPlayer->team);
        target        = our_flag.spawn_pos;

        if (!target)
        {
            auto nearest = getNearestPlayerDistance();
            if (CE_GOOD(nearest.first))
                target = *nearest.first->m_vecDormantOrigin();
            if (!target)
                target = LOCAL_E->m_vecOrigin();
        }
        if (target)
        {
            // Search all nav areas for valid spots
            for (auto &area : navparser::NavEngine::getNavFile()->m_areas)
            {
                // Blacklisted :(
                if (BlacklistedFromBuilding(&area))
                    continue;
                // BUG Ahead, these flag checks dont seem to work for me :/
                // Don't try to build in spawn lol
                if ((area.m_TFattributeFlags & TF_NAV_SPAWN_ROOM_RED) != 0 || (area.m_TFattributeFlags & TF_NAV_SPAWN_ROOM_BLUE) != 0 || (area.m_TFattributeFlags & TF_NAV_SPAWN_ROOM_EXIT) != 0)
                    continue;
                if ((area.m_TFattributeFlags & TF_NAV_SENTRY_SPOT) != 0)
                    building_spots.emplace_back(area.m_center);
                else
                {
                    for (auto &hiding_spot : area.m_hidingSpots)
                        if (hiding_spot.HasGoodCover())
                            building_spots.emplace_back(hiding_spot.m_pos);
                }
            }
            // Sort by distance to nearest, lower is better
            // TODO: This isnt really optimal, need a dif way to where it is a good distance from enemies but also bots dont build in the same spot
            std::sort(building_spots.begin(), building_spots.end(),
                      [target](Vector a, Vector b)
                      {
                          if (!HasGunslinger(LOCAL_E))
                          {
                              auto a_dist = a.DistTo(*target);
                              auto b_dist = b.DistTo(*target);

                              // Penalty for being in danger ranges
                              if (a_dist + 100.0f < selected_config.min_full_danger)
                                  a_dist += 4000.0f;
                              if (b_dist + 100.0f < selected_config.min_full_danger)
                                  b_dist += 4000.0f;

                              if (a_dist + 1000.0f < selected_config.min_slight_danger)
                                  a_dist += 1500.0f;
                              if (b_dist + 1000.0f < selected_config.min_slight_danger)
                                  b_dist += 1500.0f;

                              return a_dist < b_dist;
                          }
                          else
                              return a.DistTo(*target) < b.DistTo(*target);
                      });
        }
    }
}

static CachedEntity *mySentry    = nullptr;
static CachedEntity *myDispenser = nullptr;

void refreshLocalBuildings()
{
    if (isEngieMode())
    {
        mySentry    = nullptr;
        myDispenser = nullptr;
        if (CE_GOOD(LOCAL_E))
        {
            for (auto const &ent : entity_cache::valid_ents)
            {
                if (ent->m_bEnemy() || !ent->m_bAlivePlayer())
                    continue;
                auto cid = ent->m_iClassID();
                if (cid != CL_CLASS(CObjectSentrygun) && cid != CL_CLASS(CObjectDispenser))
                    continue;
                if (HandleToIDX(CE_INT(ent, netvar.m_hBuilder)) != LOCAL_E->m_IDX)
                    continue;
                if (CE_INT(ent, netvar.m_bPlacing))
                    continue;
                if (cid == CL_CLASS(CObjectSentrygun))
                    mySentry = ent;
                else if (cid == CL_CLASS(CObjectDispenser))
                    myDispenser = ent;
            }
        }
    }
}

static Vector current_building_spot;
static bool navToSentrySpot()
{
    static Timer wait_until_path_sentry;
    // Wait a bit before pathing again
    if (!wait_until_path_sentry.test_and_set(300))
        return false;
    // Try to nav to our existing sentry spot
    if (CE_GOOD(mySentry) && mySentry->m_bAlivePlayer() && mySentry->m_vecDormantOrigin())
    {
        // Don't overwrite current nav
        if (navparser::NavEngine::current_priority == engineer)
            return true;
        if (navparser::NavEngine::navTo(*mySentry->m_vecDormantOrigin(), engineer))
            return true;
    }
    else
        mySentry = nullptr;

    // No building spots
    if (building_spots.empty())
        return false;
    // Don't overwrite current nav
    if (navparser::NavEngine::current_priority == engineer)
        return false;
    // Max 10 attempts
    for (int attempts = 0; attempts < 10 && attempts < building_spots.size(); ++attempts)
    {
        // Get a semi-random building spot to still keep distance preferrance
        auto random_offset = RandomInt(0, std::min(3, (int) building_spots.size()));

        Vector random;

        // Wrap around
        if (attempts - random_offset < 0)
            random = building_spots[building_spots.size() + (attempts - random_offset)];
        else
            random = building_spots[attempts - random_offset];

        // Try to nav there
        if (navparser::NavEngine::navTo(random, engineer))
        {
            current_building_spot = random;
            return true;
        }
    }

    return false;
}

enum slots
{
    primary   = 1,
    secondary = 2,
    melee     = 3,
    pda1      = 4,
    pda2      = 5
};

#if ENABLE_VISUALS
std::vector<Vector> slight_danger_drawlist_normal;
std::vector<Vector> slight_danger_drawlist_dormant;
#endif
static Timer blacklist_update_timer{};
static Timer dormant_update_timer{};
void updateEnemyBlacklist(int slot)
{
    static int last_slot_blacklist = primary;
    bool should_run_normal         = blacklist_update_timer.test_and_set(*blacklist_delay) || last_slot_blacklist != slot;
    bool should_run_dormant        = blacklist_dormat && (dormant_update_timer.test_and_set(*blacklist_delay_dormat) || last_slot_blacklist != slot);
    // Don't run since we do not care here
    if (!should_run_dormant && !should_run_normal)
        return;

    // Clear blacklist for normal entities
    if (should_run_normal)
        navparser::NavEngine::clearFreeBlacklist(navparser::ENEMY_NORMAL);
    // Clear blacklist for dormant entities
    if (should_run_dormant || !blacklist_dormat)
        navparser::NavEngine::clearFreeBlacklist(navparser::ENEMY_DORMANT);

    // #NoFear
    if (slot == melee)
        return;

    // Store the danger of the invidual nav areas
    boost::unordered_flat_map<CNavArea *, int> dormant_slight_danger;
    boost::unordered_flat_map<CNavArea *, int> normal_slight_danger;

    // This is used to cache Dangerous areas between ents
    boost::unordered_flat_map<CachedEntity *, std::vector<CNavArea *>> ent_marked_dormant_slight_danger;
    boost::unordered_flat_map<CachedEntity *, std::vector<CNavArea *>> ent_marked_normal_slight_danger;

    std::vector<std::pair<CachedEntity *, Vector>> checked_origins;
    for (auto const &ent: entity_cache::player_cache)
    {
        
        // Entity is generally invalid, ignore
        if (CE_INVALID(ent) || !g_pPlayerResource->isAlive(ent->m_IDX))
            continue;
        // On our team, do not care
        if (g_pPlayerResource->GetTeam(ent->m_IDX) == g_pLocalPlayer->team)
            continue;

        bool is_dormant = CE_BAD(ent);
        // Should not run on dormant and entity is dormant, ignore.
        if (!should_run_dormant && is_dormant)
            continue;
        // Should not run on normal entity and entity is not dormant, ignore
        else if (!should_run_normal && !is_dormant)
            continue;

        // Avoid excessive calls by ignoring new checks if people are too close to eachother
        auto origin = ent->m_vecDormantOrigin();
        if (!origin)
            continue;
        bool should_check = true;

        // Find already dangerous marked areas by other entities
        auto to_loop = is_dormant ? &ent_marked_dormant_slight_danger : &ent_marked_normal_slight_danger;

        // Add new danger entries
        auto to_mark = is_dormant ? &dormant_slight_danger : &normal_slight_danger;

        for (auto &checked_origin : checked_origins)
        {
            // If this origin is closer than a quarter of the min HU (or less than 100 HU) to a cached one, don't go through
            // all nav areas again DistToSqr is much faster than DistTo which is why we use it here
            auto distance = selected_config.min_slight_danger;

            distance *= 0.25f;
            distance = std::max(100.0f, distance);

            // Square the distance
            distance *= distance;

            if ((*origin).DistToSqr(checked_origin.second) < distance)
            {
                should_check = false;

                bool is_absolute_danger = distance < selected_config.min_full_danger;
                if (!is_absolute_danger && (enable_slight_danger_when_capping || navparser::NavEngine::current_priority != capture))
                    for (auto &area : (*to_loop)[checked_origin.first])
                    {
                        (*to_mark)[area]++;
                        if ((*to_mark)[area] >= *blacklist_slightdanger_limit)
                            (*navparser::NavEngine::getFreeBlacklist())[area] = is_dormant ? navparser::ENEMY_DORMANT : navparser::ENEMY_NORMAL;
                    }

                break;
            }
        }
        if (!should_check)
            continue;

        // Now check which areas they are close to
        for (CNavArea &nav_area : navparser::NavEngine::getNavFile()->m_areas)
        {
            float distance             = nav_area.m_center.DistTo(*origin);
            float slight_danger_dist   = selected_config.min_slight_danger;
            float absolute_danger_dist = selected_config.min_full_danger;

            // Not dangerous, Still don't bump
            if (!player_tools::shouldTarget(ent))
            {
                slight_danger_dist   = navparser::PLAYER_WIDTH * 1.2f;
                absolute_danger_dist = navparser::PLAYER_WIDTH * 1.2f;
            }

            // Too close to count as slight danger
            bool is_absolute_danger = distance < absolute_danger_dist;
            if (distance < slight_danger_dist)
            {
                // Add as marked area
                (*to_loop)[ent].push_back(&nav_area);

                // Just slightly dangerous, only mark as such if it's clear
                if (!is_absolute_danger && (enable_slight_danger_when_capping || navparser::NavEngine::current_priority != capture))
                {
                    (*to_mark)[&nav_area]++;
                    if ((*to_mark)[&nav_area] < *blacklist_slightdanger_limit)
                        continue;
                }
                (*navparser::NavEngine::getFreeBlacklist())[&nav_area] = is_dormant ? navparser::ENEMY_DORMANT : navparser::ENEMY_NORMAL;
            }
        }
        checked_origins.emplace_back(ent, *origin);
    }
#if ENABLE_VISUALS
    if (should_run_dormant)
        slight_danger_drawlist_dormant.clear();
    if (should_run_normal)
        slight_danger_drawlist_normal.clear();

    // Store slight danger areas for drawing
    if (!normal_slight_danger.empty())
    {
        for (auto &area : normal_slight_danger)
            if (area.second < *blacklist_slightdanger_limit)
                slight_danger_drawlist_normal.push_back(area.first->m_center);
    }
    if (!dormant_slight_danger.empty())
    {
        for (auto &area : dormant_slight_danger)
            if (area.second < *blacklist_slightdanger_limit)
                slight_danger_drawlist_dormant.push_back(area.first->m_center);
    }
#endif
}

// Check if an area is valid for stay near. the Third parameter is to save some performance.
bool isAreaValidForStayNear(Vector ent_origin, CNavArea *area, bool fix_local_z = true)
{
    if (fix_local_z)
        ent_origin.z += navparser::PLAYER_JUMP_HEIGHT;
    auto area_origin = area->m_center;
    area_origin.z += navparser::PLAYER_JUMP_HEIGHT;

    // Do all the distance checks
    float distance = ent_origin.DistToSqr(area_origin);

    // Too close
    if (distance < selected_config.min_full_danger * selected_config.min_full_danger)
        return false;
    // Blacklisted
    if (navparser::NavEngine::getFreeBlacklist()->find(area) != navparser::NavEngine::getFreeBlacklist()->end())
        return false;
    // Too far away
    if (distance > selected_config.max * selected_config.max)
        return false;
    // Attempt to vischeck
    if (!IsVectorVisibleNavigation(ent_origin, area_origin))
        return false;
    return true;
}

// Actual logic, used to de-duplicate code
bool stayNearTarget(CachedEntity *ent)
{
    auto ent_origin = ent->m_vecDormantOrigin();
    // No origin recorded, don't bother
    if (!ent_origin)
        return false;

    // Add the vischeck height
    ent_origin->z += navparser::PLAYER_JUMP_HEIGHT;

    // Use std::pair to avoid using the distance functions more than once
    std::vector<std::pair<CNavArea *, float>> good_areas{};

    for (auto &area : navparser::NavEngine::getNavFile()->m_areas)
    {
        auto area_origin = area.m_center;

        // Is this area valid for stay near purposes?
        if (!isAreaValidForStayNear(*ent_origin, &area, false))
            continue;

        float distance = (*ent_origin).DistToSqr(area_origin);
        // Good area found
        good_areas.push_back(std::pair<CNavArea *, float>(&area, distance));
    }
    // Sort based on distance
    if (selected_config.prefer_far)
        std::sort(good_areas.begin(), good_areas.end(), [](std::pair<CNavArea *, float> a, std::pair<CNavArea *, float> b) { return a.second > b.second; });
    else
        std::sort(good_areas.begin(), good_areas.end(), [](std::pair<CNavArea *, float> a, std::pair<CNavArea *, float> b) { return a.second < b.second; });

    // Try to path to all the good areas, based on distance
    if (std::ranges::any_of(good_areas, [](std::pair<CNavArea *, float> area) { return navparser::NavEngine::navTo(area.first->m_center, staynear, true, !navparser::NavEngine::isPathing()); }))
        return true;

    return false;
}

// A bunch of basic checks to ensure we don't try to target an invalid entity
bool isStayNearTargetValid(CachedEntity *ent)
{
    return CE_VALID(ent) && g_pPlayerResource->isAlive(ent->m_IDX) && ent->m_IDX != g_pLocalPlayer->entity_idx && g_pLocalPlayer->team != ent->m_iTeam() && player_tools::shouldTarget(ent) && !IsPlayerInvisible(ent) && !IsPlayerInvulnerable(ent);
}

// Recursive function to find hiding spot
std::optional<std::pair<CNavArea *, int>> findClosestHidingSpot(CNavArea *area, Vector vischeck_point, int recursion_count, int index = 0)
{
    static std::vector<CNavArea *> already_recursed;
    if (index == 0)
        already_recursed.clear();
    Vector area_origin = area->m_center;
    area_origin.z += navparser::PLAYER_JUMP_HEIGHT;

    // Increment recursion index
    index++;

    // If the area works, return it
    if (!IsVectorVisibleNavigation(area_origin, vischeck_point))
        return std::pair<CNavArea *, int>{ area, index - 1 };

    // Termination condition not hit yet
    else if (index != recursion_count)
    {
        // Store the nearest area
        std::optional<std::pair<CNavArea *, int>> best_area = std::nullopt;

        for (auto &connection : area->m_connections)
        {
            if (std::find(already_recursed.begin(), already_recursed.end(), connection.area) != already_recursed.end())
                continue;
            already_recursed.push_back(connection.area);
            auto area = findClosestHidingSpot(connection.area, vischeck_point, recursion_count, index);
            if (area && (!best_area || area->second < best_area->second))
                best_area = { area->first, area->second };
        }
        return best_area;
    }
    else
        return std::nullopt;
}

// Try to avoid enemy sightlines and reload in peace
bool runReload()
{
    PROF_SECTION(runReload)
    static Timer reloadrun_cooldown{};

    // Not reloading, do not run
    if (!(CE_GOOD(LOCAL_E) && !HasCondition<TFCond_HalloweenGhostMode>(LOCAL_E) && CE_GOOD(LOCAL_W) && re::C_BaseCombatWeapon::GetSlot(RAW_ENT(LOCAL_W)) + 1 != melee && !CanShoot()))
        return false;

    if (!stay_near)
        return false;

    // Re-calc only every once in a while
    if (!reloadrun_cooldown.test_and_set(1000))
        return navparser::NavEngine::current_priority == run_reload;

    // Too high priority, so don't try
    if (navparser::NavEngine::current_priority > run_reload)
        return false;

    // Get our area and start recursing the neighbours
    CNavArea *local_area = navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin);
    if (!local_area)
        return false;

    // Get closest enemy to vicheck
    CachedEntity *closest_visible_enemy = nullptr;
    float best_distance                 = FLT_MAX;
    for (auto const &ent : entity_cache::valid_ents)
    {
        if (!ent->m_bAlivePlayer() || !ent->m_bEnemy())
            continue;
        if (ent->m_flDistance() > best_distance)
            continue;
        if (!ent->IsVisible())
            continue;
        if (!player_tools::shouldTarget(ent))
            continue;

        best_distance         = ent->m_flDistance();
        closest_visible_enemy = ent;
    }

    if (!closest_visible_enemy)
        return false;

    Vector vischeck_point = closest_visible_enemy->m_vecOrigin();
    vischeck_point.z += navparser::PLAYER_JUMP_HEIGHT;

    // Get the best non visible area
    auto best_area = findClosestHidingSpot(local_area, vischeck_point, 5);
    if (!best_area)
        return false;

    // If we can, path
    if (navparser::NavEngine::navTo((*best_area).first->m_center, run_reload, true, false, false))
        return true;
    else
        return false;
}

// Try to stay near enemies and stalk them (or in case of sniper, try to stay far from them
// and snipe them)
bool stayNear()
{
    PROF_SECTION(stayNear)
    static Timer staynear_cooldown{};
    static CachedEntity *previous_target = nullptr;

    // Stay near is expensive so we have to cache. We achieve this by only checking a pre-determined amount of players every
    // CreateMove
    constexpr int MAX_STAYNEAR_CHECKS_RANGE = 3;
    constexpr int MAX_STAYNEAR_CHECKS_CLOSE = 2;
    static int lowest_check_index           = 0;

    // Stay near is off
    if (!stay_near)
        return false;
    // Don't constantly path, it's slow.
    // Far range classes do not need to repath nearly as often as close range ones.
    if (!staynear_cooldown.test_and_set(selected_config.prefer_far ? 2000 : 500))
        return navparser::NavEngine::current_priority == staynear;

    // Too high priority, so don't try
    if (navparser::NavEngine::current_priority > staynear)
        return false;

    // Check and use our previous target if available
    if (isStayNearTargetValid(previous_target))
    {
        // Check if target is RAGE status - if so, always keep targeting them
        auto &pl = playerlist::AccessData(previous_target);
        if (pl.state == playerlist::k_EState::RAGE)
        {
            if (stayNearTarget(previous_target))
                return true;
        }
        
        auto ent_origin = previous_target->m_vecDormantOrigin();
        if (ent_origin)
        {
            // Check if current target area is valid
            if (navparser::NavEngine::isPathing())
            {
                auto crumbs = navparser::NavEngine::getCrumbs();
                // We cannot just use the last crumb, as it is always nullptr
                if (crumbs->size() > 1)
                {
                    auto last_crumb = (*crumbs)[crumbs->size() - 2];
                    // Area is still valid, stay on it
                    if (isAreaValidForStayNear(*ent_origin, last_crumb.navarea))
                        return true;
                }
            }
            // Else Check our origin for validity (Only for ranged classes)
            else if (selected_config.prefer_far && isAreaValidForStayNear(*ent_origin, navparser::NavEngine::findClosestNavSquare(LOCAL_E->m_vecOrigin())))
                return true;
        }
        // Else we try to path again
        if (stayNearTarget(previous_target))
            return true;
        // Failed, invalidate previous target and try others
        previous_target = nullptr;
    }

    auto advance_count = selected_config.prefer_far ? MAX_STAYNEAR_CHECKS_RANGE : MAX_STAYNEAR_CHECKS_CLOSE;

    // Ensure it is in bounds and also wrap around
    if (lowest_check_index > g_IEngine->GetMaxClients())
        lowest_check_index = 0;

    int calls = 0;
    
    // First check for RAGE players - they get highest priority
    for (int i = 1; i <= g_IEngine->GetMaxClients(); i++)
    {
        CachedEntity* ent = ENTITY(i);
        if (!isStayNearTargetValid(ent))
            continue;
            
        auto &pl = playerlist::AccessData(ent);
        if (pl.state == playerlist::k_EState::RAGE)
        {
            if (stayNearTarget(ent))
            {
                previous_target = ent;
                return true;
            }
        }
    }

    // Then check other players
    for (int i = lowest_check_index; i <= g_IEngine->GetMaxClients(); ++i)
    {
        CachedEntity* ent = ENTITY(i);
        if (calls >= advance_count)
            break;
        calls++;
        lowest_check_index++;
        
        if (!isStayNearTargetValid(ent))
        {
            calls--;
            continue;
        }
        
        // Skip RAGE players as we already checked them
        auto &pl = playerlist::AccessData(ent);
        if (pl.state == playerlist::k_EState::RAGE)
            continue;
            
        // Succeeded pathing
        if (stayNearTarget(ent))
        {
            previous_target = ent;
            return true;
        }
    }
    // Stay near failed to find any good targets, add extra delay
    staynear_cooldown.last += std::chrono::seconds(3);
    return false;
}

// Try to attack people using melee if we are in a situation where this is viable
bool meleeAttack(int slot, std::pair<CachedEntity *, float> &nearest)
{
    // There is no point in engaging the melee AI if we are not using melee
    if (slot != melee || !nearest.first || CE_BAD(nearest.first))
    {
        if (navparser::NavEngine::current_priority == prio_melee)
            navparser::NavEngine::cancelPath();
        return false;
    }

    // Too high priority, so don't try
    if (navparser::NavEngine::current_priority > prio_melee)
        return false;

    auto raw_local = RAW_ENT(LOCAL_E);
    if (!raw_local)
        return false;

    // We are charging, let the charge aimbot do it's job
    if (HasCondition<TFCond_Charging>(LOCAL_E))
    {
        navparser::NavEngine::cancelPath();
        return true;
    }

    static Timer melee_cooldown{};
    bool isVisible;

    {
        Ray_t ray;
        trace_t trace;
        trace::filter_default.SetSelf(raw_local);

        auto hitbox = nearest.first->hitboxes.GetHitbox(spine_3);
        if (!hitbox)
            return false;
            
        ray.Init(g_pLocalPlayer->v_Origin + Vector{ 0, 0, 20 }, hitbox->center, raw_local->GetCollideable()->OBBMins(), raw_local->GetCollideable()->OBBMaxs());
        g_ITrace->TraceRay(ray, MASK_PLAYERSOLID, &trace::filter_default, &trace);
        isVisible = (IClientEntity *) trace.m_pEnt == RAW_ENT(nearest.first);
    }

    // Charge aimbot things
    if (hacks::tf2::misc_aimbot::ShouldChargeAim() && re::C_BasePlayer::GetEquippedDemoShield(raw_local) && re::CTFPlayerShared::GetChargeMeter(re::CTFPlayerShared::GetPlayerShared(raw_local)) == 100.0f)
    {
        // Distance normally covered per second by charge
        float distance_per_second = 750.0f;
        // Apply modifiers to movespeed
        distance_per_second = ATTRIB_HOOK_FLOAT(distance_per_second, "mult_player_movespeed_shieldrequired", raw_local, 0x0, true);
        distance_per_second = ATTRIB_HOOK_FLOAT(distance_per_second, "mult_player_movespeed", raw_local, 0x0, true);
        // Max is still 750.0f
        distance_per_second = std::min(distance_per_second, 750.0f);
        // Time spent charging
        float seconds = 1.5f;
        // Apply modifiers that change charge length
        seconds = ATTRIB_HOOK_FLOAT(seconds, "mod_charge_time", RAW_ENT(LOCAL_E), 0x0, true);
        // Total distance covered by charge
        float total_distance = seconds * distance_per_second;
        if (nearest.second < total_distance && isVisible)
        {
            // Charge
            current_user_cmd->buttons |= IN_ATTACK2;
            AimAt(g_pLocalPlayer->v_Eye, nearest.first->m_vecOrigin(), current_user_cmd);
            navparser::NavEngine::cancelPath();
            return true;
        }
    }
    // If we are close enough, don't even bother with using the navparser to get there
    if (nearest.second < 200 && isVisible)
    {
        WalkTo(nearest.first->m_vecOrigin());
        navparser::NavEngine::cancelPath();
        return true;
    }
    else
    {
        // Don't constantly path, it's slow.
        // The closer we are, the more we should try to path
        if (!melee_cooldown.test_and_set(nearest.second < 200 ? 200 : nearest.second < 1000 ? 500 : 2000) && navparser::NavEngine::isPathing())
            return navparser::NavEngine::current_priority == prio_melee;

        // Just walk at the enemy l0l
        if (navparser::NavEngine::navTo(nearest.first->m_vecOrigin(), prio_melee, true, !navparser::NavEngine::isPathing()))
            return true;
        return false;
    }
}

// Basically the same as isAreaValidForStayNear, but some restrictions lifted.
bool isAreaValidForSnipe(Vector ent_origin, Vector area_origin, bool fix_sentry_z = true)
{
    if (fix_sentry_z)
        ent_origin.z += 40.0f;
    area_origin.z += navparser::PLAYER_JUMP_HEIGHT;

    float distance = ent_origin.DistToSqr(area_origin);
    // Too close to be valid
    if (distance <= (1100.0f + navparser::HALF_PLAYER_WIDTH) * (1100.0f + navparser::HALF_PLAYER_WIDTH))
        return false;
    // Fails vischeck, bad
    if (!IsVectorVisibleNavigation(area_origin, ent_origin))
        return false;
    return true;
}

// Try to snipe the sentry
bool tryToSnipe(CachedEntity *ent)
{
    auto ent_origin = GetBuildingPosition(ent);
    // Add some z to dormant sentries as it only returns origin
    if (CE_BAD(ent))
        ent_origin.z += 40.0f;

    std::vector<std::pair<CNavArea *, float>> good_areas;
    for (auto &area : navparser::NavEngine::getNavFile()->m_areas)
    {
        // Not usable
        if (!isAreaValidForSnipe(ent_origin, area.m_center, false))
            continue;
        good_areas.push_back(std::pair<CNavArea *, float>(&area, area.m_center.DistToSqr(ent_origin)));
    }

    // Sort based on distance
    if (selected_config.prefer_far)
        std::sort(good_areas.begin(), good_areas.end(), [](std::pair<CNavArea *, float> a, std::pair<CNavArea *, float> b) { return a.second > b.second; });
    else
        std::sort(good_areas.begin(), good_areas.end(), [](std::pair<CNavArea *, float> a, std::pair<CNavArea *, float> b) { return a.second < b.second; });

    if (std::ranges::any_of(good_areas, [](std::pair<CNavArea *, float> area) { return navparser::NavEngine::navTo(area.first->m_center, snipe_sentry); }))
        return true;
    return false;
}

// Is our target valid?
bool isSnipeTargetValid(CachedEntity *ent)
{
    return CE_VALID(ent) && ent->m_bAlivePlayer() && ent->m_iTeam() != g_pLocalPlayer->team && ent->m_iClassID() == CL_CLASS(CObjectSentrygun);
}

// Try to Snipe sentries
bool snipeSentries()
{
    static Timer sentry_snipe_cooldown;
    static CachedEntity *previous_target = nullptr;

    if (!snipe_sentries)
        return false;

    // Sentries don't move often, so we can use a slightly longer timer
    if (!sentry_snipe_cooldown.test_and_set(2000))
        return navparser::NavEngine::current_priority == snipe_sentry || isSnipeTargetValid(previous_target);

    if (isSnipeTargetValid(previous_target))
    {
        auto crumbs = navparser::NavEngine::getCrumbs();
        // We cannot just use the last crumb, as it is always nullptr
        if (crumbs->size() > 1)
        {
            auto last_crumb = (*crumbs)[crumbs->size() - 2];
            // Area is still valid, stay on it
            if (isAreaValidForSnipe(GetBuildingPosition(previous_target), last_crumb.navarea->m_center))
                return true;
        }
        if (tryToSnipe(previous_target))
            return true;
    }

    // Make sure we don't try to do it on shortrange classes unless specified
    if (!snipe_sentries_shortrange && (g_pLocalPlayer->clazz == tf_scout || g_pLocalPlayer->clazz == tf_pyro))
        return false;

    for (auto const &ent : entity_cache::valid_ents)
    {
        // Invalid sentry
        if (!isSnipeTargetValid(ent))
            continue;
        // Succeeded in trying to snipe it
        if (tryToSnipe(ent))
        {
            previous_target = ent;
            return true;
        }
    }
    return false;
}

enum building
{
    dispenser = 0,
    sentry    = 2
};

static int build_attempts = 0;
static bool buildBuilding(int building)
{
    // Blacklist this spot and refresh the building spots
    if (build_attempts >= 15)
    {
        (*navparser::NavEngine::getFreeBlacklist())[navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin)] = navparser::BlacklistReason_enum::BAD_BUILDING_SPOT;
        refreshBuildingSpots(true);
        current_building_spot.Invalidate();
        build_attempts = 0;
        return false;
    }
    // Make sure we have right amount of ammo
    int required = (HasGunslinger(LOCAL_E) || building == dispenser) ? 100 : 130;
    if (CE_INT(LOCAL_E, netvar.m_iAmmo + 12) < required)
        return getAmmo(true);

    // Try to build! we are close enough
    if (current_building_spot.IsValid() && current_building_spot.DistTo(g_pLocalPlayer->v_Origin) <= (building == dispenser ? 500.0f : 200.0f))
    {
        // TODO: Rotate our angle to a valid building spot ? also rotate building itself to face enemies ?
        current_user_cmd->viewangles.x = 20.0f;
        current_user_cmd->viewangles.y += 2.0f;

        // Gives us 4 1/2 seconds to build
        static Timer attempt_timer;
        if (attempt_timer.test_and_set(300))
            build_attempts++;

        if (hacks::shared::misc::getCarriedBuilding() == -1)
        {
            static Timer command_timer;
            if (command_timer.test_and_set(100))
                g_IEngine->ClientCmd_Unrestricted(strfmt("build %d", building).get());
        }
        else if (CE_INT(ENTITY(hacks::shared::misc::getCarriedBuilding()), netvar.m_bCanPlace))
            current_user_cmd->buttons |= IN_ATTACK;
        return true;
    }
    else
        return navToSentrySpot();

    return false;
}

static bool buildingNeedsToBeSmacked(CachedEntity *ent)
{
    if (CE_BAD(ent))
        return false;

    if (CE_INT(ent, netvar.iUpgradeLevel) != 3 || ent->m_iHealth() / ent->m_iMaxHealth() <= 0.80f)
        return true;
    if (ent->m_iClassID() == CL_CLASS(CObjectSentrygun))
    {
        int max_ammo = 0;
        switch (CE_INT(ent, netvar.iUpgradeLevel))
        {
        case 1:
            max_ammo = 150;
            break;
        case 2:
        case 3:
            max_ammo = 200;
            break;
        }

        return CE_INT(ent, netvar.m_iAmmoShells) / max_ammo <= 0.50f;
    }
    return false;
}

static bool smackBuilding(CachedEntity *ent)
{
    if (CE_BAD(ent))
        return false;
    if (!CE_INT(LOCAL_E, netvar.m_iAmmo + 12))
        return getAmmo(true);

    if (ent->m_flDistance() <= 100.0f && g_pLocalPlayer->weapon_mode == weapon_melee)
    {
        AimAt(g_pLocalPlayer->v_Eye, GetBuildingPosition(ent), current_user_cmd);
        current_user_cmd->buttons |= IN_ATTACK;
    }
    else if (navparser::NavEngine::current_priority != engineer)
        return navparser::NavEngine::navTo(*ent->m_vecDormantOrigin(), engineer);
    return true;
}

static bool runEngineerLogic()
{
    if (!isEngieMode())
        return false;

    // Already have a sentry
    if (CE_VALID(mySentry) && mySentry->m_bAlivePlayer())
    {
        if (HasGunslinger(LOCAL_E))
        {
            // Too far away, destroy it
            // BUG Ahead, building isnt destroyed lol
            if (mySentry->m_flDistance() >= 1800.0f)
            {
                // If we have a valid building
                if (mySentry->m_Type() == CL_CLASS(CObjectSentrygun))
                    g_IEngine->ClientCmd_Unrestricted("destroy 2");
            }
            // Return false so we run another task
            return false;
        }
        else
        {
            // Try to smack sentry first
            if (buildingNeedsToBeSmacked(mySentry))
                return smackBuilding(mySentry);
            else
            {
                // We put dispenser by sentry
                if (CE_BAD(myDispenser))
                    return buildBuilding(dispenser);
                else
                {
                    // We already have a dispenser, see if it needs to be smacked
                    if (buildingNeedsToBeSmacked(myDispenser))
                        return smackBuilding(myDispenser);
                }
            }
        }
    }
    else
        // Try to build a sentry
        return buildBuilding(sentry);
    return false;
}

enum capture_type
{
    no_capture,
    ctf,
    payload,
    controlpoints
};

static capture_type current_capturetype = no_capture;
// Overwrite to return true for payload carts as an example
static bool overwrite_capture = false;
// Doomsday is a ctf + payload map which breaks capturing...
static bool is_doomsday = false;

std::optional<Vector> getCtfGoal(int our_team, int enemy_team)
{
    // Get Flag related information
    auto status   = flagcontroller::getStatus(enemy_team);
    auto position = flagcontroller::getPosition(enemy_team);
    auto carrier  = flagcontroller::getCarrier(enemy_team);

    // No flag :(
    if (!position)
        return std::nullopt;

    current_capturetype = ctf;

    // Assist other bots with capturing
    if (status == TF_FLAGINFO_STOLEN && carrier != LOCAL_E)
    {
        // If carrier is a friendly bot/player, help them by following
        if (carrier->m_iTeam() == our_team)
        {
            // Stay slightly behind and to the side to avoid blocking
            auto carrier_pos = *carrier->m_vecDormantOrigin();
            Vector offset(40.0f, 40.0f, 0.0f);
            return carrier_pos - offset;
        }
    }

    // Flag is taken by us
    if (status == TF_FLAGINFO_STOLEN)
    {
        if (carrier == LOCAL_E)
        {
            // Return our capture point location
            auto team_flag = flagcontroller::getFlag(our_team);
            return team_flag.spawn_pos;
        }
    }
    // Get the flag if not taken by us already
    else if (status == TF_FLAGINFO_DROPPED || status == TF_FLAGINFO_HOME)
    {
        return position;
    }

    return std::nullopt;
}

std::optional<Vector> getPayloadGoal(int our_team)
{
    auto position = plcontroller::getClosestPayload(g_pLocalPlayer->v_Origin, our_team);
    // No payloads found :(
    if (!position)
        return std::nullopt;
    current_capturetype = payload;

    // Get number of teammates near cart to coordinate positioning
    int teammates_near_cart = 0;
    float cart_radius = 150.0f; // Approx cart capture radius
    
    for (const auto &ent : entity_cache::player_cache)
    {
        if (!ent->m_bAlivePlayer() || ent->m_iTeam() != our_team || ent == LOCAL_E)
            continue;
            
        if (ent->m_vecOrigin().DistTo(*position) <= cart_radius)
            teammates_near_cart++;
    }

    // Adjust position based on number of teammates to avoid crowding
    Vector adjusted_pos = *position;
    if (teammates_near_cart > 0)
    {
        // Create a ring formation around cart
        float angle = M_PI * 2 * (float)(g_pLocalPlayer->entity_idx % (teammates_near_cart + 1)) / (teammates_near_cart + 1);
        Vector offset(cos(angle) * 75.0f, sin(angle) * 75.0f, 0.0f);
        adjusted_pos += offset;
    }

    // Adjust height based on local ground height when close
    if (LOCAL_E->m_vecOrigin().DistTo(adjusted_pos) <= cart_radius)
    {
        // Trace down to find ground height
        Vector ground_pos = adjusted_pos;
        ground_pos.z += 80.0f; // Start above cart
        
        Ray_t ray;
        trace_t trace;
        ray.Init(ground_pos, ground_pos - Vector(0, 0, 256));
        g_ITrace->TraceRay(ray, MASK_PLAYERSOLID, &trace::filter_default, &trace);
        
        if (trace.DidHit())
            adjusted_pos.z = trace.endpos.z;
    }

    // If very close to adjusted position, don't move
    if (adjusted_pos.DistTo(LOCAL_E->m_vecOrigin()) <= 50.0f)
    {
        overwrite_capture = true;
        return std::nullopt;
    }

    return adjusted_pos;
}

std::optional<Vector> getControlPointGoal(int our_team)
{
    static Vector previous_position(0.0f);
    static Vector randomized_position(0.0f);

    auto position = cpcontroller::getClosestControlPoint(g_pLocalPlayer->v_Origin, our_team);
    // No points found :(
    if (!position)
        return std::nullopt;

    current_capturetype = controlpoints;

    // Get number of teammates on point
    int teammates_on_point = 0;
    float cap_radius = 100.0f; // Approximate capture radius
    
    for (const auto &ent : entity_cache::player_cache)
    {
        if (!ent->m_bAlivePlayer() || ent->m_iTeam() != our_team || ent == LOCAL_E)
            continue;
            
        if (ent->m_vecOrigin().DistTo(*position) <= cap_radius)
            teammates_on_point++;
    }

    // Check for enemies near point
    bool enemies_near = false;
    float threat_radius = 800.0f; // Distance to check for enemies
    
    for (const auto &ent : entity_cache::player_cache)
    {
        if (!ent->m_bAlivePlayer() || ent->m_iTeam() == our_team)
            continue;
            
        if (ent->m_vecOrigin().DistTo(*position) <= threat_radius)
        {
            enemies_near = true;
            break;
        }
    }

    Vector adjusted_pos = *position;

    // If enemies are near, take defensive positions
    if (enemies_near)
    {
        // Find nearby cover points
        for (auto &area : navparser::NavEngine::getNavFile()->m_areas)
        {
            for (auto &hiding_spot : area.m_hidingSpots)
            {
                if (hiding_spot.HasGoodCover() && hiding_spot.m_pos.DistTo(*position) <= cap_radius)
                {
                    adjusted_pos = hiding_spot.m_pos;
                    break;
                }
            }
        }
    }
    // Otherwise spread out in capture formation
    else
    {
        // Only update position when needed
        if (previous_position != *position || !navparser::NavEngine::isPathing())
        {
            previous_position = *position;
            
            // Create spread out formation based on player index and class
            float base_radius = 120.0f;
            
            // Add some randomization but keep formation
            float angle = M_PI * 2 * (float)(g_pLocalPlayer->entity_idx % (teammates_on_point + 1)) / (teammates_on_point + 1);
            float radius = base_radius + RandomFloat(-10.0f, 10.0f);
            Vector offset(cos(angle) * radius, sin(angle) * radius, 0.0f);
            
            adjusted_pos += offset;
        }
    }

    // If close enough to adjusted position, don't move
    if (adjusted_pos.DistTo(LOCAL_E->m_vecOrigin()) <= 50.0f)
    {
        overwrite_capture = true;
        return std::nullopt;
    }

    return adjusted_pos;
}

std::optional<Vector> getDoomsdayGoal(int our_team, int enemy_team)
{
    // Get Australium related information
    auto status = flagcontroller::getStatus(TEAM_NEUTRAL);  // Australium is neutral team
    auto position = flagcontroller::getPosition(TEAM_NEUTRAL);
    auto carrier = flagcontroller::getCarrier(TEAM_NEUTRAL);

    // No australium found
    if (!position)
        return std::nullopt;

    current_capturetype = ctf;  // Doomsday uses CTF mechanics

    // Help friendly carrier
    if (status == TF_FLAGINFO_STOLEN && carrier != LOCAL_E)
    {
        if (carrier->m_iTeam() == our_team)
        {
            // Stay slightly behind and to the side to avoid blocking
            auto carrier_pos = *carrier->m_vecDormantOrigin();
            Vector offset(40.0f, 40.0f, 0.0f);
            return carrier_pos - offset;
        }
    }

    // We have the australium
    if (status == TF_FLAGINFO_STOLEN && carrier == LOCAL_E)
    {
        // Get rocket position - in Doomsday it's marked as a cap point
        auto rocket = cpcontroller::getClosestControlPoint(g_pLocalPlayer->v_Origin, our_team);
        if (rocket)
            return *rocket;
    }
    // Get the australium if not taken
    else if (status == TF_FLAGINFO_DROPPED || status == TF_FLAGINFO_HOME)
    {
        return position;
    }

    return std::nullopt;
}

bool captureObjectives()
{
    static Timer capture_timer;
    static Vector previous_target(0.0f);

    if (!*capture_objectives || !capture_timer.check(2000))
        return false;

    // Priority too high, don't try
    if (navparser::NavEngine::current_priority > capture)
        return false;

    // Where we want to go
    std::optional<Vector> target;

    int our_team   = g_pLocalPlayer->team;
    int enemy_team = our_team == TEAM_BLU ? TEAM_RED : TEAM_BLU;

    current_capturetype = no_capture;
    overwrite_capture   = false;

    // Check if we're on doomsday
    auto map_name = std::string(g_IEngine->GetLevelName());
    bool is_doomsday = map_name.find("sd_doomsday") != map_name.npos;

    if (is_doomsday)
    {
        target = getDoomsdayGoal(our_team, enemy_team);
    }
    else
    {
        // Run ctf logic
        target = getCtfGoal(our_team, enemy_team);
        // Not ctf, run payload
        if (current_capturetype == no_capture)
        {
            target = getPayloadGoal(our_team);
            // Not payload, run control points
            if (current_capturetype == no_capture)
            {
                target = getControlPointGoal(our_team);
            }
        }
    }

    // Overwritten, for example because we are currently on the payload, cancel any sort of pathing and return true
    if (overwrite_capture)
    {
        navparser::NavEngine::cancelPath();
        return true;
    }
    // No target, bail and set on cooldown
    else if (!target)
    {
        capture_timer.update();
        return false;
    }
    // If priority is not capturing or we have a new target, try to path there
    else if (navparser::NavEngine::current_priority != capture || *target != previous_target)
    {
        if (navparser::NavEngine::navTo(*target, capture, true, !navparser::NavEngine::isPathing()))
        {
            previous_target = *target;
            return true;
        }
        else
            capture_timer.update();
    }
    return false;
}

// Roam around map
bool doRoam()
{
    static Timer roam_timer;
    static std::vector<CNavArea*> visited_areas;
    static Timer visited_areas_clear_timer;
    static CNavArea* current_target = nullptr;
    static int consecutive_fails = 0;
    
    // Clear visited areas every 60 seconds to allow revisiting
    if (visited_areas_clear_timer.test_and_set(60000))
    {
        visited_areas.clear();
        consecutive_fails = 0;
    }
    
    // Don't path constantly
    if (!roam_timer.test_and_set(2000))
        return navparser::NavEngine::current_priority == patrol;

    // If we have a current target and are pathing, continue
    if (current_target && navparser::NavEngine::current_priority == patrol)
        return true;

    // Reset current target
    current_target = nullptr;

    // Get our current nav area
    auto current_area = navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin);
    if (!current_area)
        return false;

    std::vector<CNavArea*> valid_areas;
    
    // Get all nav areas
    for (auto& area : navparser::NavEngine::getNavFile()->m_areas)
    {
        // Skip if area is blacklisted
        if (navparser::NavEngine::getFreeBlacklist()->find(&area) != navparser::NavEngine::getFreeBlacklist()->end())
            continue;
            
        // Skip if we recently visited this area
        if (std::find(visited_areas.begin(), visited_areas.end(), &area) != visited_areas.end())
            continue;

        // Skip areas that are too close
        float dist = area.m_center.DistTo(g_pLocalPlayer->v_Origin);
        if (dist < 500.0f)
            continue;
            
        valid_areas.push_back(&area);
    }

    // No valid areas found
    if (valid_areas.empty())
    {
        // If we failed too many times in a row, clear visited areas
        if (++consecutive_fails >= 3)
        {
            visited_areas.clear();
            consecutive_fails = 0;
        }
        return false;
    }

    // Reset fail counter since we found valid areas
    consecutive_fails = 0;

    // Different strategies for area selection
    std::vector<CNavArea*> potential_targets;
    
    // Strategy 1: Try to find areas that are far from current position
    for (auto area : valid_areas)
    {
        float dist = area->m_center.DistTo(g_pLocalPlayer->v_Origin);
        if (dist > 2000.0f)
            potential_targets.push_back(area);
    }
    
    // Strategy 2: If no far areas found, try areas that are at medium distance
    if (potential_targets.empty())
    {
        for (auto area : valid_areas)
        {
            float dist = area->m_center.DistTo(g_pLocalPlayer->v_Origin);
            if (dist > 1000.0f && dist <= 2000.0f)
                potential_targets.push_back(area);
        }
    }
    
    // Strategy 3: If still no areas found, use any valid area
    if (potential_targets.empty())
        potential_targets = valid_areas;

    // Shuffle the potential targets to add randomness
    for (int i = potential_targets.size() - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        std::swap(potential_targets[i], potential_targets[j]);
    }

    // Try to path to potential targets
    for (auto area : potential_targets)
    {
        if (navparser::NavEngine::navTo(area->m_center, patrol))
        {
            current_target = area;
            visited_areas.push_back(area);
            return true;
        }
    }

    return false;
}

// Run away from dangerous areas
bool escapeDanger()
{
    if (!escape_danger)
        return false;
        
    // YOLO mode - don't escape at all
    if (*yolo_mode)
        return false;
        
    // YOLO mode for RAGE - check if there's a RAGE player alive
    if (*yolo_mode_rage)
    {
        bool rage_player_alive = false;
        for (int i = 1; i <= g_IEngine->GetMaxClients(); i++)
        {
            CachedEntity* ent = ENTITY(i);
            if (!ent || CE_BAD(ent) || !g_pPlayerResource->isAlive(i))
                continue;
                
            // Check if player is on enemy team and has RAGE status
            if (g_pPlayerResource->GetTeam(i) != g_pLocalPlayer->team)
            {
                auto &pl = playerlist::AccessData(ent);
                if (pl.state == playerlist::k_EState::RAGE)
                {
                    rage_player_alive = true;
                    break;
                }
            }
        }
        
        // If RAGE player is alive, don't escape
        if (rage_player_alive)
            return false;
    }
    
    // Don't escape while we have the intel
    if (!escape_danger_ctf_cap)
    {
        auto flag_carrier = flagcontroller::getCarrier(g_pLocalPlayer->team);
        if (flag_carrier == LOCAL_E)
            return false;
    }
    // Priority too high
    if (navparser::NavEngine::current_priority > danger)
        return false;

    auto *local_nav = navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin);
    auto blacklist  = navparser::NavEngine::getFreeBlacklist();

    // Check if we're in spawn - if so, ignore danger and focus on getting out
    if (local_nav && (local_nav->m_TFattributeFlags & TF_NAV_SPAWN_ROOM_RED || local_nav->m_TFattributeFlags & TF_NAV_SPAWN_ROOM_BLUE))
        return false;

    // In danger, try to run (besides if it's a building spot, don't run away from that)
    if (blacklist->find(local_nav) != blacklist->end())
    {
        if ((*blacklist)[local_nav].value == navparser::BlacklistReason_enum::BAD_BUILDING_SPOT)
            return false;

        static CNavArea *target_area = nullptr;
        // Already running and our target is still valid
        if (navparser::NavEngine::current_priority == danger && blacklist->find(target_area) == blacklist->end())
            return true;

        std::vector<CNavArea *> nav_areas_ptr;
        // Copy a ptr list (sadly cat_nav_init exists so this cannot be only done once)
        for (auto &nav_area : navparser::NavEngine::getNavFile()->m_areas)
            nav_areas_ptr.push_back(&nav_area);

        // Sort by distance
        std::sort(nav_areas_ptr.begin(), nav_areas_ptr.end(), [](CNavArea *a, CNavArea *b) { return a->m_center.DistToSqr(g_pLocalPlayer->v_Origin) < b->m_center.DistToSqr(g_pLocalPlayer->v_Origin); });

        int calls = 0;
        // Try to path away
        for (auto area : nav_areas_ptr)
        {
            if (blacklist->find(area) == blacklist->end())
            {
                // only try the 5 closest valid areas though, something is wrong if this fails
                calls++;
                if (calls > 5)
                    break;
                if (navparser::NavEngine::navTo(area->m_center, danger))
                {
                    target_area = area;
                    return true;
                }
            }
        }
    }
    // No longer in danger
    else if (navparser::NavEngine::current_priority == danger)
        navparser::NavEngine::cancelPath();
    return false;
}

bool escapeSpawn()
{
    static Timer spawn_escape_cooldown{};
    
    // Don't try too often
    if (!spawn_escape_cooldown.test_and_set(500))
        return navparser::NavEngine::current_priority == escape_spawn;
        
    auto *local_nav = navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin);
    if (!local_nav)
        return false;
        
    // Check if we're in spawn
    bool in_spawn = local_nav->m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE);
    
    // Cancel if we're not in spawn and this is running
    if (!in_spawn && navparser::NavEngine::current_priority == escape_spawn)
    {
        navparser::NavEngine::cancelPath();
        return false;
    }
    
    // Not in spawn, don't try
    if (!in_spawn)
        return false;
        
    // Try to find an exit
    for (auto &nav_area : navparser::NavEngine::getNavFile()->m_areas)
    {
        // Skip spawn areas
        if (nav_area.m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE))
            continue;
            
        // Try to get there
        if (navparser::NavEngine::navTo(nav_area.m_center, escape_spawn))
            return true;
    }
    
    return false;
}

static int slot = primary;

static void autoJump(std::pair<CachedEntity *, float> &nearest)
{
    if (!autojump)
        return;
    static Timer last_jump{};
    if (!last_jump.test_and_set(200) || CE_BAD(nearest.first))
        return;

    if (nearest.second <= *jump_distance)
        current_user_cmd->buttons |= IN_JUMP | IN_DUCK;
}

static slots getBestSlot(slots active_slot, std::pair<CachedEntity *, float> &nearest)
{
    if (force_slot)
        return (slots) *force_slot;
    switch (g_pLocalPlayer->clazz)
    {
    case tf_scout:
    case tf_heavy:
        return primary;
    case tf_medic:
        return secondary;
    case tf_spy:
    {
        if (nearest.second > 200 && active_slot == primary)
            return active_slot;
        else if (nearest.second >= 250)
            return primary;
        else
            return melee;
    }
    case tf_sniper:
    {
        // Have a Huntsman, Always use primary
        if (HasWeapon(LOCAL_E, 56) || HasWeapon(LOCAL_E, 1005) || HasWeapon(LOCAL_E, 1092))
            return primary;

        if (nearest.second <= 200.0f && !IsPlayerInvulnerable(nearest.first) && nearest.first->IsVisible())
            return melee;
        else if (nearest.second <= 300 && nearest.first->m_iHealth() < 75)
            return secondary;
        else if (nearest.second <= 400 && nearest.first->m_iHealth() < 75)
            return active_slot;
        else
            return primary;
    }
    case tf_pyro:
    {
        if (nearest.second > 450 && active_slot == secondary)
            return active_slot;
        else if (nearest.second <= 550)
            return primary;
        else
            return secondary;
    }
    case tf_soldier:
    {
        if (nearest.second <= 200)
            return secondary;
        else if (nearest.second <= 300)
            return active_slot;
        else
            return primary;
    }
    case tf_engineer:
    {
        if (((CE_GOOD(mySentry) && mySentry->m_flDistance() <= 300) || (CE_GOOD(myDispenser) && myDispenser->m_flDistance() <= 500)) || (current_building_spot.IsValid() && current_building_spot.DistTo(g_pLocalPlayer->v_Origin) <= 500.0f))
        {
            if (active_slot >= melee && navparser::NavEngine::current_priority != prio_melee)
                return active_slot;
            else
                return melee;
        }
        else if (nearest.second <= 500)
            return primary;
        else
            return secondary;
    }
    default:
    {
        if (nearest.second <= 400)
            return secondary;
        else if (nearest.second <= 500)
            return active_slot;
        else
            return primary;
    }
    }
}

static void updateSlot(std::pair<CachedEntity *, float> &nearest)
{
    static Timer slot_timer{};
    if ((!force_slot && !primary_only) || !slot_timer.test_and_set(300))
        return;
    if (CE_GOOD(LOCAL_E) && !HasCondition<TFCond_HalloweenGhostMode>(LOCAL_E) && CE_GOOD(LOCAL_W) && LOCAL_E->m_bAlivePlayer())
    {
        IClientEntity *weapon = RAW_ENT(LOCAL_W);
        if (re::C_BaseCombatWeapon::IsBaseCombatWeapon(weapon))
        {
            slot        = re::C_BaseCombatWeapon::GetSlot(weapon) + 1;
            int newslot = getBestSlot(static_cast<slots>(slot), nearest);
            if (slot != newslot)
                g_IEngine->ClientCmd_Unrestricted(format("slot", newslot).c_str());
        }
    }
}

// Check if a position is safe from stickies and projectiles
bool isPositionSafe(Vector pos)
{
    if (!avoid_stickies && !avoid_projectiles)
        return true;

    for (auto const &ent : entity_cache::valid_ents)
    {
        if (!ent)
            continue;

        // Check for stickies
        if (avoid_stickies && ent->m_iClassID() == CL_CLASS(CTFGrenadePipebombProjectile))
        {
            // Skip non-sticky projectiles
            if (CE_INT(ent, netvar.iPipeType) != 1)
                continue;

            float dist = ent->m_vecOrigin().DistTo(pos);
            if (dist < *sticky_danger_range)
                return false;
        }
        
        // Check for rockets and pipes
        if (avoid_projectiles)
        {
            if (ent->m_iClassID() == CL_CLASS(CTFProjectile_Rocket) || 
                (ent->m_iClassID() == CL_CLASS(CTFGrenadePipebombProjectile) && CE_INT(ent, netvar.iPipeType) == 0))
            {
                float dist = ent->m_vecOrigin().DistTo(pos);
                if (dist < *projectile_danger_range)
                    return false;
            }
        }
    }
    return true;
}

// Find safe position to escape to
bool escapeProjectiles()
{
    if (!avoid_stickies && !avoid_projectiles)
        return false;

    // Don't interrupt higher priority tasks
    if (navparser::NavEngine::current_priority > danger)
        return false;

    // Check if current position is unsafe
    if (isPositionSafe(g_pLocalPlayer->v_Origin))
    {
        if (navparser::NavEngine::current_priority == danger)
            navparser::NavEngine::cancelPath();
        return false;
    }

    // Find safe nav areas sorted by distance
    std::vector<std::pair<CNavArea*, float>> safe_areas;
    auto *local_nav = navparser::NavEngine::findClosestNavSquare(g_pLocalPlayer->v_Origin);
    
    if (!local_nav)
        return false;

    for (auto &area : navparser::NavEngine::getNavFile()->m_areas)
    {
        // Skip current area
        if (&area == local_nav)
            continue;
            
        // Skip if area is blacklisted
        if (navparser::NavEngine::getFreeBlacklist()->find(&area) != navparser::NavEngine::getFreeBlacklist()->end())
            continue;

        if (isPositionSafe(area.m_center))
        {
            float dist = area.m_center.DistTo(g_pLocalPlayer->v_Origin);
            safe_areas.push_back({&area, dist});
        }
    }

    // Sort by distance
    std::sort(safe_areas.begin(), safe_areas.end(),
        [](const std::pair<CNavArea*, float> &a, const std::pair<CNavArea*, float> &b) {
            return a.second < b.second;
        });

    // Try to path to closest safe area
    for (auto &area : safe_areas)
    {
        if (navparser::NavEngine::navTo(area.first->m_center, danger))
            return true;
    }

    return false;
}

static void CreateMove()
{
    if (!enabled || !navparser::NavEngine::isReady())
        return;
    if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer() || HasCondition<TFCond_HalloweenGhostMode>(LOCAL_E))
        return;

    refreshSniperSpots();
    refreshLocalBuildings();
    refreshBuildingSpots();

    if (danger_config_custom)
    {
        selected_config = { *danger_config_custom_min_full_danger, *danger_config_custom_min_slight_danger, *danger_config_custom_max_slight_danger, *danger_config_custom_prefer_far };
    }
    else
    {
        // Update the distance config
        switch (g_pLocalPlayer->clazz)
        {
        case tf_scout:
        case tf_heavy:
            selected_config = CONFIG_SHORT_RANGE;
            break;
        case tf_engineer:
            selected_config = isEngieMode() ? HasGunslinger(LOCAL_E) ? CONFIG_GUNSLINGER_ENGINEER : CONFIG_ENGINEER : CONFIG_SHORT_RANGE;
            break;
        case tf_sniper:
            selected_config = g_pLocalPlayer->weapon()->m_iClassID() == CL_CLASS(CTFCompoundBow) ? CONFIG_MID_RANGE : CONFIG_LONG_RANGE;
            break;
        default:
            selected_config = CONFIG_MID_RANGE;
        }
    }

    auto nearest = getNearestPlayerDistance();

    updateSlot(nearest);
    updateEnemyBlacklist(slot);

    // First priority should be getting out of spawn
    if (escapeSpawn())
        return;
    // Attack people with melee
    if (meleeAttack(slot, nearest))
        return;
    // Try to escape from projectiles (higher priority than normal danger)
    if (escapeProjectiles())
        return;
    // Try to escape danger
    if (escapeDanger())
        return;
    // Second priority should be getting health
    if (getHealth())
        return;
    // If we aren't getting health, get ammo
    if (getAmmo())
        return;
    // Try to capture objectives
    if (captureObjectives())
        return;
    // Try to get spells
    if (getSpells())
        return;
    // Try to get powerups
    if (getPowerups())
        return;
    // Try to get gargoyles
    if (getGargoyles())
        return;
    if (runEngineerLogic())
        return;
    // Try to snipe sentries
    if (snipeSentries())
        return;
    // Try to stalk enemies
    if (stayNear())
        return;
    // Try to get health with a lower priority
    if (getHealth(true))
        return;
    // We have nothing else to do, roam
    if (doRoam())
        return;
}

void LevelInit()
{
    // Make it run asap
    refresh_sniperspots_timer.last -= std::chrono::seconds(60);
    sniper_spots.clear();
    is_doomsday = false;

    // Doomsday sucks
    // TODO: add proper doomsday implementation
    auto map_name = std::string(g_IEngine->GetLevelName());
    if (g_IEngine->GetLevelName() && map_name.find("sd_doomsday") != map_name.npos)
        is_doomsday = true;
}
#if ENABLE_VISUALS
void Draw()
{
    if (!draw_danger || !navparser::NavEngine::isReady())
        return;
    for (auto &area : slight_danger_drawlist_normal)
    {
        Vector out;
        if (draw::WorldToScreen(area, out))
            draw::Rectangle(out.x - 2.0f, out.y - 2.0f, 4.0f, 4.0f, colors::orange);
    }
    for (auto &area : slight_danger_drawlist_dormant)
    {
        Vector out;
        if (draw::WorldToScreen(area, out))
            draw::Rectangle(out.x - 2.0f, out.y - 2.0f, 4.0f, 4.0f, colors::orange);
    }
    for (auto &area : *navparser::NavEngine::getFreeBlacklist())
    {
        Vector out;

        if (draw::WorldToScreen(area.first->m_center, out))
            draw::Rectangle(out.x - 2.0f, out.y - 2.0f, 4.0f, 4.0f, colors::red);
    }
}
#endif

static InitRoutine init(
    []()
    {
        EC::Register(EC::CreateMove, CreateMove, "navbot_cm");
        EC::Register(EC::CreateMoveWarp, CreateMove, "navbot_cm");
        EC::Register(EC::LevelInit, LevelInit, "navbot_levelinit");
#if ENABLE_VISUALS
        EC::Register(EC::Draw, Draw, "navbot_draw");
#endif
        LevelInit();
    });

} // namespace hacks::tf2::NavBot
