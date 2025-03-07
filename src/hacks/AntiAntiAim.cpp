/*
 * Created on 29.07.18.
 */

#include "common.hpp"
#include "hacks/AntiAntiAim.hpp"
#include "sdk/dt_recv_redef.h"


void VectorAngles(const Vector &forward, QAngle &angles)
{
    float tmp, yaw, pitch;

    if (forward[1] == 0 && forward[0] == 0)
    {
        yaw = 0;
        if (forward[2] > 0)
            pitch = 270;
        else
            pitch = 90;
    }
    else
    {
        yaw = (atan2(forward[1], forward[0]) * 180 / M_PI);
        if (yaw < 0)
            yaw += 360;

        tmp = sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
        pitch = (atan2(-forward[2], tmp) * 180 / M_PI);
        if (pitch < 0)
            pitch += 360;
    }

    angles[0] = pitch;
    angles[1] = yaw;
    angles[2] = 0;
}

namespace hacks::shared::anti_anti_aim
{
static settings::Boolean enable{ "anti-anti-aim.enable", "false" };
static settings::Boolean debug{ "anti-anti-aim.debug.enable", "false" };
static settings::Int resolver_mode{ "anti-anti-aim.resolver.mode", "0" };
static settings::Int min_hits{ "anti-anti-aim.resolver.min-hits", "2" };

boost::unordered_flat_map<unsigned, brutedata> resolver_map;
std::array<CachedEntity *, 32> sniperdot_array;

// Add resolver data structure
struct PlayerResolverData {
    bool resolved;
    float resolved_yaw;
    bool is_fake;
    int resolve_type;  // 0 = none, 1 = legit aa, 2 = blatant aa
    float original_yaw;
    float last_delta;
    int missed_shots;
    float last_simtime;
    float avg_delta;
    int delta_samples;
    bool has_hit;
    float last_eye_yaw;
    float last_real_yaw;
    float last_velocity_yaw;
    int aa_type; // 0 = none, 1 = static, 2 = jitter, 3 = spin, 4 = random
    int shots_hit;
    int shots_fired;
    float hit_accuracy;
    PlayerResolverData() : resolved(false), resolved_yaw(0.0f), is_fake(false), resolve_type(0), 
                          original_yaw(0.0f), last_delta(0.0f), missed_shots(0), last_simtime(0.0f),
                          avg_delta(0.0f), delta_samples(0), has_hit(false), last_eye_yaw(0.0f),
                          last_real_yaw(0.0f), last_velocity_yaw(0.0f), aa_type(0), shots_hit(0),
                          shots_fired(0), hit_accuracy(0.0f) {}
};
static std::array<PlayerResolverData, MAX_PLAYERS> player_resolver_data;

static inline void modifyAngles()
{
    if (!g_IEngine->IsInGame())
        return;
        
    for (auto const &player : entity_cache::player_cache)
    {
        if (!player || CE_BAD(player) || !player->m_bAlivePlayer() || !player->m_bEnemy())
            continue;
            
        // Add null check for player info
        if (!player->player_info || !player->player_info->friendsID)
            continue;
            
        // Add bounds check
        if (player->m_IDX <= 0 || player->m_IDX >= MAX_PLAYERS)
            continue;
            
        auto &data = resolver_map[player->player_info->friendsID];
        if (CE_BAD(player))
            continue;
            
        auto &angle = CE_VECTOR(player, netvar.m_angEyeAngles);
        if (std::isnan(data.new_angle.x) || std::isnan(data.new_angle.y))
            continue;
            
        angle.x = data.new_angle.x;
        angle.y = data.new_angle.y;
    }
}
static inline void CreateMove()
{
    // Empty the array
    sniperdot_array.fill(0);
    // Find sniper dots
    for (auto &dot_ent : entity_cache::valid_ents)
    {

        // Not a sniper dot
        if (dot_ent->m_iClassID() != CL_CLASS(CSniperDot))
            continue;
        // Get the player it belongs to
        auto ent_idx = HandleToIDX(CE_INT(dot_ent, netvar.m_hOwnerEntity));
        // IDX check
        if (IDX_BAD(ent_idx) || ent_idx > sniperdot_array.size() || ent_idx <= 0)
            continue;
        // Good sniper dot, add to array
        sniperdot_array.at(ent_idx - 1) = dot_ent;
    }
}

void frameStageNotify(ClientFrameStage_t stage)
{
#if !ENABLE_TEXTMODE
    if (!enable || !g_IEngine->IsInGame())
        return;
    if (stage == FRAME_NET_UPDATE_POSTDATAUPDATE_START)
    {
        modifyAngles();
    }
#endif
}

static std::array<float, 8> yaw_resolves{ 0.0f, 180.0f, 90.0f, -90.0f, 45.0f, -45.0f, 135.0f, -135.0f };


bool IsAntiAiming(CachedEntity* entity) {
    if (!entity || entity->m_Type() != ENTITY_PLAYER || entity->m_IDX <= 0 || entity->m_IDX >= MAX_PLAYERS)
        return false;

    auto& data = player_resolver_data[entity->m_IDX];
    float simtime = CE_FLOAT(entity, netvar.m_flSimulationTime);
    
    if (simtime == data.last_simtime)
        return data.is_fake;
        
    data.last_simtime = simtime;
    
    // Get current angles with safety check
    if (CE_BAD(entity))
        return false;
        
    Vector angles = CE_VECTOR(entity, netvar.m_angEyeAngles);
    if (std::isnan(angles.x) || std::isnan(angles.y))
        return false;
        
    float curr_pitch = angles.x;
    float curr_yaw = angles.y;
    
    // Check for rage anti-aim first (pitch check)
    if (curr_pitch <= -89.0f || curr_pitch >= 89.0f)
    {
        data.is_fake = true;
        data.resolve_type = 2; // Rage AA
        data.aa_type = 2; // Mark as rage
        return true;
    }
    
    // Normalize yaw
    while (curr_yaw > 180) curr_yaw -= 360;
    while (curr_yaw < -180) curr_yaw += 360;

    // Calculate yaw delta
    float delta = std::abs(curr_yaw - data.original_yaw);
    if (delta > 180) delta = 360 - delta;
    
    // Update average delta with decay
    if (data.delta_samples < 10) {
        data.avg_delta = (data.avg_delta * data.delta_samples + delta) / (data.delta_samples + 1);
        data.delta_samples++;
    } else {
        data.avg_delta = data.avg_delta * 0.9f + delta * 0.1f;
    }
    
    data.original_yaw = curr_yaw;
    data.last_delta = delta;

    // Detect rage anti-aim patterns first
    if (data.avg_delta > 90.0f && data.delta_samples >= 3) {
        data.is_fake = true;
        data.resolve_type = 2; // Rage AA
        data.aa_type = 2; // Jitter/Spin
        return true;
    }
    
    // Detect spin (another form of rage AA)
    static float last_angles[MAX_PLAYERS];
    static int spin_count[MAX_PLAYERS];
    
    if (std::abs(curr_yaw - last_angles[entity->m_IDX]) > 30.0f) {
        if (++spin_count[entity->m_IDX] >= 3) {
            data.is_fake = true;
            data.resolve_type = 2;
            data.aa_type = 3; // Spin
            return true;
        }
    } else {
        spin_count[entity->m_IDX] = 0;
    }
    
    last_angles[entity->m_IDX] = curr_yaw;

    // Only check for legit AA if no rage AA detected
    if (!data.is_fake) {
        Vector velocity;
        velocity::EstimateAbsVelocity(RAW_ENT(entity), velocity);
        
        if (velocity.Length2D() > 0.1f) {
            QAngle vel_angles;
            VectorAngles(velocity, vel_angles);
            float vel_delta = std::abs(curr_yaw - vel_angles.y);
            if (vel_delta > 180) vel_delta = 360 - vel_delta;
            
            // Check for sideways AA
            if (vel_delta > 130.0f && vel_delta < 150.0f) {
                data.is_fake = true;
                data.resolve_type = 1; // Legit AA
                data.aa_type = 1;
            }
        }
    }
    
    return data.is_fake;
}

static float resolveAngleYaw(float angle, brutedata &brute, CachedEntity *ent)
{
    if (!ent || CE_BAD(ent) || !ent->m_bAlivePlayer() || ent->m_IDX <= 0 || ent->m_IDX >= MAX_PLAYERS)
        return angle;
        
    auto& data = player_resolver_data[ent->m_IDX];
    brute.original_angle.y = angle;
    
    // Normalize angle first
    while (angle > 180) angle -= 360;
    while (angle < -180) angle += 360;

    // If we've hit the target enough times with this angle, keep using it
    if (brute.hits_in_a_row >= *min_hits && data.has_hit) {
        data.resolved = true;
        data.resolved_yaw = brute.new_angle.y;
        return brute.new_angle.y;
    }

    // Different resolve modes
    switch (*resolver_mode) 
    {
    case 0: // Smart - Analyze patterns
    {
        if (data.resolve_type == 1) { // Legit AA
            // For legit AA, try to predict based on velocity
            Vector velocity;
            velocity::EstimateAbsVelocity(RAW_ENT(ent), velocity);
            if (velocity.Length2D() > 0.1f) {
                QAngle vel_angles;
                VectorAngles(velocity, vel_angles);
                
                // Try opposite of velocity direction first
                if (data.missed_shots % 2 == 0)
                    angle = vel_angles.y + 180.0f;
                else
                    angle = vel_angles.y;
            }
        }
        else { // Blatant AA
            float spin_speed = 0.0f;
            switch (data.aa_type) {
            case 2: // Jitter
                if (data.avg_delta > 150.0f) {
                    // Likely 180 flip
                    angle += 180.0f;
                }
                else {
                    // Alternate between original and inverse
                    if (brute.brutenum % 2)
                        angle += data.avg_delta;
                    else 
                        angle -= data.avg_delta;
                }
                break;
                
            case 3: // Spin
                // Try to predict the current spin angle
                spin_speed = (angle - data.last_eye_yaw) / (data.last_simtime - CE_FLOAT(ent, netvar.m_flSimulationTime));
                angle += spin_speed * 0.2f; // Predict 0.2s ahead
                break;
                
            case 4: // Random
                // Use previous successful angles more often
                if (data.shots_hit > 0 && data.hit_accuracy > 0.5f) {
                    angle = data.last_real_yaw;
                }
                else {
                    // Try common angles based on misses
                    float resolve_angles[] = { 0.0f, 180.0f, 90.0f, -90.0f };
                    angle += resolve_angles[brute.brutenum % 4];
                }
                break;
            }
        }
        break;
    }
    case 1: // Adaptive
    {
        if (data.has_hit) {
            // Use previously successful angle
            angle = data.resolved_yaw;
        }
        else {
            // Increment angle based on misses and accuracy
            float increment = 45.0f;
            if (data.shots_fired > 0)
                increment *= (1.0f - data.hit_accuracy); // More aggressive for lower accuracy
                
            angle += increment * (data.missed_shots % 8);
        }
        break;
    }
    case 2: // Brute force
    {
        const float angles[] = { 0.0f, 180.0f, 90.0f, -90.0f, 45.0f, -45.0f, 135.0f, -135.0f };
        angle += angles[brute.brutenum % 8];
        break;
    }
    }

    // Normalize result
    while (angle > 180) angle -= 360;
    while (angle < -180) angle += 360;
    
    // Update tracking data
    data.last_eye_yaw = CE_VECTOR(ent, netvar.m_angEyeAngles).y;
    brute.new_angle.y = angle;
    data.resolved_yaw = angle;
    data.resolved = true;
    
    return angle;
}

void OnHit(CachedEntity* target) {
    if (!target || target->m_Type() != ENTITY_PLAYER || target->m_IDX <= 0 || target->m_IDX >= MAX_PLAYERS)
        return;
        
    auto& data = player_resolver_data[target->m_IDX];
    data.has_hit = true;
    data.missed_shots = 0;
    data.shots_hit++;
    data.shots_fired++;
    data.hit_accuracy = (float)data.shots_hit / data.shots_fired;
    data.last_real_yaw = data.resolved_yaw;
}

void OnMiss(CachedEntity* target) {
    if (!target || target->m_Type() != ENTITY_PLAYER || target->m_IDX <= 0 || target->m_IDX >= MAX_PLAYERS)
        return;
        
    auto& data = player_resolver_data[target->m_IDX];
    data.missed_shots++;
    data.shots_fired++;
    data.hit_accuracy = (float)data.shots_hit / data.shots_fired;
    
    if (data.missed_shots > 4) {
        data.has_hit = false;
    }
}

static float resolveAnglePitch(float angle, brutedata &brute, CachedEntity *ent)
{
    if (!ent || CE_BAD(ent))
        return angle;
        
    brute.original_angle.x = angle;

    // Get CSniperDot associated with entity
    CachedEntity *sniper_dot = nullptr;

    // Get Weapon id with bounds check
    int weapon_id = HandleToIDX(CE_INT(ent, netvar.hActiveWeapon));
    if (IDX_BAD(weapon_id))
        return angle;

    // Check weapon for validity
    auto weapon_ent = ENTITY(weapon_id);
    if (CE_GOOD(weapon_ent))
    {
        if (weapon_ent->m_iClassID() == CL_CLASS(CTFSniperRifle) || 
            weapon_ent->m_iClassID() == CL_CLASS(CTFSniperRifleDecap) || 
            weapon_ent->m_iClassID() == CL_CLASS(CTFSniperRifleClassic))
        {
            // Bounds check for sniper dot array
            if (ent->m_IDX > 0 && ent->m_IDX <= MAX_PLAYERS)
            {
                sniper_dot = sniperdot_array.at(ent->m_IDX - 1);
                if (CE_BAD(sniper_dot) || sniper_dot->m_iClassID() != CL_CLASS(CSniperDot))
                    sniper_dot = nullptr;
            }
        }
    }
    

    if (sniper_dot == nullptr)
    {
        switch (*resolver_mode)
        {
        case 0: // Aggressive
            if (angle >= 89.0f || angle <= -89.0f)
            {
                if (brute.brutenum % 4 == 0)
                    angle = 89.0f;
                else if (brute.brutenum % 4 == 1)
                    angle = -89.0f;
                else if (brute.brutenum % 4 == 2)
                    angle = 0.0f;
                else
                    angle = angle > 0 ? -89.0f : 89.0f;
            }
            break;
            
        case 1: // Adaptive
            if (brute.hits_in_a_row > 0)
                angle = brute.last_successful_angle;
            else if (angle >= 89.0f || angle <= -89.0f)
            {
                if (brute.brutenum % 2)
                    angle = angle > 0 ? -89.0f : 89.0f;
            }
            break;
            
        case 2: // Conservative
            if (angle >= 89.0f || angle <= -89.0f)
            {
                if (brute.brutenum % 2)
                    angle = 0.0f;
            }
            break;
        }
    }
    else
    {
        // Get End and start point
        auto dot_origin = sniper_dot->m_vecOrigin();
        auto eye_origin = re::C_BasePlayer::GetEyePosition(RAW_ENT(ent));
        // Get Angle from eye to dot
        Vector diff = dot_origin - eye_origin;
        Vector angles;
        VectorAngles(diff, angles);
        // Use the pitch (yaw is not useable because sadly the sniper dot does not represent it with fake yaw)
        angle = angles.x;
    }

    brute.new_angle.x = angle;
    return angle;
}

void increaseBruteNum(int idx)
{
    if (idx <= 0 || idx >= MAX_PLAYERS)
        return;
        
    auto ent = ENTITY(idx);
    if (CE_BAD(ent) || !ent->player_info || !ent->player_info->friendsID)
        return;
        
    auto &data = hacks::shared::anti_anti_aim::resolver_map[ent->player_info->friendsID];
    
    // If we've hit enough times in a row, consider this angle resolved
    if (data.hits_in_a_row >= *min_hits)
    {
        data.hits_in_a_row = *min_hits - 1;
        data.last_successful_angle = (int)std::floor((data.brutenum / 2.0f)) % yaw_resolves.size();
        return;
    }
    else if (data.hits_in_a_row >= 2)
        data.hits_in_a_row = 0;
    else
    {
        data.brutenum++;
        if (debug)
            logging::Info("AAA: Brutenum for entity %i increased to %i", idx, data.brutenum);
        data.hits_in_a_row = 0;
        auto &angle        = CE_VECTOR(ent, netvar.m_angEyeAngles);
        angle.x            = resolveAnglePitch(data.original_angle.x, data, ent);
        angle.y            = resolveAngleYaw(data.original_angle.y, data, ent);
        data.new_angle.x   = angle.x;
        data.new_angle.y   = angle.y;
    }
}

static void pitchHook(const CRecvProxyData *pData, void *pStruct, void *pOut)
{
    float flPitch      = pData->m_Value.m_Float;
    float *flPitch_out = (float *) pOut;

    if (!enable)
    {
        *flPitch_out = flPitch;
        return;
    }

    auto client_ent   = (IClientEntity *) (pStruct);
    CachedEntity *ent = ENTITY(client_ent->entindex());
    if (CE_GOOD(ent))
        *flPitch_out = resolveAnglePitch(flPitch, resolver_map[ent->player_info->friendsID], ent);
}

static void yawHook(const CRecvProxyData *pData, void *pStruct, void *pOut)
{
    float flYaw      = pData->m_Value.m_Float;
    float *flYaw_out = (float *) pOut;

    if (!enable)
    {
        *flYaw_out = flYaw;
        return;
    }

    auto client_ent   = (IClientEntity *) (pStruct);
    CachedEntity *ent = ENTITY(client_ent->entindex());
    if (CE_GOOD(ent))
        *flYaw_out = resolveAngleYaw(flYaw, resolver_map[ent->player_info->friendsID], ent);
}

// *_ptr points to what we need to modify while *_ProxyFn holds the old value
static RecvVarProxyFn *original_ptrX;
static RecvVarProxyFn original_ProxyFnX;
static RecvVarProxyFn *original_ptrY;
static RecvVarProxyFn original_ProxyFnY;

static void hook()
{
    auto pClass = g_IBaseClient->GetAllClasses();
    while (pClass)
    {
        const char *pszName = pClass->m_pRecvTable->m_pNetTableName;
        // "DT_TFPlayer", "tfnonlocaldata"
        if (!strcmp(pszName, "DT_TFPlayer"))
        {
            for (int i = 0; i < pClass->m_pRecvTable->m_nProps; ++i)
            {
                RecvPropRedef *pProp1 = (RecvPropRedef *) &(pClass->m_pRecvTable->m_pProps[i]);
                if (!pProp1)
                    continue;
                const char *pszName2 = pProp1->m_pVarName;
                if (!strcmp(pszName2, "tfnonlocaldata"))
                    for (int j = 0; j < pProp1->m_pDataTable->m_nProps; j++)
                    {
                        RecvPropRedef *pProp2 = (RecvPropRedef *) &(pProp1->m_pDataTable->m_pProps[j]);
                        if (!pProp2)
                            continue;
                        const char *name = pProp2->m_pVarName;

                        // Pitch Fix
                        if (!strcmp(name, "m_angEyeAngles[0]"))
                        {
                            original_ptrX     = &pProp2->m_ProxyFn;
                            original_ProxyFnX = pProp2->m_ProxyFn;
                            pProp2->m_ProxyFn = pitchHook;
                        }

                        // Yaw Fix
                        if (!strcmp(name, "m_angEyeAngles[1]"))
                        {
                            original_ptrY = &pProp2->m_ProxyFn;
                            logging::Info("Yaw Fix Applied");
                            original_ProxyFnY = pProp2->m_ProxyFn;
                            pProp2->m_ProxyFn = yawHook;
                        }
                    }
            }
        }
        pClass = pClass->m_pNext;
    }
}

static void shutdown()
{
    *original_ptrX = original_ProxyFnX;
    *original_ptrY = original_ProxyFnY;
}

static InitRoutine init(
    []()
    {
        hook();
        EC::Register(EC::Shutdown, shutdown, "antiantiaim_shutdown");
        EC::Register(EC::CreateMove, CreateMove, "cm_antiantiaim");
        EC::Register(EC::CreateMoveWarp, CreateMove, "cmw_antiantiaim");
#if ENABLE_TEXTMODE
        EC::Register(EC::CreateMove, modifyAngles, "cm_textmodeantiantiaim");
        EC::Register(EC::CreateMoveWarp, modifyAngles, "cmw_textmodeantiantiaim");
#endif
    });

static float NormalizeAngle(float angle) {
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}


void UpdateResolver(int ent_idx, CachedEntity* player) {
    // Add bounds check
    if (ent_idx <= 0 || ent_idx >= MAX_PLAYERS || !player || !g_IEngine->IsInGame())
        return;

    // Add null checks for player entity
    if (CE_BAD(player) || !player->m_bAlivePlayer())
        return;

    static std::array<float, MAX_PLAYERS> last_yaw{};
    static std::array<float, MAX_PLAYERS> prev_yaw{};
    static std::array<int, MAX_PLAYERS> jitter_samples{};
    static std::array<float, MAX_PLAYERS> jitter_delta{};
    static std::array<Timer, MAX_PLAYERS> update_timer{};

    // Get eye angles with safety check
    if (CE_BAD(player))
        return;
        
    Vector eye_angles = CE_VECTOR(player, netvar.m_angEyeAngles);
    if (std::isnan(eye_angles.x) || std::isnan(eye_angles.y))
        return;
        
    float eye_yaw = eye_angles.y;
    float eye_pitch = eye_angles.x;

    if (update_timer[ent_idx].check(50)) {
        float delta = std::abs(eye_yaw - last_yaw[ent_idx]);
        delta = NormalizeAngle(delta);

        if (std::abs(delta) > 30.0f) {
            jitter_samples[ent_idx]++;
            jitter_delta[ent_idx] = delta;

            if (jitter_samples[ent_idx] >= 3) {
                float avg_delta = jitter_delta[ent_idx] / jitter_samples[ent_idx];
                float predicted_yaw = eye_yaw;
                if (std::abs(eye_yaw - prev_yaw[ent_idx]) < 10.0f)
                    predicted_yaw += avg_delta;

                player_resolver_data[ent_idx].resolved_yaw = predicted_yaw;
                player_resolver_data[ent_idx].resolved = true;
            }
        }

        prev_yaw[ent_idx] = last_yaw[ent_idx];
        last_yaw[ent_idx] = eye_yaw;
        update_timer[ent_idx].update();
    }

    if (jitter_samples[ent_idx] < 3) {
        float velocity_yaw = 0.0f;
        if (CE_VALID(player)) {
            Vector velocity;
            velocity::EstimateAbsVelocity(RAW_ENT(player), velocity);
            if (!velocity.IsZero()) {
                QAngle angle;
                VectorAngles(velocity, angle);
                velocity_yaw = angle.y;
            }
        }

        if (std::abs(eye_yaw - velocity_yaw) > 150.0f) {
            player_resolver_data[ent_idx].resolved_yaw = velocity_yaw;
            player_resolver_data[ent_idx].resolved = true;
            return;
        }

        float delta_from_90 = std::abs(std::fmod(std::abs(eye_yaw - velocity_yaw), 90.0f));
        if (delta_from_90 < 15.0f) {
            player_resolver_data[ent_idx].resolved_yaw = velocity_yaw + 90.0f;
            player_resolver_data[ent_idx].resolved = true;
            return;
        }
    }

    static Timer reset_timer{};
    if (reset_timer.check(5000)) {
        for (int i = 1; i <= g_IEngine->GetMaxClients(); i++)
            jitter_samples[i] = 0;
        reset_timer.update();
    }
}
} // namespace hacks::shared::anti_anti_aim
