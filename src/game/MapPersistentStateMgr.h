/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __InstanceSaveMgr_H
#define __InstanceSaveMgr_H

#include "Common.h"
#include "Platform/Define.h"
#include "Policies/Singleton.h"
#include "ace/Thread_Mutex.h"
#include <list>
#include <map>
#include "Utilities/UnorderedMapSet.h"
#include "Database/DatabaseEnv.h"
#include "DBCEnums.h"
#include "ObjectGuid.h"

struct InstanceTemplate;
struct MapEntry;
class Player;
class Group;

class MapPersistentStateManager;

/*
    Holds the information necessary for creating a new map for non-instanceable maps

    As object Used for non-instanceable Map only
*/
class MapPersistentState
{
    friend class MapPersistentStateManager;
    public:
        /* Created either when:
           - any new instance is being generated
           - the first time a player bound to InstanceId logs in
           - when a group bound to the instance is loaded */
        MapPersistentState(uint16 MapId, uint32 InstanceId, Difficulty difficulty);

        /* Unloaded when m_playerList and m_groupList become empty
           or when the instance is reset */
        virtual ~MapPersistentState();

        /* A map corresponding to the InstanceId/MapId does not always exist.
        MapPersistentState objects may be created on player logon but the maps are
        created and loaded only when a player actually enters the instance. */
        uint32 GetInstanceId() const { return m_instanceid; }
        uint32 GetMapId() const { return m_mapid; }

        MapEntry const* GetMapEntry() const;

        /* currently it is possible to omit this information from this structure
           but that would depend on a lot of things that can easily change in future */
        Difficulty GetDifficulty() const { return m_difficulty; }

        bool IsUsedByMap() const { return m_usedByMap; }
        void SetUsedByMapState(bool state)
        {
            m_usedByMap = state;
            if (!state)
                UnloadIfEmpty();
        }

        time_t GetCreatureRespawnTime(uint32 loguid) const
        {
            RespawnTimes::const_iterator itr = m_creatureRespawnTimes.find(loguid);
            return itr != m_creatureRespawnTimes.end() ? itr->second : 0;
        }
        void SaveCreatureRespawnTime(uint32 loguid, time_t t);
        time_t GetGORespawnTime(uint32 loguid) const
        {
            RespawnTimes::const_iterator itr = m_goRespawnTimes.find(loguid);
            return itr != m_goRespawnTimes.end() ? itr->second : 0;
        }
        void SaveGORespawnTime(uint32 loguid, time_t t);

    protected:
        virtual bool CanBeUnload() const;
        bool UnloadIfEmpty();
        void ClearRespawnTimes();

    private:
        void SetCreatureRespawnTime(uint32 loguid, time_t t);
        void SetGORespawnTime(uint32 loguid, time_t t);

    private:
        typedef UNORDERED_MAP<uint32, time_t> RespawnTimes;

        uint32 m_instanceid;
        uint32 m_mapid;
        Difficulty m_difficulty;
        bool m_usedByMap;                                   // true when instance map loaded, lock MapPersistentState from unload

        // persistent data
        RespawnTimes m_creatureRespawnTimes;                // lock MapPersistentState from unload, for example for temporary bound dungeon unload delay
        RespawnTimes m_goRespawnTimes;                      // lock MapPersistentState from unload, for example for temporary bound dungeon unload delay
};

/*
    Holds the information necessary for creating a new map for an existing instance
    Is referenced in three cases:
    - player-instance binds for solo players (not in group)
    - player-instance binds for permanent heroic/raid saves
    - group-instance binds (both solo and permanent) cache the player binds for the group leader

    Used for InstanceMap only
*/
class DungeonPersistentState : public MapPersistentState
{
    public:
        /* Created either when:
           - any new instance is being generated
           - the first time a player bound to InstanceId logs in
           - when a group bound to the instance is loaded */
        DungeonPersistentState(uint16 MapId, uint32 InstanceId, Difficulty difficulty, time_t resetTime, bool canReset);

        ~DungeonPersistentState();

        InstanceTemplate const* GetTemplate() const;

        uint8 GetPlayerCount() const { return m_playerList.size(); }
        uint8 GetGroupCount() const { return m_groupList.size(); }

        /* online players bound to the instance (perm/solo)
           does not include the members of the group unless they have permanent saves */
        void AddPlayer(Player *player) { m_playerList.push_back(player); }
        bool RemovePlayer(Player *player) { m_playerList.remove(player); return UnloadIfEmpty(); }
        /* all groups bound to the instance */
        void AddGroup(Group *group) { m_groupList.push_back(group); }
        bool RemoveGroup(Group *group) { m_groupList.remove(group); return UnloadIfEmpty(); }

        /* for normal instances this corresponds to max(creature respawn time) + X hours
           for raid/heroic instances this caches the global respawn time for the map */
        time_t GetResetTime() const { return m_resetTime; }
        void SetResetTime(time_t resetTime) { m_resetTime = resetTime; }
        time_t GetResetTimeForDB() const;

        /* instances cannot be reset (except at the global reset time)
           if there are players permanently bound to it
           this is cached for the case when those players are offline */
        bool CanReset() const { return m_canReset; }
        void SetCanReset(bool canReset) { m_canReset = canReset; }

        /* Saved when the instance is generated for the first time */
        void SaveToDB();
        /* When the instance is being reset (permanently deleted) */
        void DeleteFromDB();
        /* Delete respawn data at dungeon reset */
        void DeleteRespawnTimes();

    protected:
        bool CanBeUnload() const;                           // overwrite MapPersistentState::CanBeUnload

    private:
        typedef std::list<Player*> PlayerListType;
        typedef std::list<Group*> GroupListType;

        time_t m_resetTime;
        bool m_canReset;

