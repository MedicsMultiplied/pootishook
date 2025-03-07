/*
    This file is part of Cathook.

    Cathook is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Cathook is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Cathook. If not, see <https://www.gnu.org/licenses/>.
*/

#include <PlayerTools.hpp>
#include "common.hpp"
#include "HookedMethods.hpp"
#include "MiscTemporary.hpp"
#include "hacks/Backtrack.hpp"
#include "EffectGlow.hpp"
#include "Aimbot.hpp"
#include "hacks/AntiAim.hpp"
#include <algorithm>

namespace hooked_methods {

/* Core settings */
static settings::Boolean enable{ "chams.enable", "false" };
static settings::Boolean legit{ "chams.legit", "false" };

/* Player settings */
static settings::Boolean players{ "chams.show.players", "true" };
static settings::Boolean teammates{ "chams.show.teammates", "false" };
static settings::Boolean chamsself{ "chams.self", "true" };
static settings::Float player_alpha{ "chams.player.alpha", "1" };

/* Building settings */
static settings::Boolean buildings{ "chams.show.buildings", "true" };
static settings::Boolean buildings_enemy{ "chams.building.enemy", "true" };
static settings::Boolean buildings_team{ "chams.building.team", "false" };
static settings::Float building_alpha{ "chams.building.alpha", "1" };

/* Projectile settings */
static settings::Boolean projectiles{ "chams.show.projectiles", "true" };
static settings::Boolean projectiles_enemy{ "chams.projectile.enemy", "true" };
static settings::Boolean projectiles_team{ "chams.projectile.team", "false" };

/* Viewmodel settings */
static settings::Boolean viewmodel_hands{ "chams.viewmodel.hands", "false" };
static settings::Boolean viewmodel_weapon{ "chams.viewmodel.weapon", "false" };
static settings::Boolean weapons_original{ "chams.weapons.original", "true" };
static settings::Boolean no_arms{ "remove.arms", "false" };

/* World settings */
static settings::Boolean world_objective{ "chams.world.objective", "false" };
static settings::Boolean world_npcs{ "chams.world.npcs", "false" };
static settings::Boolean world_pickups{ "chams.world.pickups", "false" };
static settings::Boolean world_powerups{ "chams.world.powerups", "false" };
static settings::Boolean world_halloween{ "chams.world.halloween", "false" };
static settings::Boolean world_bombs{ "chams.world.bombs", "false" };

/* Backtrack settings */
static settings::Boolean backtrack_enabled{ "chams.backtrack.enabled", "false" };
static settings::Int backtrack_draw{ "chams.backtrack.draw", "0" };
static settings::Rgba backtrack_color{ "chams.backtrack.color", "ff00ffff" };

/* Fake angle settings */
static settings::Boolean fakeangle_enabled{ "chams.fakeangle.enabled", "false" };
static settings::Rgba fakeangle_color{ "chams.fakeangle.color", "ff00ffff" };

/* Material settings */
static settings::Int chams_type{ "chams.type", "0" };
static settings::Boolean phong_enable{ "chams.phong", "true" };
static settings::Int phong_boost{ "chams.phongboost", "2" };
static settings::Float envmap_tint{ "chams.envmaptint", "0" };
static settings::Float material_force_rimlight{ "chams.rimlight", "0" };

/* Team colors */
static settings::Rgba player_enemy_visible{ "chams.player.enemy.visible", "ff0000ff" };
static settings::Rgba player_enemy_occluded{ "chams.player.enemy.occluded", "ff8800ff" };
static settings::Rgba player_team_visible{ "chams.player.team.visible", "0000ffff" };
static settings::Rgba player_team_occluded{ "chams.player.team.occluded", "bc00ffff" };

static settings::Rgba building_enemy_visible{ "chams.building.enemy.visible", "ff0000ff" };
static settings::Rgba building_enemy_occluded{ "chams.building.enemy.occluded", "ff8800ff" };
static settings::Rgba building_team_visible{ "chams.building.team.visible", "0000ffff" };
static settings::Rgba building_team_occluded{ "chams.building.team.occluded", "bc00ffff" };

static settings::Rgba projectile_enemy_visible{ "chams.projectile.enemy.visible", "ff0000ff" };
static settings::Rgba projectile_enemy_occluded{ "chams.projectile.enemy.occluded", "ff8800ff" };
static settings::Rgba projectile_team_visible{ "chams.projectile.team.visible", "0000ffff" };
static settings::Rgba projectile_team_occluded{ "chams.projectile.team.occluded", "bc00ffff" };

static settings::Rgba viewmodel_hands_color{ "chams.viewmodel.hands.color", "ffffffff" };
static settings::Rgba viewmodel_weapon_color{ "chams.viewmodel.weapon.color", "ffffffff" };

class ChamsMaterial {
public:
    IMaterial* material;
    bool created;
    bool ignore_z;
    
