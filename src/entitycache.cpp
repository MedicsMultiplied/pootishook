/*
 * entitycache.cpp
 *
 *  Created on: Nov 7, 2016
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include <settings/Float.hpp>
#include "soundcache.hpp"
#include "enums.hpp"

namespace entity_cache
{
std::unordered_map<int, CachedEntity> array;
std::vector<CachedEntity *> valid_ents;
std::vector<CachedEntity *> player_cache;
int previous_max = 0;
int previous_ent = 0;
int max = 1;
} // namespace entity_cache

inline void CachedEntity::Update()
{
    try 
    {
        #ifndef PROXY_ENTITY
        m_pEntity = g_IEntityList->GetClientEntity(m_IDX);
        if (!m_pEntity)
        {
            Reset();
            return;
        }
        #endif

        // Validate entity before doing anything
        IClientEntity* ent = InternalEntity();
        if (!ent || !ent->GetClientClass() || !ent->GetClientClass()->m_ClassID)
        {
            Reset();
            return;
        }

        // Check if entity is dormant or dead
        if (ent->IsDormant() || (m_Type() == ENTITY_PLAYER && !m_bAlivePlayer()))
        {
            Reset();
            return;
        }

        hitboxes.InvalidateCache();
        m_bVisCheckComplete = false;
    }
    catch (...)
    {
        Reset();
    }
}

inline CachedEntity::CachedEntity(int idx) : m_IDX(idx), hitboxes(hitbox_cache::EntityHitboxCache{ idx })
{
    #ifndef PROXY_ENTITY
    m_pEntity = nullptr;
    #endif
    player_info = nullptr;
}

inline CachedEntity::~CachedEntity()
{
    Reset();
}

bool CachedEntity::IsVisible()
{
    try
    {
        if (!InternalEntity() || InternalEntity()->IsDormant())
            return false;

        if (m_bVisCheckComplete)
            return m_bAnyHitboxVisible;

        Vector result;
        auto hitbox = hitboxes.GetHitbox(0);  // Just check first hitbox for safety
        
        if (!hitbox)
            result = m_vecOrigin();
        else
            result = hitbox->center;

        if (IsEntityVectorVisible(this, result, true, MASK_SHOT_HULL, nullptr, true))
        {
            m_bAnyHitboxVisible = true;
            m_bVisCheckComplete = true;
            return true;
        }

        m_bAnyHitboxVisible = false;
        m_bVisCheckComplete = true;
        return false;
    }
    catch (...)
    {
        return false;
    }
}

void entity_cache::Update()
{
    try
    {
        if (g_Settings.bInvalid)
            return;

        max = std::min(g_IEntityList->GetHighestEntityIndex(), MAX_ENTITIES - 1);   
        int current_ents = g_IEntityList->NumberOfEntities(false);

        valid_ents.clear();
        player_cache.clear();

        valid_ents.reserve(max + 1);
        player_cache.reserve(g_GlobalVars->maxClients);

        // First pass: Remove invalid entities
        for (auto it = array.begin(); it != array.end();)
        {
            bool should_remove = true;
            IClientEntity* entity = nullptr;
            
            try
            {
                entity = g_IEntityList->GetClientEntity(it->first);
                if (entity && entity->GetClientClass() && entity->GetClientClass()->m_ClassID)
                {
                    IClientEntity* internal = it->second.InternalEntity();
                    if (internal && !internal->IsDormant())
                        should_remove = false;
                }
            }
            catch (...) { }

            if (should_remove)
            {
                it->second.Reset();
                it = array.erase(it);
            }
            else
                ++it;
        }

        // Second pass: Update and validate remaining entities
        for (int i = 0; i <= max; i++)
        {
            IClientEntity* entity = nullptr;
            try
            {
                entity = g_IEntityList->GetClientEntity(i);
                if (!entity)
                    continue;
                    
     
                if (!entity->GetClientClass() || !entity->GetClientClass()->m_ClassID)
                    continue;
                    
 
                if (entity->IsDormant() || !entity->GetRefEHandle().IsValid())
                    continue;

                auto it = array.find(i);
                if (it == array.end())
                {
                    try
                    {
                        it = array.try_emplace(i, CachedEntity{ i }).first;
                    }
                    catch (...)
                    {
                        continue;
                    }
                }

                CachedEntity &ent = it->second;
                bool was_alive = ent.m_bAlivePlayer();

                try
                {
                    ent.Update();
                }
                catch (...)
                {
                    ent.Reset();
                    continue;
                }

                IClientEntity* internal = ent.InternalEntity();
                if (!internal || internal->IsDormant())
                    continue;

                bool is_alive = ent.m_bAlivePlayer();
                if (was_alive && !is_alive)
                {
                    ent.Reset();
                    continue;
                }

                valid_ents.push_back(&ent);
                EntityType type = ent.m_Type();

                if ((type == ENTITY_PLAYER || type == ENTITY_BUILDING || type == ENTITY_NPC) && is_alive)
                {
                    try
                    {
                        if (!internal->IsDormant())
                            ent.hitboxes.UpdateBones();

                        if (type == ENTITY_PLAYER)
                            player_cache.push_back(&ent);
                    }
                    catch (...) { }
                }

                if (type == ENTITY_PLAYER)
                {
                    try
                    {
                        if (!ent.player_info)
                            ent.player_info = new player_info_s;
                        
                        if (ent.player_info)
                            GetPlayerInfo(ent.m_IDX, ent.player_info);
                    }
                    catch (...) { }
                }
            }
            catch (...)
            {
                continue;
            }
        }

        previous_max = max;
        previous_ent = current_ents;
    }
    catch (...)
    {
        // If something goes terribly wrong, invalidate everything
        Invalidate();
    }
}

void entity_cache::Invalidate()
{
    try
    {
        for (auto& pair : array)
            pair.second.Reset();
        array.clear();
        valid_ents.clear();
        player_cache.clear();
    }
    catch (...) { }
}

void entity_cache::Shutdown()
{
    Invalidate();
    previous_max = 0;
    max = -1;
}