        /* the only reason the instSave-object links are kept is because
           the object-instSave links need to be broken at reset time
           TODO: maybe it's enough to just store the number of players/groups */
        PlayerListType m_playerList;                        // lock MapPersistentState from unload
        GroupListType m_groupList;                          // lock MapPersistentState from unload
};

class BattleGroundPersistentState : public MapPersistentState
{
    public:
        /* Created either when:
           - any new BG/arena is being generated
        */
        BattleGroundPersistentState(uint16 MapId, uint32 InstanceId, Difficulty difficulty)
            : MapPersistentState(MapId, InstanceId, difficulty) {}

        ~BattleGroundPersistentState() {}

    protected:
        bool CanBeUnload() const;                           // overwrite MapPersistentState::CanBeUnload
};


enum ResetEventType
{
    RESET_EVENT_NORMAL_DUNGEON = 0,                         // no fixed reset time
    RESET_EVENT_INFORM_1       = 1,                         // raid/heroic warnings
    RESET_EVENT_INFORM_2       = 2,
    RESET_EVENT_INFORM_3       = 3,
    RESET_EVENT_INFORM_LAST    = 4,
};

#define MAX_RESET_EVENT_TYPE   5

/* resetTime is a global propery of each (raid/heroic) map
    all instances of that map reset at the same time */
struct DungeonResetEvent
{
    ResetEventType type   :8;                               // if RESET_EVENT_DUNGEON then InstanceID == 0 and applied to all instances for map)
    uint16 mapid;                                           // used with mapid used as for select reset for global cooldown instances (instanceid==0 for event)
    uint32 instanceId;                                      // used for select reset for normal dungeons

    DungeonResetEvent() : type(RESET_EVENT_NORMAL_DUNGEON), mapid(0), instanceId(0) {}
    DungeonResetEvent(ResetEventType t, uint32 _mapid, uint32 _instanceid)
        : type(t), mapid(_mapid), instanceId(_instanceid) {}

    bool operator == (const DungeonResetEvent& e) { return e.mapid == mapid && e.instanceId == instanceId; }
};

class DungeonResetScheduler
{
    public:                                                 // constructors
        explicit DungeonResetScheduler(MapPersistentStateManager& mgr) : m_InstanceSaves(mgr) {}
        void LoadResetTimes();

    public:                                                 // accessors
        time_t GetResetTimeFor(uint32 mapid) { return m_resetTimeByMapId[mapid]; }

        static uint32 GetMaxResetTimeFor(InstanceTemplate const* temp);
        static time_t CalculateNextResetTime(InstanceTemplate const* temp, time_t prevResetTime);
    public:                                                 // modifiers
        void SetResetTimeFor(uint32 mapid, time_t t)
        {
            m_resetTimeByMapId[mapid] = t;
        }

        void ScheduleReset(bool add, time_t time, DungeonResetEvent event);

        void Update();

    private:                                                // fields
        MapPersistentStateManager& m_InstanceSaves;

        // fast lookup for reset times (always use existing functions for access/set)
        typedef std::vector<time_t /*resetTime*/> ResetTimeVector;
        ResetTimeVector m_resetTimeByMapId;

        typedef std::multimap<time_t /*resetTime*/, DungeonResetEvent> ResetTimeQueue;
        ResetTimeQueue m_resetTimeQueue;
};

class MANGOS_DLL_DECL MapPersistentStateManager : public MaNGOS::Singleton<MapPersistentStateManager, MaNGOS::ClassLevelLockable<MapPersistentStateManager, ACE_Thread_Mutex> >
{
    friend class DungeonResetScheduler;
    public:                                                 // constructors
        MapPersistentStateManager();
        ~MapPersistentStateManager();

    public:                                                 // common for all MapPersistentState (sub)classes
        void LoadCreatureRespawnTimes();
        void LoadGameobjectRespawnTimes();

        // auto select appropriate MapPersistentState (sub)class by MapEntry, and autoselect appropriate way store (by instance/map id)
        // always return != NULL
        MapPersistentState* AddPersistentState(MapEntry const* mapEntry, uint32 instanceId, Difficulty difficulty, time_t resetTime, bool canReset, bool load = false);

        // search stored state, can be NULL in result
        MapPersistentState *GetPersistentState(uint32 mapId, uint32 InstanceId);

        void RemovePersistentState(uint32 mapId, uint32 instanceId);

    public:                                                 // DungeonPersistentState specific
        void CleanupInstances();
        void PackInstances();

        DungeonResetScheduler& GetScheduler() { return m_Scheduler; }

        static void DeleteInstanceFromDB(uint32 instanceid);

        void GetStatistics(uint32& numStates, uint32& numBoundPlayers, uint32& numBoundGroups);

        void Update() { m_Scheduler.Update(); }
    private:
        typedef UNORDERED_MAP<uint32 /*InstanceId or MapId*/, MapPersistentState*> PersistentStateMap;

        //  called by scheduler
        void _ResetOrWarnAll(uint32 mapid, bool warn, uint32 timeleft);
        void _ResetInstance(uint32 mapid, uint32 instanceId);
        void _CleanupExpiredInstancesAtTime(time_t t);

        void _ResetSave(PersistentStateMap& holder, PersistentStateMap::iterator &itr);
        void _DelHelper(DatabaseType &db, const char *fields, const char *table, const char *queryTail,...);
        // used during global instance resets
        bool lock_instLists;
        // fast lookup by instance id for instanceable maps
        PersistentStateMap m_instanceSaveByInstanceId;
        // fast lookup by map id for non-instanceable maps
        PersistentStateMap m_instanceSaveByMapId;

        DungeonResetScheduler m_Scheduler;
};

#define sMapPersistentStateMgr MaNGOS::Singleton<MapPersistentStateManager>::Instance()
#endif