    ChamsMaterial() : material(nullptr), created(false), ignore_z(false) {}
    
    void Shutdown() {
        if (material) {
            material->DecrementReferenceCount();
            material = nullptr;
        }
        created = false;
    }

    bool Create(bool ignorez) {
        if (!g_IMaterialSystem)
            return false;

        ignore_z = ignorez;
        const char* material_type = (*chams_type == 5) ? "UnlitGeneric" : "VertexLitGeneric";
        
        KeyValues* kv = new KeyValues(material_type);
        if (!kv)
            return false;

        // Base material setup
        kv->SetString("$basetexture", "vgui/white");
        kv->SetInt("$model", 1);
        
        if (ignorez)
            kv->SetInt("$ignorez", 1);
            
        if (*chams_type != 5) {
            kv->SetInt("$nocull", 1);
            kv->SetInt("$halflambert", 1);
        }

        // Material type specific settings
        switch (*chams_type) {
        case 1: // Flat
            kv->SetInt("$phong", 0);
            kv->SetInt("$basemapalphaphongmask", 0);
            kv->SetInt("$normalmapalphaenvmapmask", 0);
            break;
            
        case 2: // Shaded
            kv->SetInt("$phong", 1);
            kv->SetFloat("$phongexponent", 15);
            kv->SetFloat("$phongboost", *phong_boost);
            kv->SetString("$envmap", "");
            kv->SetString("$phongfresnelranges", "[.5 .5 1]");
            break;
            
        case 3: // Glossy
            kv->SetInt("$phong", 1);
            kv->SetFloat("$phongexponent", 30);
            kv->SetFloat("$phongboost", *phong_boost);
            kv->SetString("$envmap", "env_cubemap");
            kv->SetFloat("$envmaptint", *envmap_tint);
            kv->SetString("$phongfresnelranges", "[.5 .5 1]");
            break;
            
        case 4: // Glow
            kv->SetInt("$additive", 1);
            kv->SetString("$envmap", "models/effects/cube_white");
            kv->SetInt("$selfillum", 1);
            kv->SetString("$selfillumtint", "[1 1 1]");
            kv->SetFloat("$selfillumboost", 2.0f);
            break;
            
        case 5: // Wireframe
            kv->SetInt("$wireframe", 1);
            break;
            
        default: // Normal
            if (*phong_enable) {
                kv->SetInt("$phong", 1);
                kv->SetFloat("$phongexponent", 15);
                kv->SetFloat("$phongboost", *phong_boost);
                kv->SetString("$phongfresnelranges", "[.5 .5 1]");
            }
            break;
        }

        char material_name[128];
        snprintf(material_name, sizeof(material_name), "__cathook_chams_%s_%d", ignorez ? "ignorez" : "regular", *chams_type);
        
        material = g_IMaterialSystem->CreateMaterial(material_name, kv);
        kv->deleteThis();
        
        if (!material || material->IsErrorMaterial()) {
            material = nullptr;
            return false;
        }

        created = true;
        return true;
    }
};

static ChamsMaterial mat_regular;
static ChamsMaterial mat_ignorez;
static int last_chams_type = -1;

static void CreateMaterials() {
    if (!g_IMaterialSystem)
        return;
        
    if (last_chams_type == *chams_type && mat_regular.created && mat_ignorez.created)
        return;
        
    // Safely clean up existing materials first
    mat_regular.Shutdown();
    mat_ignorez.Shutdown();
    
    // Only create new materials if chams is enabled
    if (!enable)
        return;
        
    bool success = mat_regular.Create(false) && mat_ignorez.Create(true);
    if (!success) {
        mat_regular.Shutdown();
        mat_ignorez.Shutdown();
        return;
    }
    
    last_chams_type = *chams_type;
}

static bool ShouldRenderChams(IClientEntity* entity) {
    if (!entity || !enable)
        return false;
        
    int idx = entity->entindex();
    if (idx < 0)
        return false;
        
    CachedEntity* ent = ENTITY(idx);
    if (!ent || CE_BAD(ent))
        return false;
        
    if (ent && LOCAL_E && ent->m_IDX == LOCAL_E->m_IDX)
        return *chamsself;

    switch (ent->m_Type()) {
    case ENTITY_BUILDING:
        if (!buildings)
            return false;
        if (!buildings_enemy && ent->m_bEnemy())
            return false;
        if (!buildings_team && !ent->m_bEnemy())
            return false;
        if (ent->m_iHealth() == 0 || !ent->m_iHealth())
            return false;
        return true;
    case ENTITY_PLAYER:
        if (!players)
            return false;
        if (!teammates && !ent->m_bEnemy())
            return false;
        if (CE_BYTE(ent, netvar.iLifeState))
            return false;
        return true;
    case ENTITY_PROJECTILE:
        if (!projectiles)
            return false;
        if (!projectiles_enemy && ent->m_bEnemy())
            return false;
        if (!projectiles_team && !ent->m_bEnemy())
            return false;
        return true;
    default:
        break;
    }

    // Handle world entities
    const char* model_name = g_IModelInfo->GetModelName(ent->InternalEntity()->GetModel());
    if (!model_name)
        return false;

    if (world_objective && strstr(model_name, "flagpole"))
        return true;
    if (world_npcs && (strstr(model_name, "boss") || strstr(model_name, "merasmus") || strstr(model_name, "zombie")))
        return true;
    if (world_pickups && (strstr(model_name, "ammo") || strstr(model_name, "medkit")))
        return true;
    if (world_powerups && strstr(model_name, "powerup"))
        return true;
    if (world_halloween && strstr(model_name, "halloween"))
        return true;
    if (world_bombs && strstr(model_name, "bomb"))
        return true;

    return false;
}

static rgba_t GetEntityColor(IClientEntity* entity, bool ignorez) {
    if (!entity)
        return colors::white;
        
    try {
        CachedEntity* ent = ENTITY(entity->entindex());
        if (CE_BAD(ent))
            return colors::white;

        bool is_player = ent->m_Type() == ENTITY_PLAYER;
        bool is_building = ent->m_Type() == ENTITY_BUILDING;
        bool is_projectile = ent->m_Type() == ENTITY_PROJECTILE;
        bool is_enemy = ent->m_bEnemy();

        if (is_player) {
            return is_enemy ? 
                (ignorez ? *player_enemy_occluded : *player_enemy_visible) :
                (ignorez ? *player_team_occluded : *player_team_visible);
        }
        else if (is_building) {
            return is_enemy ?
                (ignorez ? *building_enemy_occluded : *building_enemy_visible) :
                (ignorez ? *building_team_occluded : *building_team_visible);
        }
        else if (is_projectile) {
            return is_enemy ?
                (ignorez ? *projectile_enemy_occluded : *projectile_enemy_visible) :
                (ignorez ? *projectile_team_occluded : *projectile_team_visible);
        }
    } catch (...) {}

    return colors::white;
}

static void ApplyChams(IClientEntity* entity, bool ignorez, const DrawModelState_t& state, const ModelRenderInfo_t& info, matrix3x4_t* bone) {
    if (!g_IVRenderView || !g_IVModelRender || !g_IMaterialSystem || !entity)
        return;
        
    IMaterial* material = ignorez ? mat_ignorez.material : mat_regular.material;
    if (!material || material->IsErrorMaterial())
        return;

    // Save the original color modulation and blend
    float original_color[3] = { 0.0f, 0.0f, 0.0f };
    float original_blend = 0.0f;
    
    try {
        g_IVRenderView->GetColorModulation(original_color);
        original_blend = g_IVRenderView->GetBlend();
    } catch (...) {
        return;
    }

    try {
        rgba_t color = GetEntityColor(entity, ignorez);
        auto cc = entity->GetClientClass();
        if (!cc)
            return;
        float alpha = (cc->m_ClassID == RCC_PLAYER ? *player_alpha : *building_alpha);
        
        // Clamp alpha to valid range
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        
        // Set color with safety checks
        float new_color[3] = { 
            std::clamp(color.r / 255.0f, 0.0f, 1.0f),
            std::clamp(color.g / 255.0f, 0.0f, 1.0f),
            std::clamp(color.b / 255.0f, 0.0f, 1.0f)
        };
        
        g_IVRenderView->SetColorModulation(new_color);
        g_IVRenderView->SetBlend(alpha);
        
        // Force material override
        g_IVModelRender->ForcedMaterialOverride(material);
        
        // Draw with original material
        original::DrawModelExecute(g_IVModelRender, state, info, bone);
        
        // Immediately restore material to prevent state leaks
        g_IVModelRender->ForcedMaterialOverride(nullptr);
        g_IVRenderView->SetColorModulation(original_color);
        g_IVRenderView->SetBlend(original_blend);
    } catch (...) {
        // Restore original state in case of any exception
        g_IVModelRender->ForcedMaterialOverride(nullptr);
        g_IVRenderView->SetColorModulation(original_color);
        g_IVRenderView->SetBlend(original_blend);
    }
}

static void RenderBacktrack(const DrawModelState_t& state, const ModelRenderInfo_t& info) {
    if (!backtrack_enabled)
        return;

    auto entity = g_IEntityList->GetClientEntity(info.entity_index);
    auto cc = (entity ? entity->GetClientClass() : nullptr);
    if (!entity || !cc || cc->m_ClassID != RCC_PLAYER)
        return;

    CachedEntity* ent = ENTITY(info.entity_index);
    if (CE_BAD(ent) || !ent->m_bAlivePlayer() || !ent->m_bEnemy())
        return;

    auto& records = hacks::tf2::backtrack::bt_data[info.entity_index];
    if (records.empty())
        return;

    IMaterial* material = mat_regular.material;
    if (!material || material->IsErrorMaterial())
        return;

    float original_color[3];
    float original_blend;
    g_IVRenderView->GetColorModulation(original_color);
    original_blend = g_IVRenderView->GetBlend();

    colors::rgba_t color = *backtrack_color;
    float color_array[3] = {
        std::clamp(color.r / 255.0f, 0.0f, 1.0f),
        std::clamp(color.g / 255.0f, 0.0f, 1.0f),
        std::clamp(color.b / 255.0f, 0.0f, 1.0f)
    };

    g_IVRenderView->SetColorModulation(color_array);
    g_IVRenderView->SetBlend(color.a / 255.0f);
    g_IVModelRender->ForcedMaterialOverride(material);

    switch (*backtrack_draw) {
    case 0: // Last
        if (!records.empty()) {
            auto& record = records.back();
            original::DrawModelExecute(g_IVModelRender, state, info, record.bones.data());
        }
        break;
    case 1: // First + Last
        if (!records.empty()) {
            auto& first = records.front();
            auto& last = records.back();
            original::DrawModelExecute(g_IVModelRender, state, info, first.bones.data());
            original::DrawModelExecute(g_IVModelRender, state, info, last.bones.data());
        }
        break;
    case 2: // All
        for (auto& record : records) {
            original::DrawModelExecute(g_IVModelRender, state, info, record.bones.data());
        }
        break;
    }

    g_IVModelRender->ForcedMaterialOverride(nullptr);
    g_IVRenderView->SetColorModulation(original_color);
    g_IVRenderView->SetBlend(original_blend);
}

static void RenderFakeAngle(const DrawModelState_t& state, const ModelRenderInfo_t& info, matrix3x4_t* bone) {
    if (!fakeangle_enabled || info.entity_index != g_IEngine->GetLocalPlayer())
        return;

    if (!LOCAL_E || CE_BAD(LOCAL_E))
        return;

    if (!hacks::shared::antiaim::isEnabled())
        return;

    if (!info.pRenderable || !info.pModel)
        return;

    IMaterial* material = mat_regular.material;
    if (!material || material->IsErrorMaterial())
        return;

    // Save original state
    float original_color[3] = { 0.0f, 0.0f, 0.0f };
    float original_blend = 0.0f;
    
    try {
        g_IVRenderView->GetColorModulation(original_color);
        original_blend = g_IVRenderView->GetBlend();
    } catch (...) {
        return;
    }

    try {
        colors::rgba_t color = *fakeangle_color;
        float color_array[3] = {
            std::clamp(color.r / 255.0f, 0.0f, 1.0f),
            std::clamp(color.g / 255.0f, 0.0f, 1.0f),
            std::clamp(color.b / 255.0f, 0.0f, 1.0f)
        };

        g_IVRenderView->SetColorModulation(color_array);
        g_IVRenderView->SetBlend(color.a / 255.0f);
        g_IVModelRender->ForcedMaterialOverride(material);

        // Draw the model with original bones
        original::DrawModelExecute(g_IVModelRender, state, info, bone);
    } catch (...) {}

    // Always restore original state
    g_IVModelRender->ForcedMaterialOverride(nullptr);
    g_IVRenderView->SetColorModulation(original_color);
    g_IVRenderView->SetBlend(original_blend);
}

DEFINE_HOOKED_METHOD(DrawModelExecute, void, IVModelRender* this_, const DrawModelState_t& state, const ModelRenderInfo_t& info, matrix3x4_t* bone) {
    if (!this_ || !g_IVModelRender || !g_IMaterialSystem || !g_IVRenderView || !isHackActive() || 
        effect_glow::g_EffectGlow.drawing || (*clean_screenshots && g_IEngine->IsTakingScreenshot()) || 
        disable_visuals || CE_BAD(LOCAL_E) || !enable) {
        return original::DrawModelExecute(this_, state, info, bone);
    }

    if (!info.pModel) {
        return original::DrawModelExecute(this_, state, info, bone);
    }

    IClientEntity* entity = g_IEntityList->GetClientEntity(info.entity_index);
    if (!entity) {
        return original::DrawModelExecute(this_, state, info, bone);
    }

    const char* name = g_IModelInfo->GetModelName(info.pModel);
    if (!name) {
        return original::DrawModelExecute(this_, state, info, bone);
    }

    try {
        // Create materials before any rendering
        CreateMaterials();

        // Save original state
        float original_color[3] = { 0.0f, 0.0f, 0.0f };
        float original_blend = 0.0f;
        g_IVRenderView->GetColorModulation(original_color);
        original_blend = g_IVRenderView->GetBlend();

        bool restore_needed = false;
        bool handled = false;

        // Handle arms
        if (strstr(name, "arms") || strstr(name, "c_engineer_gunslinger")) {
            if (no_arms)
                return;

            if (viewmodel_hands && mat_regular.material && !mat_regular.material->IsErrorMaterial()) {
                colors::rgba_t color = *viewmodel_hands_color;
                float color_array[3] = {
                    std::clamp(color.r / 255.0f, 0.0f, 1.0f),
                    std::clamp(color.g / 255.0f, 0.0f, 1.0f),
                    std::clamp(color.b / 255.0f, 0.0f, 1.0f)
                };
                g_IVRenderView->SetColorModulation(color_array);
                g_IVRenderView->SetBlend(color.a / 255.0f);
                g_IVModelRender->ForcedMaterialOverride(mat_regular.material);
                restore_needed = true;
                original::DrawModelExecute(this_, state, info, bone);
                handled = true;
            }
            if (!handled)
                original::DrawModelExecute(this_, state, info, bone);
            goto cleanup;
        }

        // Handle weapons
        if (strstr(name, "weapons/w_") || strstr(name, "weapons/c_")) {
            if (viewmodel_weapon && mat_regular.material && !mat_regular.material->IsErrorMaterial()) {
                colors::rgba_t color = *viewmodel_weapon_color;
                float color_array[3] = {
                    std::clamp(color.r / 255.0f, 0.0f, 1.0f),
                    std::clamp(color.g / 255.0f, 0.0f, 1.0f),
                    std::clamp(color.b / 255.0f, 0.0f, 1.0f)
                };
                g_IVRenderView->SetColorModulation(color_array);
                g_IVRenderView->SetBlend(color.a / 255.0f);
                g_IVModelRender->ForcedMaterialOverride(mat_regular.material);
                restore_needed = true;
                original::DrawModelExecute(this_, state, info, bone);
                handled = true;
            }
            if (!handled && weapons_original)
                original::DrawModelExecute(this_, state, info, bone);
            goto cleanup;
        }

        // Handle backtrack and fake angle
        {
            auto cc = (entity ? entity->GetClientClass() : nullptr);
            if (cc && cc->m_ClassID == RCC_PLAYER) {
                RenderBacktrack(state, info);
                RenderFakeAngle(state, info, bone);
            }
        }

        if (!ShouldRenderChams(entity)) {
            original::DrawModelExecute(this_, state, info, bone);
            goto cleanup;
        }

        if (!legit && mat_ignorez.material && !mat_ignorez.material->IsErrorMaterial())
            ApplyChams(entity, true, state, info, bone);
        if (mat_regular.material && !mat_regular.material->IsErrorMaterial())
            ApplyChams(entity, false, state, info, bone);

cleanup:
        if (restore_needed) {
            g_IVModelRender->ForcedMaterialOverride(nullptr);
            g_IVRenderView->SetColorModulation(original_color);
            g_IVRenderView->SetBlend(original_blend);
        }
    } catch (...) {
        g_IVModelRender->ForcedMaterialOverride(nullptr);
        original::DrawModelExecute(this_, state, info, bone);
    }
}

static InitRoutine init([]() {
    EC::Register(EC::LevelShutdown, []() {
        mat_regular.Shutdown();
        mat_ignorez.Shutdown();
        last_chams_type = -1;
    }, "dme_lvl_shutdown");

    EC::Register(EC::Shutdown, []() {
        mat_regular.Shutdown();
        mat_ignorez.Shutdown();
        last_chams_type = -1;
    }, "dme_shutdown");

    phong_enable.installChangeCallback([](settings::VariableBase<bool>&, bool) {
        last_chams_type = -1;
    });
    phong_boost.installChangeCallback([](settings::VariableBase<int>&, int) {
        last_chams_type = -1;
    });
    chams_type.installChangeCallback([](settings::VariableBase<int>&, int) {
        last_chams_type = -1;
    });
    envmap_tint.installChangeCallback([](settings::VariableBase<float>&, float) {
        last_chams_type = -1;
    });
    material_force_rimlight.installChangeCallback([](settings::VariableBase<float>&, float) {
        last_chams_type = -1;
    });
});

} // namespace hooked_methods
