/*
 * targethelper.cpp
 *
 *  Created on: Oct 16, 2016
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "hacks/Backtrack.hpp"

/*
 * Targeting priorities:
 * passive bullet vacc medic
 * zoomed snipers ALWAYS
 * medics
 * snipers
 * spies
 */

/* Assuming given entity is a valid target range 0 to 100 */
int GetScoreForEntity(CachedEntity *entity)
{
    if (!entity)
        return 0;

    // First check if player is marked as RAGE - give highest priority
    if (entity->m_Type() == ENTITY_PLAYER)
    {
        auto &pl = playerlist::AccessData(entity);
        if (pl.state == playerlist::k_EState::RAGE)
            return 100; // Maximum priority for RAGE players
    }

    if (entity->m_Type() == ENTITY_BUILDING)
    {
        if (entity->m_iClassID() == CL_CLASS(CObjectSentrygun))
        {
            bool is_strong_class = g_pLocalPlayer->clazz == tf_heavy || g_pLocalPlayer->clazz == tf_soldier;

            if (is_strong_class)
            {
                float distance = (g_pLocalPlayer->v_Origin - entity->m_vecOrigin()).Length();
                if (distance < 400.0f)
                    return 120;
                else if (distance < 1100.0f)
                    return 60;
                return 30;
            }
            return 1;
        }
        return 0;
    }
    int clazz      = CE_INT(entity, netvar.iClass);
    int health     = CE_INT(entity, netvar.iHealth);
    float distance = (g_pLocalPlayer->v_Origin - entity->m_vecOrigin()).Length();
    bool zoomed    = HasCondition<TFCond_Zoomed>(entity);
    bool pbullet   = HasCondition<TFCond_SmallBulletResist>(entity);
    bool special   = false;
    bool kritz     = IsPlayerCritBoosted(entity);
    int total      = 0;
    switch (clazz)
    {
    case tf_sniper:
        total += 25;
        if (zoomed)
        {
            total += 50;
        }
        special = true;
        break;
    case tf_medic:
        total += 50;
        if (pbullet)
            return 100;
        break;
    case tf_spy:
        total += 20;
        if (distance < 400)
            total += 60;
        special = true;
        break;
    case tf_soldier:
        if (HasCondition<TFCond_BlastJumping>(entity))
            total += 30;
        break;
    }
    if (!special)
    {
        if (pbullet)
        {
            total += 50;
        }
        if (kritz)
        {
            total += 99;
        }
        if (distance != 0)
        {
            int distance_factor = (4096 / distance) * 4;
            total += distance_factor;
            if (health != 0)
            {
                int health_factor = (450 / health) * 4;
                if (health_factor > 30)
                    health_factor = 30;
                total += health_factor;
            }
        }
    }
    if (total > 99)
        total = 99;
    if (playerlist::AccessData(entity).state == playerlist::k_EState::RAGE)
        total = 999;
    if (IsSentryBuster(entity))
        total = 0;
    if (clazz == tf_medic && g_pGameRules->isPVEMode)
        total = 999;
    return total;
}
