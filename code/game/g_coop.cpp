/*
===========================================================================
Copyright (C) 2026, JACoop contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// g_coop.cpp -- game-side co-op support: networking character appearance.
//
// The SP cgame renders every character (players and NPCs) out of its server
// gentity: the ghoul2 model, skin, surfaces and sabers are built at spawn by
// G_SetG2PlayerModel / saber parsing on the host and never travel over the
// wire. A serverless remote client has none of that, so the host packs each
// character's appearance into a "model spec" configstring
//
//     model;skin;surfOff;surfOn;saber1;colors1;saber2;colors2;class;r,g,b,a;hand;vehicle
//
// (';' because JA's three-part skins already use '|'; hand = "L:path" or
// "R:path", a cutscene prop bolted to that hand, see G_CoopRecordHandModel)
// registered in
// CS_COOP_MODELSPECS and referenced from s.modelindex3, which the entity delta
// already networks. The remote cgame rebuilds the same ghoul2 from that spec
// on first sight (CG_CoopEnsureCharacter in cg_coop.cpp).

#include "g_local.h"
#include "g_functions.h"
#include "wp_saber.h"

extern int G_FindConfigstringIndex( const char *name, int start, int max, qboolean create );

typedef struct coopAppearance_s {
	char	modelName[MAX_QPATH];
	char	customSkin[MAX_QPATH];
	char	surfOff[MAX_QPATH*4];
	char	surfOn[MAX_QPATH*4];
	char	handModel[MAX_QPATH+2];	// "L:path" / "R:path": cutscene prop bolted to a hand
	char	lastSpec[MAX_STRING_CHARS];
} coopAppearance_t;

// Parallel to g_entities, deliberately outside gentity_t / gclient_t so the
// savegame format is untouched.
static coopAppearance_t	coopAppearance[MAX_GENTITIES];

/*
================
G_CoopRecordModel

Called from G_SetG2PlayerModel with the arguments that built the ghoul2, so
the appearance can be re-described later. modelName may be the stormtrooper
fallback if the requested model failed to load.
================
*/
void G_CoopRecordModel( const gentity_t *ent, const char *modelName, const char *customSkin, const char *surfOff, const char *surfOn )
{
	coopAppearance_t *a = &coopAppearance[ent->s.number];

	Q_strncpyz( a->modelName, modelName ? modelName : "", sizeof( a->modelName ) );
	Q_strncpyz( a->customSkin, customSkin ? customSkin : "", sizeof( a->customSkin ) );
	Q_strncpyz( a->surfOff, surfOff ? surfOff : "", sizeof( a->surfOff ) );
	Q_strncpyz( a->surfOn, surfOn ? surfOn : "", sizeof( a->surfOn ) );
	a->lastSpec[0] = '\0';	// force a re-publish on the next frame
}

/*
================
G_CoopRecordSkinPath

SET_SKIN swaps the skin of a built model by full path
("models/players/tavion_new/model_possessed.skin"). The spec names skins
the G_SetG2PlayerModel way, so keep the "possessed" part when the path is
this model's; a skin from another model directory cannot be described.
================
*/
void G_CoopRecordSkinPath( const gentity_t *ent, const char *skinPath )
{
	coopAppearance_t *a = &coopAppearance[ent->s.number];
	char prefix[MAX_QPATH];

	if ( !a->modelName[0] )
	{
		return;
	}
	Com_sprintf( prefix, sizeof( prefix ), "models/players/%s/model_", a->modelName );
	if ( Q_stricmpn( skinPath, prefix, strlen( prefix ) ) )
	{
		if ( g_developer->integer )
		{
			gi.Printf( "coop: ent %i SET_SKIN '%s' is not a skin of '%s', joiners keep theirs\n", ent->s.number, skinPath, a->modelName );
		}
		return;
	}
	Q_strncpyz( a->customSkin, skinPath + strlen( prefix ), sizeof( a->customSkin ) );
	char *dot = strrchr( a->customSkin, '.' );
	if ( dot )
	{
		*dot = '\0';
	}
	if ( !Q_stricmp( a->customSkin, "default" ) )
	{
		a->customSkin[0] = '\0';
	}
	a->lastSpec[0] = '\0';
}

/*
================
G_CoopRecordHandModel

A cutscene prop bolted to a hand (SET_ADDLHANDBOLT_MODEL / RHAND: datapad,
comlink, scepter) is ghoul2 data the joiners never see; publish it as the
spec's last field, "L:path" or "R:path" (stock keeps one such model per
character, ent->cinematicModel). An empty path removes it.
================
*/
void G_CoopRecordHandModel( const gentity_t *ent, char side, const char *modelPath )
{
	coopAppearance_t *a = &coopAppearance[ent->s.number];

	if ( modelPath && modelPath[0] )
	{
		Com_sprintf( a->handModel, sizeof( a->handModel ), "%c:%s", side, modelPath );
	}
	else
	{
		a->handModel[0] = '\0';
	}
	a->lastSpec[0] = '\0';
}

/*
================
G_CoopClearAppearance

Entity slot is being freed or reused: forget its appearance so a later
occupant does not inherit it.
================
*/
/*
================
G_CoopSoundSetIndex

CS_COOP_SOUNDSETS slot of an ambient sound set name (allocated on first
use); 0 when the range is full (the remote client then just lacks that set).
================
*/
int G_CoopSoundSetIndex( const char *name )
{
	char	s[MAX_STRING_CHARS];
	int		i;

	if ( !name || !name[0] )
	{
		return 0;
	}
	for ( i = 1; i < MAX_COOP_SOUNDSETS; i++ )
	{
		gi.GetConfigstring( CS_COOP_SOUNDSETS + i, s, sizeof( s ) );
		if ( !s[0] )
		{
			gi.SetConfigstring( CS_COOP_SOUNDSETS + i, name );
			return i;
		}
		if ( !Q_stricmp( s, name ) )
		{
			return i;
		}
	}
	gi.Printf( "coop: no CS_COOP_SOUNDSETS slot left for '%s'\n", name );
	return 0;
}

void G_CoopClearAppearance( const gentity_t *ent )
{
	memset( &coopAppearance[ent->s.number], 0, sizeof( coopAppearance[0] ) );
}

/*
================
G_CoopModelSpecIndex

CS_COOP_MODELSPECS slot for an appearance spec. Like G_FindConfigstringIndex,
but a full range is not fatal: random tints and saber colours make many
distinct specs over a level, so a slot no live character references any
more is recycled (the remote cgame rebuilds whoever pointed at it when the
configstring changes), and as a last resort the entity keeps its old index.
================
*/
static int G_CoopModelSpecIndex( const gentity_t *ent, const char *spec )
{
	char		s[MAX_STRING_CHARS];
	qboolean	used[MAX_COOP_MODELSPECS];
	int			i;

	for ( i = 1; i < MAX_COOP_MODELSPECS; i++ )
	{
		gi.GetConfigstring( CS_COOP_MODELSPECS + i, s, sizeof( s ) );
		if ( !s[0] )
		{
			gi.SetConfigstring( CS_COOP_MODELSPECS + i, spec );
			return i;
		}
		if ( !Q_stricmp( s, spec ) )
		{
			return i;
		}
	}

	// full: recycle a slot that no character in use still points at
	memset( used, 0, sizeof( used ) );
	for ( i = 0; i < globals.num_entities; i++ )
	{
		const gentity_t *e = &g_entities[i];
		if ( e->inuse && e->client && e != ent && e->s.modelindex3 > 0 && e->s.modelindex3 < MAX_COOP_MODELSPECS )
		{
			used[e->s.modelindex3] = qtrue;
		}
	}
	for ( i = 1; i < MAX_COOP_MODELSPECS; i++ )
	{
		if ( !used[i] )
		{
			if ( g_developer->integer )
			{
				gi.Printf( "coop: appearance specs full, ent %i recycles slot %i\n", ent->s.number, i );
			}
			gi.SetConfigstring( CS_COOP_MODELSPECS + i, spec );
			return i;
		}
	}
	gi.Printf( "coop: appearance specs full, ent %i keeps spec %i (remote clients see its previous look)\n", ent->s.number, ent->s.modelindex3 );
	return ent->s.modelindex3;
}

static void G_CoopAppendSaber( char *spec, int specSize, const saberInfo_t *saber )
{
	if ( !saber || !saber->name || !saber->name[0] )
	{
		Q_strcat( spec, specSize, ";;" );
		return;
	}
	Q_strcat( spec, specSize, va( ";%s;", saber->name ) );
	for ( int b = 0; b < saber->numBlades && b < MAX_BLADES; b++ )
	{
		Q_strcat( spec, specSize, va( "%s%i", b ? "," : "", (int)saber->blade[b].color ) );
	}
}

/*
================
G_CoopUpdateAppearance

Run once per frame for every entity with a client. Rebuilds the spec string
and re-registers it only when something changed (model swap, saber pickup,
colour change), so the per-frame cost is a few string appends.
================
*/
void G_CoopUpdateAppearance( gentity_t *ent )
{
	coopAppearance_t	*a = &coopAppearance[ent->s.number];
	char				spec[MAX_STRING_CHARS];

	if ( !ent->client )
	{
		return;
	}
	if ( ent->s.number > 0 && ent->s.number < MAX_CLIENTS )
	{
		// A joiner's eyePoint/headPoint are only refreshed by the host's cgame
		// while the host can see it; keep them at its eyes from the server
		// side (Force push/pull, LOS and targeting trace from eyePoint).
		renderInfo_t *ri = &ent->client->renderInfo;
		VectorCopy( ent->currentOrigin, ri->eyePoint );
		ri->eyePoint[2] += ent->client->ps.viewheight;
		VectorCopy( ent->client->ps.viewangles, ri->eyeAngles );
		VectorCopy( ri->eyePoint, ri->headPoint );
		VectorCopy( ent->currentOrigin, ri->torsoPoint );
		ri->torsoPoint[2] += ent->client->ps.viewheight * 0.6f;
	}
	// the wire has no health; faces, health bars and corpse handling read it
	ent->s.coopHealth = ent->health;
	ent->s.coopMaxHealth = ( ent->max_health & COOP_MAXHEALTH_MASK )
		| ( ( ent->NPC && ( ent->NPC->scriptFlags & SCF_MORELIGHT ) ) ? COOP_MAXHEALTH_MORELIGHT : 0 );
	// head tracking: interest points are host-only, so only entity targets travel
	ent->s.coopLookTarget = ( ent->client->renderInfo.lookMode == LM_ENT ) ? ent->client->renderInfo.lookTarget : ENTITYNUM_NONE;
	// Force visuals the remote cgame draws from the placeholder's playerState (lightning, drain,
	// grip hand blur, rage/protect/absorb shells, speed trails, push/pull hand refraction)
	{
		const playerState_t *ps = &ent->client->ps;
		int f = ps->forcePowersActive & COOPF_ACTIVE_MASK;
		f |= Q_min( ps->forcePowerLevel[FP_LIGHTNING], 3 ) << COOPF_LVL_LIGHTNING_SHIFT;
		f |= Q_min( ps->forcePowerLevel[FP_DRAIN], 3 ) << COOPF_LVL_DRAIN_SHIFT;
		f |= Q_min( ps->forcePowerLevel[FP_PROTECT], 3 ) << COOPF_LVL_PROTECT_SHIFT;
		f |= Q_min( ps->forcePowerLevel[FP_ABSORB], 3 ) << COOPF_LVL_ABSORB_SHIFT;
		if ( ps->forceDrainEntityNum >= ENTITYNUM_WORLD )
		{
			f |= COOPF_DRAIN_AREA;
		}
		if ( ps->powerups[PW_FORCE_PUSH] > level.time )
		{
			f |= COOPF_PUSH_LHAND;
		}
		if ( ps->powerups[PW_FORCE_PUSH_RHAND] > level.time )
		{
			f |= COOPF_PUSH_RHAND;
		}
		if ( ent->client->dualPistols )
		{
			f |= COOPF_DUAL_PISTOLS;
		}
		ent->s.coopForce = f;
		// victim electrocution: the wire bit sticks on corpses, the timer decides
		ent->s.coopShockTime = ps->powerups[PW_SHOCKED];
		// team status panel: shield + which powers the mate has + how much pool is left
		ent->s.coopArmor = ps->stats[STAT_ARMOR];
		ent->s.coopForce2 = ( ps->forcePowersKnown & 0xffff )
			| ( Q_min( 100, Q_max( 0, ps->forcePowerMax > 0 ? ps->forcePower * 100 / ps->forcePowerMax : 0 ) ) << 16 );
	}
	if ( !a->modelName[0] && ent->ghoul2.size() && ent->playerModel >= 0 && ent->playerModel < ent->ghoul2.size() )
	{
		// Savegame load: the entity came back with its ghoul2 (and its skin as
		// a CS_CHARSKINS index) but this record is not part of the save, so
		// rebuild it from them - otherwise the host and every NPC of the save
		// stay invisible to the joiners.
		const char *file = ent->ghoul2[ent->playerModel].mFileName;
		if ( !Q_stricmpn( file, "models/players/", 15 ) )
		{
			char model[MAX_QPATH], skin[MAX_QPATH * 2];
			Q_strncpyz( model, file + 15, sizeof( model ) );
			char *slash = strchr( model, '/' );
			if ( slash )
			{
				*slash = '\0';
			}
			skin[0] = '\0';
			const int skinIndex = ent->ghoul2[ent->playerModel].mCustomSkin;
			if ( skinIndex > 0 )
			{
				char cs[MAX_QPATH * 2];
				gi.GetConfigstring( CS_CHARSKINS + skinIndex, cs, sizeof( cs ) );
				const char *bar = strchr( cs, '|' );
				const char *base = strrchr( cs, '/' );
				if ( bar )
				{	// "models/players/X/|head|torso|legs"
					Q_strncpyz( skin, bar + 1, sizeof( skin ) );
				}
				else if ( base && !Q_stricmpn( base + 1, "model_", 6 ) )
				{	// "models/players/X/model_<skin>.skin"
					Q_strncpyz( skin, base + 7, sizeof( skin ) );
					char *dot = strrchr( skin, '.' );
					if ( dot )
					{
						*dot = '\0';
					}
					if ( !Q_stricmp( skin, "default" ) )
					{
						skin[0] = '\0';
					}
				}
			}
			G_CoopRecordModel( ent, model, skin[0] ? skin : NULL, NULL, NULL );
			if ( g_developer->integer )
			{
				gi.Printf( "coop: ent %i appearance rebuilt from its ghoul2: '%s' skin '%s'\n", ent->s.number, model, skin );
			}
		}
	}
	if ( !a->modelName[0] )
	{
		// A few NPCs (remote_sp, mouse, seeker) still use the pre-ghoul2 md3
		// legs/torso/head models: no G_SetG2PlayerModel, so nothing recorded.
		// Describe them as "@legs;torso;head" for the remote cgame.
		const renderInfo_t *ri = &ent->client->renderInfo;
		if ( ent->ghoul2.size() || !ri->legsModelName[0] )
		{
			return;
		}
		Com_sprintf( spec, sizeof( spec ), "@%s;%s;%s;", ri->legsModelName, ri->torsoModelName, ri->headModelName );
	}
	else
	{
		Com_sprintf( spec, sizeof( spec ), "%s;%s;%s;%s", a->modelName, a->customSkin, a->surfOff, a->surfOn );
	}
	// a player driving an AT-ST keeps its saber in its playerState: not on the walker
	const qboolean noSaber = (qboolean)( ent->client->NPC_class == CLASS_ATST );
	G_CoopAppendSaber( spec, sizeof( spec ), noSaber ? NULL : &ent->client->ps.saber[0] );
	G_CoopAppendSaber( spec, sizeof( spec ), ( !noSaber && ent->client->ps.dualSabers ) ? &ent->client->ps.saber[1] : NULL );
	Q_strcat( spec, sizeof( spec ), va( ";%i", (int)ent->client->NPC_class ) );
	const byte *rgba = ent->client->renderInfo.customRGBA;
	Q_strcat( spec, sizeof( spec ), va( ";%i,%i,%i,%i", rgba[0], rgba[1], rgba[2], rgba[3] ) );
	Q_strcat( spec, sizeof( spec ), va( ";%s", a->handModel ) );
	// vehicles: the .veh name, so the remote cgame can hold a placeholder Vehicle_t (by name: vehicle indices differ per process)
	Q_strcat( spec, sizeof( spec ), va( ";%s", ( ent->m_pVehicle && ent->m_pVehicle->m_pVehicleInfo && ent->m_pVehicle->m_pVehicleInfo->name ) ? ent->m_pVehicle->m_pVehicleInfo->name : "" ) );

	if ( !strcmp( spec, a->lastSpec ) )
	{
		return;
	}
	Q_strncpyz( a->lastSpec, spec, sizeof( a->lastSpec ) );
	ent->s.modelindex3 = G_CoopModelSpecIndex( ent, spec );
}

/*
==============================================================================
Player queries for the AI and triggers

Stock SP hardcodes "the player" as g_entities[0]. With several clients the AI
must consider all of them; these helpers replace those sites.
==============================================================================
*/

/*
================
G_CoopFixClientScriptName

ICARUS resolves script names through a map with one entity per name, and the
mission scripts only know the host as "player". Client slots get fixed names:
slot 0 is "player", the joiners are "player2".."player4". Called wherever an
entity is (re)associated with ICARUS and after a savegame load, so a save
written by an older build (joiner saved as "player") cannot steal the host's
scripts.
================
*/
void G_CoopFixClientScriptName( gentity_t *ent )
{
	if ( !ent || ent->s.number >= MAX_CLIENTS || !ent->client )
	{
		return;
	}
	const char *want = ( ent->s.number == 0 ) ? "player" : va( "player%i", ent->s.number + 1 );
	if ( !ent->script_targetname || Q_stricmp( ent->script_targetname, want ) )
	{
		ent->script_targetname = ( ent->s.number == 0 ) ? (char *)"player" : G_NewString( want );
	}
}

// true for any human player's entity (slots [0, MAX_CLIENTS) are reserved for clients)
qboolean G_CoopIsPlayer( const gentity_t *ent )
{
	return (qboolean)( ent && ent->s.number < MAX_CLIENTS && ent->client && ent->inuse );
}

/*
================
G_CoopNote

Crash breadcrumb. The host's process has died on JD's machine leaving nothing
behind but a Windows fault record, so the events able to take a client's
entity with them (a mover blocking on a player, a co-op death, a refused free)
each drop one line in qconsole.log. The last lines of the log then say where
the game was when it went.

Rate limited: a mover grinding a body calls its blocked function every frame.
================
*/
#define COOP_NOTE_REPEAT	3000	// ms before the very same note is printed again

static char	coopLastNote[192];
static int	coopLastNoteTime;

void G_CoopNote( const char *text )
{
	if ( !text || !text[0] )
	{
		return;
	}
	if ( G_CoopNumPlayers() < 2 )
	{	// alone: the log stays exactly the stock game's
		return;
	}
	char	cur[sizeof( coopLastNote )];

	Q_strncpyz( cur, text, sizeof( cur ) );	// compare what we would store, not the full text
	if ( !strcmp( cur, coopLastNote ) && level.time >= coopLastNoteTime
		&& level.time - coopLastNoteTime < COOP_NOTE_REPEAT )
	{	// (level.time restarts on a level change, hence the lower bound: a note
		// must not be swallowed on the first frames of the next mission)
		return;
	}
	Q_strncpyz( coopLastNote, cur, sizeof( coopLastNote ) );
	coopLastNoteTime = level.time;
	gi.Printf( "coop: note: %s\n", text );
}

// name an entity the way a breadcrumb wants it: who it is, not what it points at
const char *G_CoopEntName( const gentity_t *ent )
{
	if ( !ent )
	{
		return "none";
	}
	if ( ent->s.number < MAX_CLIENTS && ent->client && ent->inuse )
	{	// no health here: the text has to stay the same from frame to frame or the
		// rate limit above never catches a mover grinding the same body
		return va( "player %i '%s'", ent->s.number, ent->client->pers.netname );
	}
	return va( "ent %i '%s' targetname '%s'%s", ent->s.number,
		ent->classname ? ent->classname : "?",
		ent->targetname ? ent->targetname : "-",
		ent->client ? " (NPC)" : "" );
}

// number of connected players
int G_CoopNumPlayers( void )
{
	int n = 0;
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if ( g_entities[i].inuse && g_entities[i].client && g_entities[i].client->pers.connected == CON_CONNECTED )
		{
			n++;
		}
	}
	return n;
}

/*
================
G_CoopNearestPlayer

The closest connected player to org. With aliveOnly, dead players are
skipped. Never returns NULL: falls back to the host player (slot 0) so
callers written for the single-player global keep working.
================
*/
gentity_t *G_CoopNearestPlayer( const vec3_t org, qboolean aliveOnly )
{
	gentity_t	*best = &g_entities[0];
	float		bestDist = -1;

	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
		{
			continue;
		}
		if ( aliveOnly && ( ent->health <= 0 || G_CoopIsDowned( ent ) ) )
		{
			continue;
		}
		const float dist = DistanceSquared( org, ent->currentOrigin );
		if ( bestDist < 0 || dist < bestDist )
		{
			bestDist = dist;
			best = ent;
		}
	}
	return best;
}

// true if at least one connected player is alive
gentity_t *G_CoopPlayerSlot( int i )
{
	gentity_t *ent = &g_entities[i];
	if ( i < 0 || i >= MAX_CLIENTS || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED )
	{
		return NULL;
	}
	return ent;
}

qboolean G_CoopAnyPlayerAlive( void )
{
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		const gentity_t *ent = &g_entities[i];
		if ( G_CoopIsVersusEnemy( ent ) )
		{
			continue;	// versus: living hunters don't keep the mission alive
		}
		if ( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && ent->health > 0 )
		{
			return qtrue;
		}
	}
	return qfalse;
}

// true if org is in the PVS of any connected player (NPC removal / spawn checks)
qboolean G_CoopInAnyPlayerPVS( const vec3_t org )
{
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		const gentity_t *ent = &g_entities[i];
		if ( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && gi.inPVS( org, ent->currentOrigin ) )
		{
			return qtrue;
		}
	}
	return qfalse;
}

/*
==============================================================================
Death and respawn

Single player ends the mission when the player dies. In co-op a dead player
comes back a few seconds later beside a living teammate, keeping the loadout
they died with; the mission only fails once nobody is left alive.
==============================================================================
*/

#define COOP_RESPAWN_DELAY	4000

qboolean G_CoopIsUp( const gentity_t *ent );	// defined with the downed/revive code below
static void G_CoopAfterRespawn( gentity_t *ent );	// defined with the camera code below

static int				coopRespawnTime[MAX_CLIENTS];
static int				coopRespawnAtSpawn[MAX_CLIENTS];	// USE held while dead: checkpoint instead of beside a mate
static playerState_t	coopDeathState[MAX_CLIENTS];	// loadout snapshot taken at death

// co-op scoreboard counters (kills ride along on PERS_ENEMIES_KILLED); reset
// each map with the rest of the co-op state
static int	coopStatDowns[MAX_CLIENTS];
static int	coopStatRevives[MAX_CLIENTS];
static int	coopStatDeaths[MAX_CLIENTS];

static void G_CoopSendStats( int toSlot );

// true while this player is dead and waiting for a co-op respawn
qboolean G_CoopRespawnPending( const gentity_t *ent )
{
	return (qboolean)( G_CoopIsPlayer( ent ) && coopRespawnTime[ent->s.number] != 0 );
}

gentity_t *G_CoopLivingTeammate( const gentity_t *self )
{
	gentity_t	*best = NULL;
	float		bestDist = -1;
	const team_t	mySide = G_CoopSideOf( self );	// versus: only respawn beside your own side

	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( ent == self || !G_CoopIsUp( ent )
			|| ( G_CoopVersusOn() && G_CoopSideOf( ent ) != mySide ) )
		{
			continue;
		}
		const float dist = DistanceSquared( self->currentOrigin, ent->currentOrigin );
		if ( bestDist < 0 || dist < bestDist )
		{
			bestDist = dist;
			best = ent;
		}
	}
	return best;
}

/*
================
G_CoopPlayerDied

Called from player_die. Returns qtrue when the death is handled as a co-op
respawn, in which case the caller must not start the mission-failed flow.
================
*/
extern void G_AddWeaponModels( gentity_t *ent );		// g_client.cpp
extern void G_RemoveWeaponModels( gentity_t *ent );		// g_client.cpp
extern void WP_SaberCatch( gentity_t *self, gentity_t *saber, qboolean switchToSaber );	// wp_saber.cpp

/*
================
G_CoopBestOwnedWeapon

The weapon a player should be holding when he has none: his saber if he owns
one, else the best gun in STAT_WEAPONS. WP_NONE when he really carries nothing.
================
*/
int G_CoopBestOwnedWeapon( const playerState_t *ps )
{
	static const int order[] = {
		WP_SABER, WP_DISRUPTOR, WP_REPEATER, WP_FLECHETTE, WP_ROCKET_LAUNCHER,
		WP_BOWCASTER, WP_DEMP2, WP_BLASTER, WP_BLASTER_PISTOL, WP_BRYAR_PISTOL,
		WP_THERMAL, WP_TRIP_MINE, WP_DET_PACK, WP_STUN_BATON
	};
	int i;

	for ( i = 0; i < (int)( sizeof( order ) / sizeof( order[0] ) ); i++ )
	{
		if ( ps->stats[STAT_WEAPONS] & ( 1 << order[i] ) )
		{
			return order[i];
		}
	}
	return WP_NONE;
}

/*
================
G_CoopReEquip

Put a weapon back in a player's hands: after a revive (his saber may be lying
on the floor since a Force push, or player_die may have blanked the weapon
while he was bleeding out) and after a co-op respawn. Sends the choice to a
joiner's own cgame, which owns its weapon selection.
================
*/
void G_CoopReEquip( gentity_t *ent )
{
	playerState_t	*ps;
	int				wp;

	if ( !ent || !ent->client || ent->health <= 0 )
	{
		return;
	}
	ps = &ent->client->ps;

	if ( ps->stats[STAT_WEAPONS] & ( 1 << WP_SABER ) )
	{	// the hilt entity: caught back if it is on the ground or in flight,
		// rebuilt if the death freed it (WP_SaberUpdate frees a dropped saber
		// once its owner is dead)
		if ( ps->saberEntityNum > 0 && ps->saberEntityNum < ENTITYNUM_WORLD )
		{
			gentity_t *saberent = &g_entities[ps->saberEntityNum];

			if ( saberent->inuse && ( ps->saberInFlight || !( saberent->s.eFlags & EF_NODRAW ) ) )
			{
				WP_SaberCatch( ent, saberent, qfalse );
			}
		}
		else if ( ps->saberEntityNum == ENTITYNUM_NONE )
		{
			WP_SaberInitBladeData( ent );
		}
	}

	if ( ps->weapon > WP_NONE && ps->weapon < WP_NUM_WEAPONS && ( ps->stats[STAT_WEAPONS] & ( 1 << ps->weapon ) ) )
	{
		return;		// he is holding something he owns: leave him alone
	}
	wp = G_CoopBestOwnedWeapon( ps );
	if ( wp <= WP_NONE )
	{
		return;
	}
	ps->weapon = wp;
	ent->s.weapon = wp;
	ps->weaponstate = WEAPON_READY;
	G_RemoveWeaponModels( ent );
	G_AddWeaponModels( ent );
	G_CoopChangeWeapon( ent, wp );	// a joiner picks it in its own cgame
}

qboolean G_CoopPlayerDied( gentity_t *self )
{
	if ( !G_CoopIsPlayer( self ) )
	{
		return qfalse;
	}
	G_CoopClearDowned( self );
	if ( !G_CoopLivingTeammate( self ) && !G_CoopDownedActive() && !G_CoopIsVersusEnemy( self ) )
	{	// hunters always respawn - the mission only ends when the Jedi do
		return qfalse;	// last one standing in a plain game: the stock mission-failed screen
	}
	// with downed players the respawn waits for someone to be up again (G_CoopDownedFrame
	// reloads the checkpoint otherwise)
	const int delay = ( G_CoopDownedActive() && g_coopRespawnDelay->integer > 0 ) ? g_coopRespawnDelay->integer * 1000 : COOP_RESPAWN_DELAY;
	coopDeathState[self->s.number] = self->client->ps;
	if ( coopDeathState[self->s.number].weapon <= WP_NONE )
	{	// player_die ran first: it tossed the saber and blanked the weapon
		// (g_combat.cpp, TossClientItems) before we got here, so the snapshot
		// would bring the player back empty-handed. Remember what he still
		// owns instead - the saber first, it is what he was carrying.
		coopDeathState[self->s.number].weapon = G_CoopBestOwnedWeapon( &coopDeathState[self->s.number] );
	}
	coopRespawnTime[self->s.number] = level.time + delay;
	if ( G_CoopLivingTeammate( self ) )
	{
		G_CoopPrintTr( -1, "print", "%s ^3est tombe, retour dans %i s...\n", "%s ^3fell, back in %i s...\n", self->client->pers.netname, delay / 1000 );
	}
	else
	{	// nobody is up: the all-down screen says what happens next
		G_CoopPrintTr( -1, "print", "%s ^3est tombe.\n", "%s ^3fell.\n", self->client->pers.netname );
	}
	coopStatDeaths[self->s.number]++;	// coop: scoreboard
	G_CoopBroadcastStats();
	return qtrue;
}

extern qboolean G_SpawnOriginIsFree( vec3_t org );
extern vec3_t playerMins;
extern vec3_t playerMaxs;

/*
==============================================================================
Putting a player down next to another one

G_SpawnOriginIsFree() only sees clients that are solid, and right after a
cutscene the players often are not (CONTENTS_BODY cleared while the camera
runs). Two joiners gathered on the same host therefore used to get the very
same spot and ended up inside each other, unable to move: SP has no
player-versus-player unstick in PM_ code.

So a placement "pass" remembers every spot it hands out (plus where the live
players already stand) and no two players ever get the same one. What slips
through anyway - a script teleport, a respawn, a lift - is caught once per
frame by G_CoopUnstickPlayers() below.
==============================================================================
*/

#define COOP_PLACE_MAX		(2 * MAX_CLIENTS)	// where everybody stands now, plus where we put them
#define COOP_PLACE_SPACING	44.0f	// player box is 32 wide: leaves a real gap
#define COOP_PLACE_HEIGHT	56.0f	// two spots this far apart in Z do not fight

static vec3_t	coopPlaceTaken[COOP_PLACE_MAX];
static int		coopPlaceNumTaken = 0;
static qboolean	coopPlacePassOpen = qfalse;

static void G_CoopPlaceReserve( const vec3_t org )
{
	if ( coopPlaceNumTaken < COOP_PLACE_MAX )
	{
		VectorCopy( org, coopPlaceTaken[coopPlaceNumTaken] );
		coopPlaceNumTaken++;
	}
}

// true when a player standing at org would sit on a spot this pass already used
static qboolean G_CoopPlaceIsTaken( const vec3_t org )
{
	for ( int i = 0; i < coopPlaceNumTaken; i++ )
	{
		vec3_t d;
		VectorSubtract( org, coopPlaceTaken[i], d );
		if ( fabs( d[2] ) < COOP_PLACE_HEIGHT && ( d[0] * d[0] + d[1] * d[1] ) < ( COOP_PLACE_SPACING * COOP_PLACE_SPACING ) )
		{
			return qtrue;
		}
	}
	return qfalse;
}

// open a pass: the mate's spot and everybody else's are taken from the start
static void G_CoopPlaceBeginPass( const gentity_t *mate )
{
	coopPlaceNumTaken = 0;
	coopPlacePassOpen = qtrue;
	if ( mate )
	{
		G_CoopPlaceReserve( mate->currentOrigin );
	}
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		const gentity_t *other = G_CoopPlayerSlot( i );
		if ( other && other != mate && other->health > 0 )
		{
			G_CoopPlaceReserve( other->currentOrigin );
		}
	}
}

static void G_CoopPlaceEndPass( void )
{
	coopPlacePassOpen = qfalse;
	coopPlaceNumTaken = 0;
}

// settle org on the floor under it, so nobody is left hanging in the air
static void G_CoopPlaceDropToFloor( vec3_t org )
{
	trace_t	tr;
	vec3_t	below;

	VectorCopy( org, below );
	below[2] -= 64;
	gi.trace( &tr, org, playerMins, playerMaxs, below, ENTITYNUM_NONE, MASK_PLAYERSOLID & ~CONTENTS_BODY, (EG2_Collision)0, 0 );
	if ( !tr.allsolid && !tr.startsolid && tr.fraction < 1.0f )
	{
		VectorCopy( tr.endpos, org );
		org[2] += 1.0f;
	}
}

// ring of candidates around base, growing outwards; yawStart makes two players
// placed in the same pass walk the ring in a different order
static void G_CoopPlaceFindSpot( const vec3_t base, float yawStart, vec3_t out )
{
	static const float	radii[] = { 48.0f, 72.0f, 104.0f, 144.0f, 192.0f };
	vec3_t				cand;

	VectorCopy( base, cand );
	if ( G_SpawnOriginIsFree( cand ) && !G_CoopPlaceIsTaken( cand ) )
	{
		G_CoopPlaceDropToFloor( cand );
		if ( !G_CoopPlaceIsTaken( cand ) )
		{
			VectorCopy( cand, out );
			return;
		}
	}

	for ( int r = 0; r < (int)ARRAY_LEN( radii ); r++ )
	{
		for ( int a = 0; a < 8; a++ )
		{
			const float yaw = yawStart + a * 45.0f + ( ( r & 1 ) ? 22.5f : 0.0f );
			const float rad = yaw * ( M_PI / 180.0f );

			VectorCopy( base, cand );
			cand[0] += cos( rad ) * radii[r];
			cand[1] += sin( rad ) * radii[r];
			if ( !G_SpawnOriginIsFree( cand ) )
			{
				continue;
			}
			{	// and it must be reachable from the mate: no wall in between
				trace_t	path;
				gi.trace( &path, (float *)base, NULL, NULL, cand, ENTITYNUM_NONE, MASK_SOLID, (EG2_Collision)0, 0 );
				if ( path.fraction < 1.0f )
				{
					continue;
				}
			}
			G_CoopPlaceDropToFloor( cand );
			if ( G_CoopPlaceIsTaken( cand ) )
			{
				continue;
			}
			VectorCopy( cand, out );
			return;
		}
	}

	// nowhere clear at all: drop him on the mate and let the per-frame unstick sort it out
	VectorCopy( base, out );
}

// move a freshly spawned player next to mate (no-op without one)
void G_CoopPlaceBeside( gentity_t *ent, gentity_t *mate )
{
	if ( !ent || !ent->client || !mate || !mate->client || mate == ent )
	{
		return;
	}
	const qboolean ownPass = (qboolean)!coopPlacePassOpen;
	if ( ownPass )
	{
		G_CoopPlaceBeginPass( mate );
	}

	vec3_t base, origin;
	VectorCopy( mate->currentOrigin, base );
	base[2] += 9;
	// start each player on his own side of the ring (slot number), from the mate's right
	G_CoopPlaceFindSpot( base, mate->client->ps.viewangles[YAW] + 90.0f + ent->s.number * 47.0f, origin );
	G_CoopPlaceReserve( origin );

	VectorCopy( origin, ent->client->ps.origin );
	VectorCopy( origin, ent->currentOrigin );
	SetClientViewAngle( ent, mate->client->ps.viewangles );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	gi.linkentity( ent );

	if ( ownPass )
	{
		G_CoopPlaceEndPass();
	}
}

// versus: a hunter respawns beside an enemy NPC near the jedi front instead of
// trekking in from the map start - L4D2 director style. qfalse = nobody to
// stand next to, keep whatever spawnpoint ClientSpawn picked.
static qboolean G_CoopVersusPlace( gentity_t *ent )
{
	vec3_t	front;
	qboolean haveFront = qfalse;
	float	frontDist = -1;
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *jedi = &g_entities[i];
		if ( !G_CoopIsUp( jedi ) || G_CoopIsVersusEnemy( jedi ) )
		{
			continue;
		}
		const float dist = DistanceSquared( ent->currentOrigin, jedi->currentOrigin );
		if ( !haveFront || dist < frontDist )
		{	// the standing jedi nearest where the hunter fell is the front
			VectorCopy( jedi->currentOrigin, front );
			frontDist = dist;
			haveFront = qtrue;
		}
	}
	if ( !haveFront )
	{
		VectorCopy( ent->currentOrigin, front );	// nobody standing: gather near the corpse
	}

	gentity_t	*best = NULL;
	float		bestDist = -1;
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ )
	{
		gentity_t *npc = &g_entities[i];
		if ( !npc->inuse || !npc->client || npc->health <= 0 || npc->client->playerTeam != TEAM_ENEMY )
		{
			continue;
		}
		const float dist = DistanceSquared( npc->currentOrigin, front );
		if ( bestDist < 0 || dist < bestDist )
		{
			bestDist = dist;
			best = npc;
		}
	}
	if ( !best )
	{
		return qfalse;
	}
	G_CoopPlaceBeside( ent, best );
	return qtrue;
}

/*
================
G_CoopUnstickPlayers

Safety net, once per server frame. Two live players whose boxes overlap can
never free themselves (they are solid to each other and nothing pushes them
apart), so slide them apart a few units a frame until they are clear. Only
into room the world allows and never off a ledge: they must still be able to
block each other normally, just not stay stuck.
================
*/
#define COOP_UNSTICK_STEP	3.0f

static qboolean G_CoopUnstickGroundBelow( const vec3_t org )
{
	trace_t	tr;
	vec3_t	start, below;

	VectorCopy( org, start );
	VectorCopy( org, below );
	below[2] -= 64;
	gi.trace( &tr, start, playerMins, playerMaxs, below, ENTITYNUM_NONE, MASK_PLAYERSOLID & ~CONTENTS_BODY, (EG2_Collision)0, 0 );
	return (qboolean)( tr.fraction < 1.0f );
}

// slide ent along dir, ignoring other bodies (that is what we are escaping from);
// false when the world would not let him move that way at all
static qboolean G_CoopUnstickNudge( gentity_t *ent, const vec3_t dir, float dist )
{
	trace_t	tr;
	vec3_t	start, end;

	VectorCopy( ent->currentOrigin, start );
	VectorMA( start, dist, dir, end );
	gi.trace( &tr, start, ent->mins, ent->maxs, end, ent->s.number, MASK_PLAYERSOLID & ~CONTENTS_BODY, (EG2_Collision)0, 0 );
	if ( tr.allsolid || tr.startsolid )
	{	// already half inside the world (a mover pushed him into it, a teleport
		// dropped him low): the sweep tells us nothing, judge the target alone
		trace_t	dest;
		gi.trace( &dest, end, ent->mins, ent->maxs, end, ent->s.number, MASK_PLAYERSOLID & ~CONTENTS_BODY, (EG2_Collision)0, 0 );
		if ( dest.allsolid || dest.startsolid )
		{
			return qfalse;
		}
		VectorCopy( end, tr.endpos );
	}
	else if ( tr.fraction <= 0.0f )
	{
		return qfalse;		// the world holds him here; the other one will move instead
	}
	if ( !G_CoopUnstickGroundBelow( tr.endpos ) && G_CoopUnstickGroundBelow( start ) )
	{
		return qfalse;		// would shove him off a ledge
	}
	VectorCopy( tr.endpos, ent->currentOrigin );
	VectorCopy( tr.endpos, ent->client->ps.origin );
	gi.linkentity( ent );
	return qtrue;
}

static qboolean G_CoopOnSolidGround( const gentity_t *ent );

static qboolean G_CoopUnstickCandidate( const gentity_t *ent )
{
	if ( ent && ent->client && !G_CoopOnSolidGround( ent ) )
	{	// in the air (a jump, a fall, a lift): let physics settle it, never push.
		// This used to read ps.groundEntityNum, which on the host is stuck at
		// ENTITYNUM_NONE for every joiner - so no joiner was ever unstuck, which
		// is precisely the "two guests inside each other" JD reported.
		return qfalse;
	}
	if ( !ent || !ent->client || !ent->inuse || ent->health <= 0 )
	{
		return qfalse;
	}
	if ( !( ent->contents & CONTENTS_BODY ) )
	{
		return qfalse;	// non-solid (cutscene): nothing blocks anybody
	}
	if ( ent->client->ps.eFlags & EF_LOCKED_TO_WEAPON )
	{
		return qfalse;
	}
	if ( ent->s.m_iVehicleNum )
	{
		return qfalse;	// on a swoop: its own code owns the origin
	}
	return qtrue;
}

void G_CoopUnstickPlayers( void )
{
	if ( G_CoopNumPlayers() < 2 )
	{
		return;
	}
	for ( int a = 0; a < MAX_CLIENTS; a++ )
	{
		gentity_t *ea = G_CoopPlayerSlot( a );
		if ( !G_CoopUnstickCandidate( ea ) )
		{
			continue;
		}
		for ( int b = a + 1; b < MAX_CLIENTS; b++ )
		{
			gentity_t *eb = G_CoopPlayerSlot( b );
			if ( !G_CoopUnstickCandidate( eb ) )
			{
				continue;
			}
			// boxes overlapping on all three axes? (1 unit of slack: touching is fine)
			qboolean overlap = qtrue;
			for ( int k = 0; k < 3; k++ )
			{
				if ( eb->currentOrigin[k] + eb->mins[k] >= ea->currentOrigin[k] + ea->maxs[k] - 1.0f
					|| ea->currentOrigin[k] + ea->mins[k] >= eb->currentOrigin[k] + eb->maxs[k] - 1.0f )
				{
					overlap = qfalse;
					break;
				}
			}
			if ( !overlap )
			{
				continue;
			}

			vec3_t push;
			VectorSubtract( eb->currentOrigin, ea->currentOrigin, push );
			push[2] = 0;
			if ( VectorLength( push ) < 1.0f )
			{	// exactly on top of each other: a stable direction, the same one every frame
				const float rad = ( a * 71 + b * 37 ) * ( M_PI / 180.0f );
				push[0] = cos( rad );
				push[1] = sin( rad );
			}
			VectorNormalize( push );
			qboolean moved = G_CoopUnstickNudge( eb, push, COOP_UNSTICK_STEP );
			VectorScale( push, -1.0f, push );
			if ( G_CoopUnstickNudge( ea, push, COOP_UNSTICK_STEP ) )
			{
				moved = qtrue;
			}
			if ( !moved )
			{	// a wall holds them both on that line: try along it instead
				vec3_t side;
				side[0] = -push[1];
				side[1] = push[0];
				side[2] = 0;
				if ( !G_CoopUnstickNudge( eb, side, COOP_UNSTICK_STEP ) )
				{
					VectorScale( side, -1.0f, side );
					G_CoopUnstickNudge( ea, side, COOP_UNSTICK_STEP );
				}
			}
		}
	}
}

/*
================
G_CoopGatherJoiners

Teleport joiners beside the host: every one of them when 'all' is set,
otherwise only those the host cannot see (not in its PVS), alive and on foot.
================
*/
void G_CoopGatherJoiners( qboolean all )
{
	gentity_t *host = &g_entities[0];
	if ( !host->inuse || !host->client || host->health <= 0 )
	{
		return;
	}
	// one pass for the whole gather: each joiner gets his own spot around the host
	G_CoopPlaceBeginPass( host );
	for ( int i = 1; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ent->health <= 0 )
		{
			continue;
		}
		if ( ent->client->ps.eFlags & EF_LOCKED_TO_WEAPON )
		{
			continue;
		}
		if ( !all && gi.inPVS( host->currentOrigin, ent->currentOrigin ) )
		{
			continue;
		}
		G_CoopPlaceBeside( ent, host );
		gi.Printf( "coop: %s brought to the host (%.0f %.0f %.0f)\n", ent->client->pers.netname,
			ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] );
	}
	G_CoopPlaceEndPass();
}

/*
================
G_CoopStackPlayers

"coop_stack" (cheats, the host): drop every player on the host's exact spot,
without the kill box a real teleport carries. Nothing in the game does this -
it is how the tests check that G_CoopUnstickPlayers() always gets everybody
out of everybody else again.
================
*/
void G_CoopStackPlayers( void )
{
	gentity_t *host = &g_entities[0];
	if ( !host->inuse || !host->client )
	{
		return;
	}
	for ( int i = 1; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = G_CoopPlayerSlot( i );
		if ( !ent || ent->health <= 0 )
		{
			continue;
		}
		VectorCopy( host->currentOrigin, ent->client->ps.origin );
		VectorCopy( host->currentOrigin, ent->currentOrigin );
		VectorClear( ent->client->ps.velocity );
		ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
		gi.linkentity( ent );
		gi.Printf( "coop_stack: %s dropped on the host (%.0f %.0f %.0f)\n", ent->client->pers.netname,
			ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] );
	}
}

/*
================
G_CoopFollowHostTeleport

A script moved "player" (SET_ORIGIN / SET_COPY_ORIGIN, or a cutscene that
ended somewhere else): the campaign only ever relocates the host, so the
joiners left where they stood would be in the wrong room. Bring the ones
that are not beside it any more.
================
*/
void G_CoopFollowHostTeleport( const vec3_t from )
{
	gentity_t *host = &g_entities[0];
	if ( !host->inuse || !host->client || host->health <= 0 )
	{
		return;
	}
	if ( Distance( from, host->currentOrigin ) < 128.0f )
	{
		return;	// a script nudge, not a relocation
	}
	// one pass: two joiners following the same cutscene never land on one another
	G_CoopPlaceBeginPass( host );
	for ( int i = 1; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ent->health <= 0 )
		{
			continue;	// (a downed joiner keeps health 1 and follows: it stays revivable beside the host)
		}
		if ( G_CoopIsVersusEnemy( ent ) )
		{
			continue;	// versus: hunters don't follow the jedi's script teleports
		}
		if ( ent->client->ps.eFlags & EF_LOCKED_TO_WEAPON )
		{
			continue;
		}
		if ( ent->s.m_iVehicleNum )
		{
			continue;	// never rip a rider off its swoop
		}
		if ( Distance( ent->currentOrigin, host->currentOrigin ) < 256.0f && gi.inPVS( host->currentOrigin, ent->currentOrigin ) )
		{
			continue;
		}
		G_CoopPlaceBeside( ent, host );
		gi.Printf( "coop: %s follows the host's script teleport (%.0f %.0f %.0f)\n", ent->client->pers.netname,
			ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] );
	}
	G_CoopPlaceEndPass();
}

// "coop_tp": a joiner asks to be brought beside the host (stuck behind a locked
// door...); the host asks to be brought beside its nearest joiner (a script
// locked a door with the joiner on the far side).
void G_CoopTeleportCommand( gentity_t *ent )
{
	if ( !ent || !ent->client || ent->s.number >= MAX_CLIENTS || ent->health <= 0 || G_CoopIsDowned( ent )
		|| G_CoopIsVersusEnemy( ent ) )
	{	// versus hunters hunt on foot - no teleporting onto the jedi
		return;
	}
	gentity_t *target = NULL;
	if ( ent->s.number == 0 )
	{
		float best = 0;
		for ( int i = 1; i < MAX_CLIENTS; i++ )
		{
			gentity_t *other = &g_entities[i];
			if ( !G_CoopIsUp( other ) )
			{
				continue;
			}
			const float d = DistanceSquared( ent->currentOrigin, other->currentOrigin );
			if ( !target || d < best )
			{
				target = other;
				best = d;
			}
		}
	}
	else
	{
		target = &g_entities[0];
	}
	if ( !target || !target->inuse || !target->client )
	{
		return;
	}
	G_CoopPlaceBeside( ent, target );
	gi.Printf( "coop: %s teleported to %s on request\n", ent->client->pers.netname, target->client->pers.netname );
}

/*
================
G_CoopReleaseHeld

Sortir un joueur de la gueule de ce qui le tient. Les drapeaux "tenu par"
bloquent le mouvement (bg_pmove.cpp) et personne ne les efface : en solo la
mort rechargeait une carte neuve, en coop on revient sur place avec l'etat tel
quel - le joueur reste alors coince, sous le decor, sans pouvoir bouger.

Appele a la reapparition, au relevement, et par le ver des sables au moment ou
il avale un joueur (qu'il ne peut pas faire disparaitre).
================
*/
void G_CoopReleaseHeld( gentity_t *ent )
{
	const int held = EF_HELD_BY_SAND_CREATURE | EF_HELD_BY_RANCOR | EF_HELD_BY_WAMPA;

	if ( !G_CoopIsPlayer( ent ) )
	{
		return;
	}
	if ( !( ent->client->ps.eFlags & held ) && !( ent->s.eFlags & held ) && !ent->activator )
	{
		return;		// rien a defaire, cas courant
	}
	G_CoopNote( va( "releasing %s from a creature's grip", G_CoopEntName( ent ) ) );
	ent->client->ps.eFlags &= ~held;
	ent->s.eFlags &= ~held;
	// le monstre avait rendu le joueur intangible pour le trainer avec lui
	ent->contents = CONTENTS_BODY;
	ent->clipmask = MASK_PLAYERSOLID;
	ent->activator = NULL;
	ent->client->ps.viewEntity = 0;
}

static void G_CoopRespawn( gentity_t *ent )
{
	const int		slot = ent->s.number;
	playerState_t	*dead = &coopDeathState[slot];
	gentity_t		*mate = G_CoopLivingTeammate( ent );

	// coop: breadcrumb - a respawn walks ClientSpawn over a client that may still be
	// inside a mover, so the log must say it happened before anything else goes wrong
	G_CoopNote( va( "respawning %s %s", G_CoopEntName( ent ),
		coopRespawnAtSpawn[slot] ? "at the checkpoint (USE held)" : va( "beside %s", G_CoopEntName( mate ) ) ) );

	ClientSpawn( ent, eNO );
	G_CoopReleaseHeld( ent );	// coop: ClientSpawn ne touche pas a eFlags

	// bring the loadout back
	playerState_t *ps = &ent->client->ps;
	ps->stats[STAT_WEAPONS] = ( dead->stats[STAT_WEAPONS] & ~( 1 << WP_EMPLACED_GUN ) ) | ( 1 << WP_NONE );	// died on a gun: not a weapon to carry
	memcpy( ps->ammo, dead->ammo, sizeof( ps->ammo ) );
	memcpy( ps->inventory, dead->inventory, sizeof( ps->inventory ) );
	// security keys are a count in inventory[] plus the key names the door
	// panels compare against (INV_SecurityKeyCheck): bring the names back too,
	// otherwise the panel answers "wrong key" forever and the level is blocked
	memcpy( ps->security_key_message, dead->security_key_message, sizeof( ps->security_key_message ) );
	ps->stats[STAT_ITEMS] = dead->stats[STAT_ITEMS];
	memcpy( ps->forcePowerLevel, dead->forcePowerLevel, sizeof( ps->forcePowerLevel ) );
	ps->forcePowersKnown = dead->forcePowersKnown;
	ps->forcePowerMax = dead->forcePowerMax;
	ps->forcePower = ps->forcePowerMax;
	ps->saberStylesKnown = dead->saberStylesKnown;
	ps->saberAnimLevel = dead->saberAnimLevel;
	if ( dead->weapon > WP_NONE && dead->weapon < WP_NUM_WEAPONS && dead->weapon != WP_EMPLACED_GUN && ( ps->stats[STAT_WEAPONS] & ( 1 << dead->weapon ) ) )
	{
		ps->weapon = dead->weapon;
		G_RemoveWeaponModels( ent );
		G_AddWeaponModels( ent );
	}

	// come back beside a living teammate rather than at the map start -
	// unless the player held USE while dead: plain checkpoint spawn then
	// (the group may be standing in the fight that killed him)
	if ( G_CoopIsVersusEnemy( ent ) )
	{
		G_CoopVersusPlace( ent );	// hunter: materialize next to the NPCs near the front
	}
	else if ( coopRespawnAtSpawn[slot] )
	{
		G_CoopNote( va( "respawning %s at the checkpoint (USE held)", G_CoopEntName( ent ) ) );
	}
	else
	{
		G_CoopPlaceBeside( ent, mate );
	}
	coopRespawnAtSpawn[slot] = 0;
	ent->health = ps->stats[STAT_HEALTH] = ps->stats[STAT_MAX_HEALTH];
	G_CoopReEquip( ent );	// coop: never come back empty-handed
	G_CoopAfterRespawn( ent );
}

// run once per server frame
/*
================
G_CoopSaberWatch

Every frame, for every player: did the saber go away (inventory, saber
definition, hilt model while the saber is in hand)? Printed once per change,
with what was happening - a report from a real game ("pushed with a blaster in
hand, the saber is gone") that two local instances did not reproduce.
================
*/
static int	coopSaberSeen[MAX_CLIENTS];		// bit 0 in inventory, bit 1 has a definition, bit 2 hilt drawn in hand
static int	coopSaberHandSince[MAX_CLIENTS];
// coop: last hilt reattach attempt (level.time). Never hammer Ghoul2 every frame —
// failed attach + skin write used to corrupt memory and crash solo hosts in ucrtbase.
static int	coopHiltRetryAt[MAX_CLIENTS];

// the ghoul2 slot is still a model, not a hole left by G2API_RemoveGhoul2Model
static qboolean G_CoopHiltLive( const gentity_t *ent, int slot )
{
	if ( !ent || slot < 0 || slot >= ent->ghoul2.size() )
	{
		return qfalse;
	}
	return (qboolean)( ent->ghoul2[slot].mModelindex >= 0 );
}

void G_CoopSaberWatch( void )
{
	extern qboolean PM_InKnockDown( playerState_t *ps );

	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *p = G_CoopPlayerSlot( i );

		if ( !p || !p->client || p->health <= 0 )
		{
			coopSaberSeen[i] = 0;
			coopSaberHandSince[i] = 0;
			coopHiltRetryAt[i] = 0;
			continue;
		}
		playerState_t	*ps = &p->client->ps;
		int				now = 0;

		// coop: switching weapons removes the hilt and puts it back at the end of
		// the holster. If that second step misses (no model string, bolt not ready),
		// the saber stays gone while STAT_WEAPONS still has it. Put the hilt back.
		// Solo hosts hit this path too — retry at most every 250ms, and only when
		// the hand bolt exists so WP_SaberAddG2SaberModels can succeed.
		if ( ( ps->stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) && ps->weapon == WP_SABER && !ps->saberInFlight
			&& !G_CoopHiltLive( p, p->weaponModel[0] ) )
		{
			if ( p->handRBolt >= 0 && level.time >= coopHiltRetryAt[i] )
			{
				coopHiltRetryAt[i] = level.time + 250;
				if ( ( !ps->saber[0].model || !ps->saber[0].model[0] ) && ps->saber[0].name && ps->saber[0].name[0] )
				{
					WP_SaberParseParms( ps->saber[0].name, &ps->saber[0], qfalse );
				}
				if ( ps->saber[0].model && ps->saber[0].model[0] )
				{
					WP_SaberAddG2SaberModels( p );
				}
			}
		}
		else
		{
			coopHiltRetryAt[i] = 0;
		}

		if ( ps->stats[STAT_WEAPONS] & ( 1 << WP_SABER ) )
		{
			now |= 1;
		}
		if ( ps->saber[0].name && ps->saber[0].name[0] )
		{
			now |= 2;
		}
		if ( ps->weapon == WP_SABER && !ps->saberInFlight )
		{
			if ( !coopSaberHandSince[i] )
			{
				coopSaberHandSince[i] = level.time;
			}
			if ( p->weaponModel[0] >= 0 || level.time - coopSaberHandSince[i] < 1000 )
			{	// a second to draw it after a switch
				now |= 4;
			}
		}
		else
		{
			coopSaberHandSince[i] = 0;
			now |= 4;
		}
		if ( coopSaberSeen[i] && ( coopSaberSeen[i] & ~now ) )
		{
			gi.Printf( "coop: SABRE PERDU joueur %i (%s) : %s%s%s arme %i sabre '%s' vol %i etat %i ent %i modeles %i %i, pousse %i renverse %i anim %i/%i\n",
				i, p->client->pers.netname,
				( coopSaberSeen[i] & ~now & 1 ) ? "[inventaire] " : "",
				( coopSaberSeen[i] & ~now & 2 ) ? "[definition] " : "",
				( coopSaberSeen[i] & ~now & 4 ) ? "[modele en main] " : "",
				ps->weapon, ps->saber[0].name ? ps->saber[0].name : "", ps->saberInFlight, ps->saberEntityState, ps->saberEntityNum,
				p->weaponModel[0], p->weaponModel[1], p->forcePushTime > level.time ? 1 : 0, PM_InKnockDown( ps ) ? 1 : 0,
				ps->legsAnim, ps->torsoAnim );
		}
		coopSaberSeen[i] = now;
	}
}

void G_CoopRunRespawns( void )
{
	for ( int slot = 0; slot < MAX_CLIENTS; slot++ )
	{
		if ( !coopRespawnTime[slot] || level.time < coopRespawnTime[slot] )
		{
			continue;
		}
		if ( !G_CoopAnyPlayerUp() )
		{
			continue;	// nobody to come back beside: the all-down flow decides (G_CoopDownedFrame)
		}
		coopRespawnTime[slot] = 0;
		gentity_t *ent = &g_entities[slot];
		if ( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && ent->health <= 0 )
		{
			G_CoopRespawn( ent );
			G_CoopPrintTr( -1, "print", "^2%s ^7est de retour\n", "^2%s ^7is back\n", ent->client->pers.netname );
		}
	}
}

/*
================
G_CoopRespawnPendingNow

A level transition is about to be recorded: SV_Player_EndOfLevelSave
snapshots every client's playerState and KEEP_PREV spawn points import
STAT_HEALTH as is, so anyone still waiting for a co-op respawn must come
back before it runs (the "maptransition" command executes before the next
G_RunFrame, G_CoopRunRespawns would be too late).
================
*/
static void G_CoopRevive( gentity_t *target, gentity_t *reviver );	// downed/revive code below

void G_CoopRespawnPendingNow( void )
{
	for ( int slot = 0; slot < MAX_CLIENTS; slot++ )
	{
		gentity_t *downed = &g_entities[slot];
		if ( G_CoopIsDowned( downed ) )
		{	// a downed player would carry its 1 hp into the next level: stand it up
			G_CoopRevive( downed, NULL );
			gi.Printf( "coop: %s stood up for the level transition\n", downed->client->pers.netname );
		}
		if ( !coopRespawnTime[slot] )
		{
			continue;
		}
		coopRespawnTime[slot] = 0;
		coopRespawnAtSpawn[slot] = 0;
		gentity_t *ent = &g_entities[slot];
		if ( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && ent->health <= 0 )
		{
			G_CoopRespawn( ent );
			gi.Printf( "coop: %s respawned for the level transition\n", ent->client->pers.netname );
		}
	}
}

/*
==============================================================================
Cinematic camera replication

Cutscenes run entirely on the host: ICARUS drives the cgame camera
(cg_camera.cpp) through shared memory, which a remote client never sees.
While the host is in a camera, or a scripted screen fade is up outside one
(fade to black before end_level, pre-black before a scene), a broadcast
entity mirrors the host's camera into every snapshot; the remote cgame turns
it back into its own client_camera (cg_coop.cpp):

  s.frame & 1      camera active (else only the fade is meaningful)
  pos/apos.trBase  view origin / angles before the shake (coopCameraOrg/Angles)
  origin2[0]       script FOV, before CG_CalcFOVFromX (coopCameraFovX)
  origin2[1]       cinematic bar height, time2 = bar alpha x 255
  angles2, origin2[2]  fade colour rgb and alpha
==============================================================================
*/

#include "../cgame/cg_local.h"
#include "../cgame/cg_camera.h"

static qboolean coopMissionFailedSent[MAX_CLIENTS];	// per client: told about the mission-failed screen
static gentity_t *coopCameraEnt = NULL;
static qboolean coopCameraWasIn = qfalse;
extern float coopCameraFovX;		// cg_camera.cpp
extern vec3_t coopCameraOrg, coopCameraAngles;

/*
================
G_CoopFadeClient

Screen fades the game code aims at one player (falling deaths: trigger_hurt,
target_kill) go to that player's screen only: the host's own cgame for slot
0, a "fade" server command for a joiner. Both fade from the current colour.
A fade meant for the host alone is not broadcast by G_CoopUpdateCamera,
until the next script fade (CameraFade), which is for everyone.
================
*/
void G_CoopFadePlayer( gentity_t *ent, const vec4_t src, const vec4_t dst, int ms );
static qboolean coopFadeLocal = qfalse;

void G_CoopFadeClient( const gentity_t *ent, const vec4_t dst, int ms )
{
	if ( !ent || ent->s.number >= MAX_CLIENTS )
	{
		return;
	}
	vec4_t src;
	VectorCopy4( client_camera.fade_color, src );
	G_CoopFadePlayer( (gentity_t *)ent, src, dst, ms );	// (marks a host-local fade below)
}

// a script fade (CQuake3GameInterface::CameraFade) is for everyone
void G_CoopFadeShared( void )
{
	coopFadeLocal = qfalse;
}
static vec3_t coopCameraHostOrigin;					// where the host stood when the cutscene started

// A held fade is a dead end: the screen stays white (or black) and nothing in
// the co-op flow takes it away, because the stock flow counted on the player
// dying and on the mission-failed screen covering it. Outside a cutscene, with
// no end-of-mission panel and somebody still playing, we lift it ourselves.
#define COOP_FADE_STUCK_MS	2500

static void G_CoopFadeWatchdog( void )
{
	static int	fullSince = 0;

	if ( in_camera || cg.missionStatusShow || G_CoopIsLobby()
		|| client_camera.fade_color[3] < 1.0f || ( client_camera.info_state & CAMERA_FADING ) )
	{
		fullSince = 0;		// no fade, still moving, or a screen that owns it
		return;
	}
	if ( !G_CoopAnyPlayerAlive() || !G_CoopAnyPlayerUp() )
	{
		fullSince = 0;		// everybody is out: the all-down panel owns the screen
		return;
	}
	if ( !fullSince )
	{
		fullSince = level.time;
		return;
	}
	if ( level.time - fullSince < COOP_FADE_STUCK_MS )
	{
		return;
	}
	fullSince = 0;
	G_CoopNote( "fade watchdog: ecran fige sur un fondu plein, on le leve" );
	CG_CoopFadeIn( 700 );	// the host; the joiners follow through the ET_COOPCAMERA broadcast
	for ( int i = 1; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = G_CoopPlayerSlot( i );

		if ( ent )
		{
			G_CoopFadeInPlayer( ent, 700 );
		}
	}
}

void G_CoopUpdateCamera( void )
{
	// a fade running outside a camera is shared too, unless it is the host's own (G_CoopFadeClient)
	const qboolean fading = (qboolean)( !coopFadeLocal && ( client_camera.fade_color[3] > 0.0f || ( client_camera.info_state & CAMERA_FADING ) != 0 ) );

	G_CoopFadeWatchdog();

	if ( !in_camera && !fading )
	{
		if ( coopCameraEnt )
		{
			G_FreeEntity( coopCameraEnt );
			coopCameraEnt = NULL;
			// the cutscene may have relocated the host (ROFF, SET_ORIGIN during the fade...)
			G_CoopFollowHostTeleport( coopCameraHostOrigin );
		}
		coopCameraWasIn = qfalse;
		return;
	}

	if ( !coopCameraEnt || !coopCameraEnt->inuse || coopCameraEnt->s.eType != ET_COOPCAMERA )
	{
		// a cutscene starts: scripts lock doors behind the host and drive the
		// story from where it stands. A joiner left out of sight (a door that
		// closed on it, a fall, a detour) would be stranded, so bring it along.
		G_CoopGatherJoiners( qfalse );
		VectorCopy( g_entities[0].currentOrigin, coopCameraHostOrigin );
		coopCameraEnt = G_Spawn();
		if ( !coopCameraEnt )
		{
			return;
		}
		coopCameraEnt->classname = "coop_camera";
		coopCameraEnt->s.eType = ET_COOPCAMERA;
		coopCameraEnt->svFlags |= SVF_BROADCAST;	// sent regardless of PVS
		coopCameraEnt->contents = 0;
		coopCameraEnt->clipmask = 0;
	}
	if ( in_camera && !coopCameraWasIn )
	{
		// a cutscene starts: scripts lock doors behind the host and drive the
		// story from where it stands. A joiner left out of sight (a door that
		// closed on it, a fall, a detour) would be stranded, so bring it along.
		G_CoopGatherJoiners( qfalse );
	}
	coopCameraWasIn = (qboolean)in_camera;

	gentity_t *cam = coopCameraEnt;
	cam->s.frame = in_camera ? 1 : 0;
	if ( in_camera )
	{
		VectorCopy( coopCameraOrg, cam->s.origin );
		VectorCopy( coopCameraOrg, cam->s.pos.trBase );
		VectorCopy( coopCameraOrg, cam->currentOrigin );
		VectorCopy( coopCameraAngles, cam->s.angles );
		VectorCopy( coopCameraAngles, cam->s.apos.trBase );
		cam->s.origin2[0] = coopCameraFovX;
	}
	cam->s.pos.trType = cam->s.apos.trType = TR_INTERPOLATE;
	cam->s.origin2[1] = client_camera.bar_height;
	cam->s.time2 = (int)( client_camera.bar_alpha * 255.0f );
	cam->s.origin2[2] = client_camera.fade_color[3];
	VectorCopy( client_camera.fade_color, cam->s.angles2 );
	gi.linkentity( cam );
}

/*
================
G_CoopShake

Camera shakes are host-cgame state (CGCam_Shake). Shake the host as before
and broadcast the same shake as a temp entity for the joiners
(EV_COOP_SHAKE): angles2[0] = intensity (per unit of distance when a range
is given), angles2[1] = range (0 = everyone, else each joiner scales by its
own distance to the origin, as the AI does with "the player"),
time2 = duration, otherEntityNum = one client or ENTITYNUM_NONE.
================
*/
static void G_CoopShakeEvent( const vec3_t origin, float intensity, float range, int duration, int clientNum )
{
	if ( G_CoopNumPlayers() < 2 )
	{
		return;
	}
	gentity_t *te = G_TempEntity( origin, EV_COOP_SHAKE );
	te->svFlags |= SVF_BROADCAST;
	te->s.angles2[0] = intensity;
	te->s.angles2[1] = range;
	te->s.time2 = duration;
	te->s.otherEntityNum = clientNum;
}

// every player's screen (scripts: CAMERA SHAKE)
void G_CoopShakeAll( float intensity, int duration )
{
	CGCam_Shake( intensity, duration );
	G_CoopShakeEvent( g_entities[0].currentOrigin, intensity, 0.0f, duration, ENTITYNUM_NONE );
}

// one player's screen
void G_CoopShakeClient( const gentity_t *ent, float intensity, int duration )
{
	if ( !ent || ent->s.number >= MAX_CLIENTS )
	{
		return;
	}
	if ( ent->s.number == 0 )
	{
		CGCam_Shake( intensity, duration );
		return;
	}
	G_CoopShakeEvent( ent->currentOrigin, intensity, 0.0f, duration, ent->s.number );
}

// the AI's "shake the player by its distance": perUnit x distance for every player within range
void G_CoopShakeNear( const vec3_t origin, float perUnit, float range, int duration )
{
	const gentity_t *host = &g_entities[0];
	if ( host->inuse && host->client )
	{
		const float dist = Distance( host->currentOrigin, origin );
		if ( dist < range )
		{
			CGCam_Shake( perUnit * dist, duration );
		}
	}
	G_CoopShakeEvent( origin, perUnit, range, duration, ENTITYNUM_NONE );
}

/*
================
Screen fades that belong to one player

A falling death (trigger_hurt FALLING, target_kill) fades the screen of the
player who fell: the host's own cgame for slot 0 (game and cgame are one
module here), a "fade" server command for a remote client. The respawn
fades back in the same way.
================
*/
void G_CoopFadePlayer( gentity_t *ent, const vec4_t src, const vec4_t dst, int ms )
{
	if ( !G_CoopIsPlayer( ent ) )
	{
		return;
	}
	if ( ent->s.number == 0 )
	{
		coopFadeLocal = qtrue;
		CGCam_Fade( (float *)src, (float *)dst, ms );
	}
	else
	{
		gi.SendServerCommand( ent->s.number, "fade %i %g %g %g %g %g %g %g %g", ms, src[0], src[1], src[2], src[3], dst[0], dst[1], dst[2], dst[3] );
	}
}

void G_CoopFadeInPlayer( gentity_t *ent, int ms )
{
	if ( !G_CoopIsPlayer( ent ) )
	{
		return;
	}
	if ( ent->s.number == 0 )
	{
		CG_CoopFadeIn( ms );
	}
	else
	{
		gi.SendServerCommand( ent->s.number, "fadein %i", ms );
	}
}

/*
================
G_CoopMirrorCvars

The cinematic skip (g_active.cpp G_StartCinematicSkip, CGCam_Disable) and
the script timescale (Q3_SetTimeScale) are host cvars; the joiners learn
them as "skip N" / "ts V" server commands when they change.
================
*/
static int		coopSentSkip = 0;
static float	coopSentTimescale = 1.0f;

static void G_CoopSendJoiners( const char *cmd )
{
	for ( int i = 1; i < MAX_CLIENTS; i++ )
	{
		const gentity_t *cl = &g_entities[i];
		if ( cl->inuse && cl->client && cl->client->pers.connected == CON_CONNECTED )
		{
			gi.SendServerCommand( i, "%s", cmd );
		}
	}
}

void G_CoopMirrorCvars( void )
{
	extern cvar_t *g_skippingcin;
	extern cvar_t *g_timescale;

	if ( g_skippingcin->integer != coopSentSkip )
	{
		coopSentSkip = g_skippingcin->integer;
		G_CoopSendJoiners( va( "skip %i", coopSentSkip ) );
	}
	if ( g_timescale->value != coopSentTimescale )
	{
		coopSentTimescale = g_timescale->value;
		G_CoopSendJoiners( va( "ts %g", coopSentTimescale ) );
	}
}

/*
================
Rider / gunner feedback routed to the right client

Vehicles and emplaced guns switch the rider's view, weapon selection and
print "press USE to exit" straight into the host's cgame. For a remote
client the same goes through reliable commands ("tp", "wp", "cp").
================
*/
void G_CoopRiderThirdPerson( gentity_t *rider, qboolean on )
{
	if ( !G_CoopIsPlayer( rider ) )
	{
		return;
	}
	if ( rider->s.number == 0 )
	{
		gi.cvar_set( "cg_thirdperson", on ? "1" : "0" );
	}
	else
	{
		gi.SendServerCommand( rider->s.number, "tp %i", on ? 1 : 0 );
	}
}

// a weapon change's view switch (PM_CoopViewForWeapon): 1 third person, -1 a gun
// (first person if the player's own cg_gunAutoFirst says so, CG_CoopThirdPerson_f)
void G_CoopRiderViewMode( gentity_t *rider, int mode )
{
	if ( !G_CoopIsPlayer( rider ) )
	{
		return;
	}
	if ( rider->s.number == 0 )
	{
		if ( mode > 0 )
		{
			gi.cvar_set( "cg_thirdperson", "1" );
		}
		else if ( mode == 0 || cg_gunAutoFirst.integer )
		{
			gi.cvar_set( "cg_thirdperson", "0" );
		}
		return;
	}
	gi.SendServerCommand( rider->s.number, "tp %i", mode > 0 ? 1 : ( mode < 0 ? -1 : 0 ) );
}

void G_CoopRiderCenterPrint( gentity_t *rider, const char *str, float yFrac )
{
	if ( !G_CoopIsPlayer( rider ) )
	{
		return;
	}
	if ( rider->s.number == 0 )
	{
		CG_CenterPrint( str, SCREEN_HEIGHT * yFrac );
	}
	else
	{
		gi.SendServerCommand( rider->s.number, "cp %s %g", str, yFrac );
	}
}

extern void CG_ChangeWeapon( int num );
void G_CoopRiderWeapon( gentity_t *rider, int wp )
{
	if ( !G_CoopIsPlayer( rider ) )
	{
		return;
	}
	if ( rider->s.number == 0 )
	{
		CG_ChangeWeapon( wp );
	}
	else
	{
		gi.SendServerCommand( rider->s.number, "wp %i", wp );
	}
}

// the host's cgame state after a co-op respawn: no death fade, no locked camera
static void G_CoopAfterRespawn( gentity_t *ent )
{
	if ( in_camera )
	{
		return;
	}
	G_CoopFadeInPlayer( ent, 700 );
	// missionStatusDeadTime is set when a death ends the mission and is never
	// cleared inside a level by the stock code: left alone, the first wipe of
	// the level disables the host's fade-in below for good.
	if ( G_CoopAnyPlayerAlive() )
	{
		cg.missionStatusDeadTime = 0;
	}
	if ( ent->s.number == 0 )
	{
		cg.overrides.active &= ~CG_OVERRIDE_3RD_PERSON_CDP;
		cg.overrides.thirdPersonCameraDamp = 0;
	}
	else if ( client_camera.fade_color[3] > 0.0f && !cg.missionStatusShow && !cg.missionStatusDeadTime )
	{	// a script death fade (t1_rail/death_fade) tripped by the joiner landed on the
		// alive host's screen; outside a cutscene it has no purpose once the joiner is back
		CG_CoopFadeIn( 700 );
	}
}

// forget the camera entity when the level goes away
void G_CoopResetCamera( void )
{
	coopSentSkip = 0;
	coopSentTimescale = 1.0f;
	coopCameraEnt = NULL;
	coopCameraWasIn = qfalse;
	coopFadeLocal = qfalse;
	VectorClear( coopCameraHostOrigin );
	memset( coopMissionFailedSent, 0, sizeof( coopMissionFailedSent ) );
	memset( coopRespawnTime, 0, sizeof( coopRespawnTime ) );
	memset( coopRespawnAtSpawn, 0, sizeof( coopRespawnAtSpawn ) );
	G_CoopResetDowned();
}

/*
================
G_CoopUpdateMissionFailed

The mission-failed screen is raised straight in the host's cgame (a script
via Q3_SetMissionFailed, or the last player dying: cg.missionStatusShow /
cg.missionStatusDeadTime). Tell the remote clients once, with the status
text, so they show the same screen.
================
*/
void G_CoopUpdateMissionFailed( void )
{
	extern int statusTextIndex;

	const qboolean allDead = (qboolean)( !G_CoopAnyPlayerAlive() && cg.missionStatusDeadTime && cg.missionStatusDeadTime < level.time );
	if ( !cg.missionStatusShow && !allDead )
	{
		return;
	}
	for ( int i = 1; i < MAX_CLIENTS; i++ )
	{
		const gentity_t *ent = &g_entities[i];
		if ( !coopMissionFailedSent[i] && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED )
		{
			coopMissionFailedSent[i] = qtrue;
			gi.SendServerCommand( i, "cad -1" );				// no downed overlay under the screen
			gi.SendServerCommand( i, "mf %i", statusTextIndex );	// CG_MissionFailed opens coopAllDownClient with the reason
		}
	}
}

/*
================
G_CoopClientBegin

A client (re)entered the world: forget what we told the previous occupant.
================
*/
void G_CoopClientBegin( const gentity_t *ent )
{
	G_CoopLobbyClientBegin( ent );
	if ( ent->s.number >= 0 && ent->s.number < MAX_CLIENTS )
	{
		coopMissionFailedSent[ent->s.number] = qfalse;
	}
	if ( ent->s.number > 0 && ent->s.number < MAX_CLIENTS )
	{
		// coop: from here on the host is running a session that can die on something
		// only two players ever reach, and it dies without an error of its own (see
		// G_CoopNote). Start writing qconsole.log, flushed line by line, so the last
		// lines say where it was. Solo never gets here, so solo keeps the stock
		// behaviour of writing nothing.
		if ( gi.cvar( "g_coopLog", "1", CVAR_ARCHIVE )->integer && !gi.cvar( "logfile", "0", CVAR_TEMP )->integer )
		{
			gi.cvar_set( "logfile", "2" );
			gi.Printf( "coop: logging this session to base/qconsole.log (a crash leaves its last lines there)\n" );
		}
		// what G_CoopMirrorCvars already told the others
		if ( coopSentSkip )
		{
			gi.SendServerCommand( ent->s.number, "skip %i", coopSentSkip );
		}
		if ( coopSentTimescale != 1.0f )
		{
			gi.SendServerCommand( ent->s.number, "ts %g", coopSentTimescale );
		}
		if ( G_CoopVersusOn() )
		{	// versus: tell the joiner they can hunt instead of help
			G_CoopPrintTr( ent->s.number, "print", "^3Mode versus^7 - ^5coop_team enemy^7 pour jouer les mechants.\n",
				"^3Versus mode^7 - type ^5coop_team enemy^7 to hunt the jedi.\n" );
		}
	}
	if ( ent->s.number >= 0 && ent->s.number < MAX_CLIENTS )
	{
		G_CoopSendStats( ent->s.number );	// coop: the joiner fills its scoreboard table
	}
}

/*
==============================================================================
Downed players and revives (PUBG style)

With g_coopDowned and at least two players, a killing blow no longer kills:
the player drops to the ground (health 1, knockdown animation held, immune,
no input but the view) and bleeds out for g_coopBleedOut seconds. A standing
teammate within g_coopReviveRange holding +coop_revive for g_coopReviveTime
seconds brings it back up with part of its health. Bleeding out is a real
death: the existing respawn brings the player back beside someone still up.
When nobody is up any more the host is offered the last checkpoint
(load *respawn) and, after g_coopAllDownAuto seconds, it is reloaded anyway.

The state lives here on the host; the player itself reads it from its
playerState stats (STAT_COOP_DOWN / STAT_COOP_REVIVE / STAT_COOP_REVIVER) and
everyone sees a downed teammate through the PW_COOP_DOWNED powerup bit of
its entityState (cg_coop.cpp draws the markers and the bars).
==============================================================================
*/

extern void NPC_SetAnim( gentity_t *ent, int setAnimParts, int anim, int setAnimFlags, int iBlend = SETANIM_BLEND_DEFAULT );
extern qboolean PM_InKnockDownNoGetup( playerState_t *ps );
extern void WP_ForcePowerStop( gentity_t *self, forcePowers_t forcePower );
extern void NPC_SetPainEvent( gentity_t *self );
extern void G_ClearEnemy( gentity_t *self );
extern bool in_camera;

typedef struct coopDown_s {
	int		downTime;			// level.time the player went down, 0 = up
	int		bleedOutTime;		// level.time it dies for real
	int		reviverNum;			// who is reviving it, else ENTITYNUM_NONE
	int		reviveStartTime;
	int		revivingNum;		// (standing players) who I am reviving, else ENTITYNUM_NONE
	vec3_t	safeOrigin;			// last spot where he stood on solid ground, healthy
	int		safeTime;			// level.time of that spot, 0 = none remembered yet
	float	safeYaw;
	int		giveUpTime;			// level.time the downed player started holding JUMP to give up, 0 = not
} coopDown_t;

#define COOP_GIVEUP_HOLD	1500	// ms of JUMP held on the ground to stop waiting for a revive

static coopDown_t	coopDown[MAX_CLIENTS];
static qboolean		coopPendingAutosave;	// a target_autosave fired with the host down (deferred checkpoint below)
static void G_CoopRunDeferredAutosave( void );
static int			coopAllDownTime;		// level.time nobody was up any more, 0 = someone is
static int			coopAllDownLastSec = -1;
static int			coopBleedingOutNum = -1;	// the player G_CoopDownedFrame is killing for real right now
static int			coopReloadTime;			// level.time the checkpoint reload was asked (0 = none)
static int			coopReloadTries;

cvar_t	*g_coopDowned;
cvar_t	*g_coopBleedOut;
cvar_t	*g_coopReviveTime;
cvar_t	*g_coopReviveRange;
cvar_t	*g_coopReviveHealth;
cvar_t	*g_coopRespawnDelay;
cvar_t	*g_coopAllDownAuto;
cvar_t	*g_coopPitRescue;		// 1 = a fall into a pit downs you on your last safe ground
cvar_t	*g_coopPaksGen;		// coop: the engine bumps it when a pk3 enters the search path at runtime
extern cvar_t	*g_coopFriendlyFire;
cvar_t	*g_coopSkipCinematics;	// 1 = the host auto-skips every cinematic for everyone
cvar_t	*g_coopCatchup;		// 1 = coop_cometome teleports you beside a living teammate
cvar_t	*g_coopPickupShare;	// percent of ammo/health/armor pickups shared with nearby teammates
cvar_t	*g_coopEnemyHealth;	// enemy NPC hit points scale (1.0 = stock)
cvar_t	*g_coopNGPlus;
cvar_t	*g_coopWaves;		// wave mode: 0 = off, 1 = endless enemy waves
cvar_t	*g_coopHolocrons;	// scattered holocron pickups per map (0 = off)
cvar_t	*g_mpPhysics;		// 1 = multiplayer movement constants for every player
cvar_t	*g_coopVersus;		// 1 = players may join TEAM_ENEMY and hunt the Jedi (coop_team)
cvar_t	*g_coopBots;		// companion NPC allies kept with the party (0 = off)
static int		coopBotsCheck;		// bot frame throttle
static int		coopBotsRespawn;	// level.time a fallen companion gets replaced
static qboolean	coopBotsFilled;		// per-map initial spawn done

void G_CoopInitDownedCvars( void )
{
	g_coopDowned = gi.cvar( "g_coopDowned", "1", CVAR_ARCHIVE );
	g_coopBleedOut = gi.cvar( "g_coopBleedOut", "60", CVAR_ARCHIVE );
	g_coopReviveTime = gi.cvar( "g_coopReviveTime", "3", CVAR_ARCHIVE );
	g_coopReviveRange = gi.cvar( "g_coopReviveRange", "80", CVAR_ARCHIVE|CVAR_SERVERINFO );	// serverinfo: the HUD prompt uses the same range
	g_coopReviveHealth = gi.cvar( "g_coopReviveHealth", "40", CVAR_ARCHIVE );
	g_coopRespawnDelay = gi.cvar( "g_coopRespawnDelay", "10", CVAR_ARCHIVE );
	g_coopAllDownAuto = gi.cvar( "g_coopAllDownAuto", "20", CVAR_ARCHIVE );
	g_coopPitRescue = gi.cvar( "g_coopPitRescue", "1", CVAR_ARCHIVE );
	g_coopPaksGen = gi.cvar( "cl_coopPaksGen", "0", CVAR_ROM );
	// serverinfo: a joiner can read what the host decided
	g_coopFriendlyFire = gi.cvar( "g_coopFriendlyFire", "1", CVAR_ARCHIVE|CVAR_SERVERINFO );
	// serverinfo: joiners see when the host auto-skips the story
	g_coopSkipCinematics = gi.cvar( "g_coopSkipCinematics", "0", CVAR_ARCHIVE|CVAR_SERVERINFO );
	// serverinfo: the joiners play by the same house rules as the host
	g_coopCatchup = gi.cvar( "g_coopCatchup", "1", CVAR_ARCHIVE|CVAR_SERVERINFO );
	g_coopPickupShare = gi.cvar( "g_coopPickupShare", "0", CVAR_ARCHIVE|CVAR_SERVERINFO );
	g_coopEnemyHealth = gi.cvar( "g_coopEnemyHealth", "1", CVAR_ARCHIVE );
	// serverinfo: joiners and the site see which NG+ cycle the host is on
	g_coopNGPlus = gi.cvar( "g_coopNGPlus", "0", CVAR_ARCHIVE|CVAR_SERVERINFO );
	g_coopWaves = gi.cvar( "g_coopWaves", "0", CVAR_ARCHIVE|CVAR_SERVERINFO );
	// serverinfo: joiners see how many holocrons the host scattered on the map
	g_coopHolocrons = gi.cvar( "g_coopHolocrons", "8", CVAR_ARCHIVE|CVAR_SERVERINFO );
	// serverinfo: joiners read the movement rules in CS_COOP_PHYSICS for prediction
	g_mpPhysics = gi.cvar( "g_mpPhysics", "0", CVAR_ARCHIVE|CVAR_SERVERINFO );
	// serverinfo: versus sessions show up in server info and the scoreboard
	g_coopVersus = gi.cvar( "g_coopVersus", "0", CVAR_ARCHIVE|CVAR_SERVERINFO );
	// serverinfo: joiners see how many AI companions the party keeps
	g_coopBots = gi.cvar( "g_coopBots", "0", CVAR_ARCHIVE|CVAR_SERVERINFO );
	coopBotsFilled = qfalse;
	coopBotsCheck = 0;
	coopBotsRespawn = 0;
	G_CoopGravPortalInit();	// cheat toys: clear held props, thrown arms, portal tables
	memset( coopStatDowns, 0, sizeof( coopStatDowns ) );
	memset( coopStatRevives, 0, sizeof( coopStatRevives ) );
	memset( coopStatDeaths, 0, sizeof( coopStatDeaths ) );
}

/*
==============================================================================
Movement physics (g_mpPhysics)

bg_pmove reads the bg_mpPhysics flag every move. The host's game sets it from
the cvar here; joiners set it in cg_predict.cpp from CS_COOP_PHYSICS, so client
prediction and server simulation agree whichever mode the host picked.
==============================================================================
*/

void G_CoopPhysicsFrame( void )
{
	static int	csState = -1;
	const int	mp = g_mpPhysics->integer > 0;

	bg_mpPhysics = (qboolean)mp;
	if ( mp != csState )
	{
		csState = mp;
		gi.SetConfigstring( CS_COOP_PHYSICS, mp ? "1" : "0" );
	}
}

/*
==============================================================================
Friendly fire between players (g_coopFriendlyFire)

0 = off. A player's shot does nothing at all to a teammate. Note what the
    stock ally path did instead: G_Damage drains the armour (CheckArmor) before
    the TEAM_PLAYER branch decides not to apply the damage, so a teammate's
    shots emptied the green shield bar and never touched the health.
1 = on (the default). Full damage: the victim goes down / dies through the
    normal co-op path (G_CoopTryDown, then the respawn).
2 = half damage.

Only between two connected players. An NPC ally shooting a player, or a player
shooting an NPC ally, keeps the stock behaviour (accidental hits are ignored
and the ally does not turn on you).
==============================================================================
*/

cvar_t	*g_coopFriendlyFire;

// percentage of the damage one player does to another, or -1 when this is not
// a player-versus-player hit at all (nothing to change)
int G_CoopFriendlyFireScale( const gentity_t *targ, const gentity_t *attacker )
{
	if ( !targ || !attacker || targ == attacker
		|| !G_CoopIsPlayer( targ ) || !G_CoopIsPlayer( attacker ) )
	{
		return -1;
	}
	if ( G_CoopIsLobby() || !g_coopFriendlyFire )
	{
		return 0;	// never in the lobby
	}
	if ( G_CoopVersusOn() && G_CoopSideOf( targ ) != G_CoopSideOf( attacker ) )
	{
		return 100;	// versus: enemy players are a fair target regardless of the FF setting
	}
	switch ( g_coopFriendlyFire->integer )
	{
	case 0:		return 0;
	case 2:		return 50;
	default:	return 100;
	}
}

qboolean G_CoopFriendlyFireOn( const gentity_t *targ, const gentity_t *attacker )
{
	return (qboolean)( G_CoopFriendlyFireScale( targ, attacker ) > 0 );
}

// team kill message, only when a player did it to another player
void G_CoopAnnounceKill( const gentity_t *victim, const gentity_t *attacker, qboolean downed )
{
	if ( !victim || !attacker || victim == attacker
		|| !G_CoopIsPlayer( victim ) || !G_CoopIsPlayer( attacker ) )
	{
		return;
	}
	if ( downed )
	{
		G_CoopPrintTr( -1, "print", "^1%s ^7a mis a terre ^1%s\n", "^1%s ^7downed ^1%s\n", attacker->client->pers.netname, victim->client->pers.netname );
	}
	else
	{
		G_CoopPrintTr( -1, "print", "^1%s ^7a tue ^1%s\n", "^1%s ^7killed ^1%s\n", attacker->client->pers.netname, victim->client->pers.netname );
	}
	gi.Printf( "coop: %s - %s %s %s\n",
		( G_CoopVersusOn() && G_CoopSideOf( victim ) != G_CoopSideOf( attacker ) ) ? "versus kill" : "team kill",
		attacker->client->pers.netname, downed ? "downed" : "killed", victim->client->pers.netname );
}

/*
==============================================================================
Versus mode (g_coopVersus)

L4D2-flavoured: "coop_team enemy" respawns you on TEAM_ENEMY - NPCs treat you
as one of theirs (playerTeam drives their targeting), Jedi shots on you always
land (G_CoopFriendlyFireScale cross-team), and the all-down flow only watches
the Jedi side. Your side rides in CS_PLAYERS "t" so cgame paints you red and
hides your tag until they can actually see you.
==============================================================================
*/

qboolean G_CoopVersusOn( void )
{
	return (qboolean)( g_coopVersus && g_coopVersus->integer );
}

// which side a player fights on in versus mode (TEAM_PLAYER when off)
team_t G_CoopSideOf( const gentity_t *ent )
{
	if ( ent && ent->client && G_CoopVersusOn()
		&& ent->client->sess.sessionTeam == TEAM_ENEMY )
	{
		return TEAM_ENEMY;
	}
	return TEAM_PLAYER;
}

qboolean G_CoopIsVersusEnemy( const gentity_t *ent )
{
	return (qboolean)( G_CoopIsPlayer( ent ) && G_CoopSideOf( ent ) == TEAM_ENEMY );
}

// coop_team jedi|enemy - pick a side; takes effect on the next spawn, or right
// away if you are alive (a team swap is a fresh spawn, like dying)
void G_CoopVersusTeamCommand( gentity_t *ent )
{
	const char	*arg = gi.argv( 1 );

	if ( !g_coopVersus || !g_coopVersus->integer )
	{
		gi.SendServerCommand( ent->s.number, "print \"versus mode is off (g_coopVersus 0)\n\"" );
		return;
	}
	if ( ent->s.number == 0 )
	{	// the host is the campaign anchor - zero jedi means an instant mission-fail loop
		gi.SendServerCommand( 0, "print \"the host stays on the jedi side\n\"" );
		return;
	}
	if ( !arg[0] )
	{
		gi.SendServerCommand( ent->s.number, "print \"you are on side: %s  (coop_team jedi|enemy)\n\"",
			ent->client->sess.sessionTeam == TEAM_ENEMY ? "^1enemy" : "^2jedi" );
		return;
	}
	team_t	want = TEAM_FREE;	// stock side: jedi
	if ( !Q_stricmp( arg, "enemy" ) || !Q_stricmp( arg, "enemies" ) || !Q_stricmp( arg, "e" ) )
	{
		want = TEAM_ENEMY;
	}
	else if ( !Q_stricmp( arg, "toggle" ) )
	{	// the F6 button does not know which side we are on: flip
		want = ( ent->client->sess.sessionTeam == TEAM_ENEMY ) ? TEAM_FREE : TEAM_ENEMY;
	}
	else if ( Q_stricmp( arg, "jedi" ) && Q_stricmp( arg, "ally" ) && Q_stricmp( arg, "good" ) )
	{
		gi.SendServerCommand( ent->s.number, "print \"usage: coop_team jedi|enemy\n\"" );
		return;
	}
	const qboolean	wasEnemy = (qboolean)( ent->client->sess.sessionTeam == TEAM_ENEMY );
	const qboolean	isEnemy = (qboolean)( want == TEAM_ENEMY );
	if ( wasEnemy == isEnemy )
	{
		return;	// already on that side
	}
	ent->client->sess.sessionTeam = want;
	ClientUserinfoChanged( ent->s.number );	// CS_PLAYERS "t" follows the new side
	if ( isEnemy )
	{
		G_CoopPrintTr( -1, "print", "^5%s ^7rejoint ^1l'ennemi^7!\n", "^5%s ^7joined the ^1enemies^7!\n", ent->client->pers.netname );
	}
	else
	{
		G_CoopPrintTr( -1, "print", "^5%s ^7rejoint les ^2jedi^7.\n", "^5%s ^7joined the ^2jedi^7.\n", ent->client->pers.netname );
	}
	if ( !G_CoopIsLobby() && ent->health > 0 && !G_CoopIsDowned( ent ) )
	{	// downed players swap on their next natural spawn - no un-downing yourself
		ClientSpawn( ent, eNO );
		if ( isEnemy )
		{
			G_CoopVersusPlace( ent );	// go straight to the front instead of the map start
		}
	}
}

// the level goes away (or comes back): nobody is down
void G_CoopResetZoomStance( void );

void G_CoopResetDowned( void )
{
	G_CoopResetZoomStance();
	memset( coopDown, 0, sizeof( coopDown ) );
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		coopDown[i].reviverNum = coopDown[i].revivingNum = ENTITYNUM_NONE;
	}
	coopAllDownTime = 0;
	coopAllDownLastSec = -1;
	coopBleedingOutNum = -1;
	coopPendingAutosave = qfalse;
	coopReloadTime = 0;
	coopReloadTries = 0;
}

// downed players exist only in a real co-op game
qboolean G_CoopDownedActive( void )
{
	return (qboolean)( g_coopDowned && g_coopDowned->integer && !G_CoopIsLobby() && G_CoopNumPlayers() >= 2 );
}

qboolean G_CoopIsDowned( const gentity_t *ent )
{
	return (qboolean)( G_CoopIsPlayer( ent ) && coopDown[ent->s.number].downTime != 0 );
}

// connected, alive and not down
qboolean G_CoopIsUp( const gentity_t *ent )
{
	return (qboolean)( G_CoopIsPlayer( ent ) && ent->client->pers.connected == CON_CONNECTED && ent->health > 0 && !G_CoopIsDowned( ent ) );
}

qboolean G_CoopAnyPlayerUp( void )
{
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if ( G_CoopIsVersusEnemy( &g_entities[i] ) )
		{
			continue;	// versus: the mission only fails while no Jedi is standing
		}
		if ( G_CoopIsUp( &g_entities[i] ) )
		{
			return qtrue;
		}
	}
	return qfalse;
}

static void G_CoopSetReviveStats( gentity_t *ent, int progress, int other )
{
	if ( ent && ent->client )
	{
		ent->client->ps.stats[STAT_COOP_REVIVE] = progress;
		ent->client->ps.stats[STAT_COOP_REVIVER] = other;
	}
}

// the reviver stops (key released, moved, hit, target gone)
void G_CoopReviveCancel( gentity_t *reviver )
{
	if ( !G_CoopIsPlayer( reviver ) )
	{
		return;
	}
	coopDown_t *me = &coopDown[reviver->s.number];
	if ( me->revivingNum >= 0 && me->revivingNum < MAX_CLIENTS )
	{
		coopDown_t *target = &coopDown[me->revivingNum];
		if ( target->reviverNum == reviver->s.number )
		{
			target->reviverNum = ENTITYNUM_NONE;
			target->reviveStartTime = 0;
			G_CoopSetReviveStats( &g_entities[me->revivingNum], 0, ENTITYNUM_NONE );
		}
		if ( reviver->client->ps.legsAnim == BOTH_FORCEHEAL_START )
		{
			reviver->client->ps.legsAnimTimer = reviver->client->ps.torsoAnimTimer = 0;
			NPC_SetAnim( reviver, SETANIM_BOTH, BOTH_FORCEHEAL_STOP, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD );
		}
	}
	me->revivingNum = ENTITYNUM_NONE;
	G_CoopSetReviveStats( reviver, 0, ENTITYNUM_NONE );
}

// back on its feet (revived), or dead for real, or respawned
void G_CoopClearDowned( gentity_t *ent )
{
	if ( !G_CoopIsPlayer( ent ) )
	{
		return;
	}
	coopDown_t *d = &coopDown[ent->s.number];
	if ( d->reviverNum >= 0 && d->reviverNum < MAX_CLIENTS )
	{
		G_CoopReviveCancel( &g_entities[d->reviverNum] );
	}
	if ( d->revivingNum != ENTITYNUM_NONE )
	{
		G_CoopReviveCancel( ent );
	}
	d->downTime = d->bleedOutTime = d->reviveStartTime = d->giveUpTime = 0;
	d->reviverNum = d->revivingNum = ENTITYNUM_NONE;
	ent->client->ps.powerups[PW_COOP_DOWNED] = 0;
	ent->client->ps.stats[STAT_COOP_DOWN] = 0;
	G_CoopSetReviveStats( ent, 0, ENTITYNUM_NONE );
}

/*
================
G_CoopOnSolidGround

Is this player standing on something walkable? ps.groundEntityNum cannot answer
that for a joiner - on the host it stays ENTITYNUM_NONE the whole time, even
while he stands still - so we look for ourselves, with a short trace under his
feet.
================
*/
static qboolean G_CoopOnSolidGround( const gentity_t *ent )
{
	trace_t	tr;
	vec3_t	end;

	if ( !ent || !ent->client )
	{
		return qfalse;
	}
	VectorCopy( ent->currentOrigin, end );
	end[2] -= 24.0f;
	gi.trace( &tr, (float *)ent->currentOrigin, (float *)ent->mins, (float *)ent->maxs, end,
		ent->s.number, ent->clipmask, (EG2_Collision)0, 0 );
	if ( tr.startsolid || tr.allsolid )
	{
		return qfalse;
	}
	return (qboolean)( tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f );
}

/*
================
G_CoopRememberSafeGround

Once per server frame: where each player last stood, on the ground, alive and
on his feet. G_CoopTryDown drops a falling player back on that spot instead of
letting a pit kill him while his mates are still playing (JD, 23/09).
================
*/
void G_CoopRememberSafeGround( void )
{
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = G_CoopPlayerSlot( i );

		if ( !ent || !ent->client || ent->health <= 0 || G_CoopIsDowned( ent ) )
		{
			continue;
		}
		if ( !G_CoopOnSolidGround( ent ) )
		{
			continue;	// in the air: not a spot to come back to
		}
		if ( VectorLengthSquared( ent->client->ps.velocity ) > 40.0f * 40.0f )
		{
			continue;	// moving fast (a jump, a slide): wait for him to settle
		}
		if ( ent->watertype & ( CONTENTS_LAVA | CONTENTS_SLIME ) )
		{
			continue;	// standing in the bad stuff: not a spot to come back to
		}
		if ( ent->client->ps.eFlags & EF_LOCKED_TO_WEAPON )
		{
			continue;	// bolted to an emplaced gun: not a spot to stand on
		}
		VectorCopy( ent->currentOrigin, coopDown[i].safeOrigin );
		coopDown[i].safeYaw = ent->client->ps.viewangles[YAW];
		coopDown[i].safeTime = level.time;
	}
}

/*
================
G_CoopPitRescue

Put a falling player back on the last ground he stood on, stopped and facing
the way he was. The spot is his own, a few seconds old at most, so it is
reachable - a teammate can walk to it and pick him up.
================
*/
static void G_CoopPitRescue( gentity_t *ent, const vec3_t safeOrigin, float safeYaw )
{
	vec3_t	org, angles;

	VectorCopy( safeOrigin, org );
	G_CoopPlaceDropToFloor( org );
	G_SetOrigin( ent, org );
	VectorCopy( org, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	VectorClear( ent->s.pos.trDelta );
	ent->client->ps.pm_time = 0;
	ent->client->ps.pm_flags &= ~( PMF_TIME_KNOCKBACK | PMF_TIME_NOFRICTION | PMF_SLOW_MO_FALL );
	VectorSet( angles, 0, safeYaw, 0 );
	SetClientViewAngle( ent, angles );
	ent->client->ps.groundEntityNum = ENTITYNUM_WORLD;
	// Sans ce bit, le client INTERPOLE le deplacement au lieu de sauter : la vue
	// glissait doucement du fond de la fosse jusqu'au point de secours, comme un
	// travelling (signale par JD sur t1_rail). G_CoopPlaceBeside le posait deja,
	// le sauvetage de fosse l'avait oublie.
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	gi.linkentity( ent );
	// no fade to undo: the pit trigger only fades when the fall actually killed
	// (g_trigger.cpp, "if ( G_CoopIsPlayer( other ) && other->health <= 0 )"),
	// and we are turning that death into a downed state instead
	G_CoopNote( va( "pit rescue: %s ramene a %.0f %.0f %.0f", ent->client->pers.netname, org[0], org[1], org[2] ) );
}

/*
================
G_CoopTryDown

Called by G_Damage just before a player would die. Returns qtrue when the
death was turned into a downed state instead.
================
*/
qboolean G_CoopTryDown( gentity_t *targ, gentity_t *attacker, int mod, int dflags )
{
	if ( !G_CoopIsPlayer( targ ) || !G_CoopDownedActive() || G_CoopIsDowned( targ ) || targ->s.number == coopBleedingOutNum
		|| G_CoopIsVersusEnemy( targ ) )
	{	// versus hunters never go down - they die and respawn, L4D2 style
		return qfalse;
	}
	// A pit is the one "real death" worth arguing with: the body lands out of
	// reach, so it used to be a plain death - and a plain death in a level
	// somebody else is still playing means a reload for everybody. Carry him
	// back to the last ground he stood on and let him go down there instead.
	const qboolean pit = (qboolean)( ( mod == MOD_FALLING && !G_CoopOnSolidGround( targ ) )
		|| mod == MOD_TRIGGER_HURT
		|| ( attacker && attacker->classname && !Q_stricmp( attacker->classname, "trigger_hurt" ) ) );

	if ( mod == MOD_SUICIDE || mod == MOD_SNIPER || mod == MOD_CRUSH
		|| mod == MOD_LAVA || mod == MOD_SLIME || mod == MOD_WATER		// no lying down in liquids
		|| ( targ->client->ps.eFlags & EF_LOCKED_TO_WEAPON )				// dead on an emplaced gun: RunEmplacedWeapon ejects the body (D7)
		|| ( targ->client->ps.eFlags & ( EF_FORCE_GRIPPED|EF_FORCE_DRAINED ) )	// held in the air by a gripper (D12)
		|| targ->s.m_iVehicleNum != 0
		|| ( targ->client->ps.eFlags & ( EF_HELD_BY_RANCOR|EF_HELD_BY_WAMPA|EF_HELD_BY_SAND_CREATURE ) )
		|| in_camera
		|| ( !attacker && ( dflags & DAMAGE_NO_PROTECTION ) && !pit ) )	// target_kill
	{	// no way to lie on the ground there: a real death
		return qfalse;
	}
	if ( pit )
	{
		coopDown_t *safe = &coopDown[targ->s.number];

		if ( !g_coopPitRescue->integer || !safe->safeTime || !G_CoopAnyPlayerUp() )
		{	// nobody left to come and get him, or the option is off: a real death
			return qfalse;
		}
		G_CoopPitRescue( targ, safe->safeOrigin, safe->safeYaw );
	}

	playerState_t	*ps = &targ->client->ps;
	coopDown_t		*d = &coopDown[targ->s.number];
	const int		bleed = ( g_coopBleedOut->integer > 0 ? g_coopBleedOut->integer : 60 ) * 1000;

	targ->health = 1;
	ps->stats[STAT_HEALTH] = 1;
	ps->stats[STAT_ARMOR] = 0;
	d->downTime = level.time;
	d->bleedOutTime = level.time + bleed;
	d->giveUpTime = 0;
	d->reviverNum = ENTITYNUM_NONE;
	d->reviveStartTime = 0;
	if ( d->revivingNum != ENTITYNUM_NONE )
	{
		G_CoopReviveCancel( targ );
	}
	ps->powerups[PW_COOP_DOWNED] = Q3_INFINITE;
	ps->stats[STAT_COOP_DOWN] = bleed;

	// saber off, powers off, no attack
	if ( ps->SaberActive() )
	{
		ps->SaberDeactivate();
		G_SoundIndexOnEnt( targ, CHAN_AUTO, ps->saber[0].soundOff );
	}
	for ( int fp = 0; fp < NUM_FORCE_POWERS; fp++ )
	{
		if ( ps->forcePowersActive & ( 1 << fp ) )
		{
			WP_ForcePowerStop( targ, (forcePowers_t)fp );
		}
	}
	ps->saberLockTime = 0;
	ps->weaponTime = 500;
	G_CoopSetZoomMode( targ, 0 );	// per player

	// fall like a knockdown, from the side the blow came from
	int anim = BOTH_KNOCKDOWN1;
	if ( attacker && attacker != targ )
	{
		vec3_t fwd, dir, angles = { 0, ps->viewangles[YAW], 0 };
		AngleVectors( angles, fwd, NULL, NULL );
		VectorSubtract( targ->currentOrigin, attacker->currentOrigin, dir );
		VectorNormalize( dir );
		if ( DotProduct( fwd, dir ) > 0.2f )
		{
			anim = BOTH_KNOCKDOWN3;	// hit from behind: falls forward
		}
	}
	NPC_SetAnim( targ, SETANIM_BOTH, anim, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD );
	ps->legsAnimTimer = ps->torsoAnimTimer = 3000;
	NPC_SetPainEvent( targ );

	// the enemies turn to someone still standing
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->enemy == targ )
		{
			G_ClearEnemy( e );
		}
	}

	if ( G_CoopIsPlayer( attacker ) && attacker != targ )
	{	// coop: a teammate did this (g_coopFriendlyFire), say who
		G_CoopAnnounceKill( targ, attacker, qtrue );
	}
	else
	{
		G_CoopPrintTr( -1, "print", "^1%s ^7est a terre !\n", "^1%s ^7is down!\n", targ->client->pers.netname );
	}
	gi.Printf( "coop: %s downed (mod %i), bleeds out in %i s\n", targ->client->pers.netname, mod, bleed / 1000 );
	coopStatDowns[targ->s.number]++;	// coop: scoreboard
	G_CoopBroadcastStats();
	return qtrue;
}

/*
================
G_CoopRevive

The revive completed: target stands up with part of its health and a short
grace period, reviver leaves the kneeling pose.
================
*/
static void G_CoopRevive( gentity_t *target, gentity_t *reviver )
{
	playerState_t	*ps = &target->client->ps;
	const int		pct = Com_Clampi( 1, 100, g_coopReviveHealth->integer );
	int				health = ps->stats[STAT_MAX_HEALTH] * pct / 100;

	G_CoopClearDowned( target );
	G_CoopReleaseHeld( target );	// coop: releve, donc plus tenu par quoi que ce soit
	if ( health < 1 )
	{
		health = 1;
	}
	target->health = ps->stats[STAT_HEALTH] = health;
	ps->powerups[PW_INVINCIBLE] = level.time + 2000;
	ps->legsAnimTimer = ps->torsoAnimTimer = 0;
	NPC_SetAnim( target, SETANIM_BOTH, ( ps->forcePowerLevel[FP_LEVITATION] > 0 ) ? Q_irand( BOTH_FORCE_GETUP_F1, BOTH_FORCE_GETUP_F2 ) : BOTH_GETUP1, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD );
	ps->weaponTime = 500;
	G_CoopReEquip( target );	// coop: back on his feet, and armed (JD, 23/09)
	G_Sound( target, G_SoundIndex( "sound/weapons/force/heal.mp3" ) );
	if ( reviver && reviver->client )
	{
		reviver->client->ps.legsAnimTimer = reviver->client->ps.torsoAnimTimer = 0;
		NPC_SetAnim( reviver, SETANIM_BOTH, BOTH_FORCEHEAL_STOP, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD );
		G_CoopPrintTr( -1, "print", "^2%s ^7a releve ^2%s\n", "^2%s ^7revived ^2%s\n", reviver->client->pers.netname, target->client->pers.netname );
		gi.Printf( "coop: %s revived by %s (%i hp)\n", target->client->pers.netname, reviver->client->pers.netname, health );
		coopStatRevives[reviver->s.number]++;	// coop: scoreboard
		G_CoopBroadcastStats();
	}
}

/*
==============================================================================
Waypoint ping ("coop_ping" client command, MOUSE3 by default)

A tap drops a marker where the player is aiming, shown to everybody for a
few seconds ("ping" svcmd -> CG_CoopPing_f / CG_CoopDrawPings). One live
ping per player: a new one replaces the old. Throttled per slot so a bind
spam cannot flood the command stream.
==============================================================================
*/
#define COOP_PING_THROTTLE	1200	// ms between two pings of the same player

static int	coopPingTime[MAX_CLIENTS];

void G_CoopPingCommand( gentity_t *ent )
{
	const int	slot = ent->s.number;

	if ( !G_CoopIsPlayer( ent ) || level.time - coopPingTime[slot] < COOP_PING_THROTTLE )
	{
		return;
	}

	// where the aim lands; a miss pings the horizon, still a valid "over there"
	playerState_t	*ps = &ent->client->ps;
	vec3_t			eyes, dir, end;
	trace_t			tr;

	VectorCopy( ps->origin, eyes );
	eyes[2] += ps->viewheight;
	AngleVectors( ps->viewangles, dir, NULL, NULL );
	VectorMA( eyes, 8192, dir, end );
	gi.trace( &tr, eyes, NULL, NULL, end, slot, MASK_SOLID, (EG2_Collision)0, 0 );
	VectorMA( tr.endpos, -8.0f, dir, tr.endpos );	// float off the surface a touch

	coopPingTime[slot] = level.time;

	char	name[36];
	Q_strncpyz( name, ent->client->pers.netname, sizeof( name ) );
	Q_strstrip( name, "\n\r\"", NULL );	// the svcmd embeds it between quotes

	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if ( G_CoopPlayerSlot( i ) )
		{
			gi.SendServerCommand( i, "ping %i \"%s\" %i %i %i", slot, name,
				(int)tr.endpos[0], (int)tr.endpos[1], (int)tr.endpos[2] );
		}
	}
}

/*
==============================================================================
Co-op scoreboard ("cstat" svcmd, drawn while +coop_score is held)

Kills ride along on PERS_ENEMIES_KILLED, which G_Damage already bumps. Downs,
revives and deaths live in the counters above and reset each map. Every
change re-sends the whole table: four ints per player, it is not worth
packing tighter. A joiner gets the same rows from G_CoopClientBegin.
==============================================================================
*/
static void G_CoopSendStats( int toSlot )
{
	for ( int s = 0; s < MAX_CLIENTS; s++ )
	{
		gentity_t	*p = G_CoopPlayerSlot( s );
		if ( !p )
		{
			continue;
		}
		char	name[36];
		Q_strncpyz( name, p->client->pers.netname, sizeof( name ) );
		Q_strstrip( name, "\n\r\"", NULL );	// the svcmd embeds it between quotes
		gi.SendServerCommand( toSlot, "cstat %i \"%s\" %i %i %i %i %i", s, name,
			p->client->ps.persistant[PERS_ENEMIES_KILLED],
			coopStatDowns[s], coopStatRevives[s], coopStatDeaths[s],
			p->client->ps.ping );	// coop: the server smooths a real RTT into ps.ping
	}
}

void G_CoopPublishScores( void )
{
	// the engine can't touch game memory at map end (dll is already unloaded
	// when the run post fires), so scores ride a cvar: "cn,score;cn,score"
	char	buf[256];
	int		off = 0;
	buf[0] = 0;
	for ( int s = 0; s < MAX_CLIENTS; s++ )
	{
		gentity_t	*p = G_CoopPlayerSlot( s );
		if ( p )
		{
			off += snprintf( buf + off, sizeof( buf ) - off, "%s%i,%i",
				off ? ";" : "", s, p->client->ps.persistant[PERS_SCORE] );
		}
	}
	gi.cvar_set( "sv_coopScores", buf );
}

void G_CoopBroadcastStats( void )
{
	// the site reads the same table through sv_coopTeam at map end
	char	buf[512];
	int		off = 0;
	buf[0] = 0;
	for ( int s = 0; s < MAX_CLIENTS; s++ )
	{
		if ( G_CoopPlayerSlot( s ) )
		{
			off += snprintf( buf + off, sizeof( buf ) - off, "%s%i,%i,%i,%i",
				off ? ";" : "", s, coopStatDowns[s], coopStatRevives[s], coopStatDeaths[s] );
		}
	}
	gi.cvar_set( "sv_coopTeam", buf );
	G_CoopPublishScores();

	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if ( G_CoopPlayerSlot( i ) )
		{
			G_CoopSendStats( i );
		}
	}
}

/*
================
G_CoopPrintScorecard

End-of-map chat recap, like a modern co-op post-match screen: MVP (most
kills), top medic (most revives) and the biggest casualty — each only when
it beats zero.
================
*/
void G_CoopPrintScorecard( void )
{
	int	bestK = 0, bestR = 0, bestD = 0;
	int	iK = -1, iR = -1, iD = -1;

	for ( int s = 0; s < MAX_CLIENTS; s++ )
	{
		gentity_t	*p = G_CoopPlayerSlot( s );
		if ( !p || !p->client )
		{
			continue;
		}
		const int kills = p->client->ps.persistant[PERS_ENEMIES_KILLED];
		if ( kills > bestK ) { bestK = kills; iK = s; }
		if ( coopStatRevives[s] > bestR ) { bestR = coopStatRevives[s]; iR = s; }
		if ( coopStatDeaths[s] > bestD ) { bestD = coopStatDeaths[s]; iD = s; }
	}
	G_CoopPrintTr( -1, "print", "^5--- debriefing ---\n", "^5--- mission debrief ---\n" );
	if ( iK >= 0 )
	{
		gentity_t *p = G_CoopPlayerSlot( iK );
		G_CoopPrintTr( -1, "print", "^3* MVP : ^7%s (%i frags)\n", "^3* MVP: ^7%s (%i kills)\n", p->client->pers.netname, bestK );
	}
	if ( iR >= 0 )
	{
		gentity_t *p = G_CoopPlayerSlot( iR );
		G_CoopPrintTr( -1, "print", "^2+ Sauveteur : ^7%s (%i relevages)\n", "^2+ Medic: ^7%s (%i revives)\n", p->client->pers.netname, bestR );
	}
	if ( iD >= 0 )
	{
		gentity_t *p = G_CoopPlayerSlot( iD );
		G_CoopPrintTr( -1, "print", "^1x Plus de morts : ^7%s (%i)\n", "^1x Most deaths: ^7%s (%i)\n", p->client->pers.netname, bestD );
	}
}

/*
==============================================================================
Simon the Killer Ewok (Galactic Battlegrounds homage)

"simonsays" in the console drops a jawa_armed — the closest thing JA ships
to a murderous ewok — beside the caller, with Simon's 325 hp. Hostile, so
the lobby that summons him had better mean it. One Simon at a time, 30 s
between calls.
==============================================================================
*/
void G_CoopSimonCommand( gentity_t *ent )
{
	static int	coopSimonNext = 0;

	if ( !ent || !ent->client )
	{
		return;
	}
	if ( level.time < coopSimonNext )
	{
		G_CoopPrintTr( ent->s.number, "print", "Simon est deja de sortie.\n", "Simon is already out there.\n" );
		return;
	}
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->health > 0 && e->targetname && !Q_stricmp( e->targetname, "simon" ) )
		{
			G_CoopPrintTr( ent->s.number, "print", "Simon est deja de sortie.\n", "Simon is already out there.\n" );
			return;
		}
	}
	coopSimonNext = level.time + 30000;

	// same spot math as NPC_Spawn_f, but at the caller instead of player 0
	gentity_t *spawner = G_Spawn();
	if ( !spawner )
	{
		return;
	}
	spawner->e_ThinkFunc = thinkF_G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;

	vec3_t	forward, end;
	trace_t	tr;
	AngleVectors( ent->client->ps.viewangles, forward, NULL, NULL );
	VectorNormalize( forward );
	VectorMA( ent->currentOrigin, 64, forward, end );
	gi.trace( &tr, ent->currentOrigin, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	VectorCopy( tr.endpos, end );
	end[2] -= 24;
	gi.trace( &tr, tr.endpos, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	VectorCopy( tr.endpos, end );
	end[2] += 24;
	G_SetOrigin( spawner, end );
	VectorCopy( spawner->currentOrigin, spawner->s.origin );
	spawner->s.angles[1] = ent->client->ps.viewangles[1];
	gi.linkentity( spawner );

	spawner->NPC_type = G_NewString( "jawa_armed" );
	spawner->NPC_targetname = G_NewString( "simon" );
	spawner->count = 1;
	spawner->delay = 0;
	spawner->wait = 500;
	NPC_Spawn( spawner, spawner, spawner );

	// Simon himself: Battlegrounds numbers, hostile like the original
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->targetname && !Q_stricmp( e->targetname, "simon" ) )
		{
			extern void ChangeWeapon( gentity_t *ent, int newWeapon );
			e->health = e->max_health = e->client->ps.stats[STAT_HEALTH] = e->client->ps.stats[STAT_MAX_HEALTH] = 325;
			// jawa_armed carries WP_JAWA, whose model (jawa_gun.md3) never shipped
			// with JA - cgame CG_Errors the first frame it draws him. A bowcaster
			// is the closest primitive weapon that actually exists.
			ChangeWeapon( e, WP_BOWCASTER );
			e->s.weapon = WP_BOWCASTER;
		}
	}
	G_CoopPrintTr( -1, "print", "^1SIMON DIT : ^7%s a invoque Simon l'Ewok tueur !\n",
		"^1SIMON SAYS: ^7%s summoned Simon the Killer Ewok!\n", ent->client->pers.netname );
}

/*
==============================================================================
Companion bots (g_coopBots / coop_bot)

AI teammates, not real clients: ally NPCs on TEAM_PLAYER riding the stock
"follow your leader" brain (the vanilla Jan/Kyle/Chewie escort path - spawn
code already hands them BS_FOLLOW_LEADER and player 0, we just retarget the
leader to their real owner). g_coopBots tops the party up to that many bots
each map and replaces the fallen after a cooldown; "coop_bot [type]" summons
one for the caller and "coop_bot remove" dismisses it. They are outside
CS_PLAYERS, never eat revives, and their kills credit nobody.
==============================================================================
*/
#define COOP_BOT_RESPAWN	30000	// ms before a dead companion is replaced

extern void NPC_PrecacheByClassName( const char *type );	// NPC_spawn.cpp

// spawnable allies - TEAM_PLAYER types from the stock npc files (no
// jedi_random: it needs the SP_NPC_Jedi special case, no seeker/test either)
static const char *coopBotTypes[] = {
	"jedi", "jedif", "jedimaster", "jeditrainer", "kyle", "luke",
	"jan", "rosh_penin", "chewie", "lando", "bespincop", "rebel", "rebel2",
	"prisoner", "elder", "monmothma", "merchant",
};
#define COOP_BOT_TYPES	( sizeof( coopBotTypes ) / sizeof( coopBotTypes[0] ) )

// default picks for the auto-fill and a bare "coop_bot" - the saber allies
static const char *coopBotDefault[] = { "jedi", "jedif", "jedimaster", "jeditrainer", "kyle", "luke" };
#define COOP_BOT_DEFAULTS	( sizeof( coopBotDefault ) / sizeof( coopBotDefault[0] ) )

static int G_CoopBotCount( void )
{
	int	n = 0;
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->client && e->health > 0
			&& e->targetname && !Q_stricmp( e->targetname, "coopbot" ) )
		{
			n++;
		}
	}
	return n;
}

// the living companion currently following this player, if any
static gentity_t *G_CoopMyBot( const gentity_t *owner )
{
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->client && e->health > 0
			&& e->targetname && !Q_stricmp( e->targetname, "coopbot" )
			&& e->client->leader == owner )
		{
			return e;
		}
	}
	return NULL;
}

// round-robin pick of a living Jedi-side player to adopt a bot
static gentity_t *G_CoopBotLeader( void )
{
	static int	next = 0;
	int			i, n = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->client && e->client->pers.connected == CON_CONNECTED
			&& e->health > 0
			&& ( !G_CoopVersusOn() || G_CoopSideOf( e ) != TEAM_ENEMY ) )
		{
			n++;
		}
	}
	if ( !n )
	{
		return NULL;
	}
	next %= n;
	for ( i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->client && e->client->pers.connected == CON_CONNECTED
			&& e->health > 0
			&& ( !G_CoopVersusOn() || G_CoopSideOf( e ) != TEAM_ENEMY ) )
		{
			if ( next-- == 0 )
			{
				next = n;
				return e;
			}
		}
	}
	return NULL;
}

// drop one ally NPC beside owner and point its leader at him (the spawn
// code defaults every ally to player 0, so it gets stamped with a unique
// tag first and renamed once found)
static gentity_t *G_CoopSpawnBot( gentity_t *owner, const char *npcType )
{
	static int	seq = 0;
	char		tag[32];
	gentity_t	*spawner, *bot = NULL;
	vec3_t		forward, end;
	trace_t		tr;
	int			i;

	spawner = G_Spawn();
	if ( !spawner )
	{
		return NULL;
	}
	spawner->e_ThinkFunc = thinkF_G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;

	AngleVectors( owner->client->ps.viewangles, forward, NULL, NULL );
	VectorNormalize( forward );
	VectorMA( owner->currentOrigin, 96, forward, end );
	gi.trace( &tr, owner->currentOrigin, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	VectorCopy( tr.endpos, end );
	end[2] -= 24;
	gi.trace( &tr, tr.endpos, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	VectorCopy( tr.endpos, end );
	end[2] += 24;
	G_SetOrigin( spawner, end );
	VectorCopy( spawner->currentOrigin, spawner->s.origin );
	spawner->s.angles[1] = owner->client->ps.viewangles[1] + 180.0f;	// face the owner
	gi.linkentity( spawner );

	Com_sprintf( tag, sizeof( tag ), "coopbot_new%d", seq++ );
	spawner->NPC_type = G_NewString( npcType );
	spawner->NPC_targetname = G_NewString( tag );
	spawner->count = 1;
	spawner->delay = 0;
	spawner->wait = 500;
	NPC_PrecacheByClassName( spawner->NPC_type );
	NPC_Spawn( spawner, spawner, spawner );

	for ( i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->client && e->targetname && !Q_stricmp( e->targetname, tag ) )
		{
			bot = e;
			break;
		}
	}
	if ( !bot )
	{
		return NULL;
	}
	bot->targetname = (char *)"coopbot";	// count/dismiss tag
	bot->client->leader = owner;
	bot->NPC->behaviorState = bot->NPC->defaultBehavior = BS_FOLLOW_LEADER;
	return bot;
}

void G_CoopBotCommand( gentity_t *ent )
{
	extern cvar_t *g_coopBots;
	const char	*arg = gi.argv( 1 );
	gentity_t	*bot;

	if ( !G_CoopIsPlayer( ent ) || !g_coopBots || g_coopBots->integer <= 0 )
	{
		return;
	}
	if ( arg[0] && ( !Q_stricmp( arg, "remove" ) || !Q_stricmp( arg, "dismiss" ) ) )
	{
		bot = G_CoopMyBot( ent );
		if ( !bot )
		{
			G_CoopPrintTr( ent->s.number, "print", "Aucun compagnon ne te suit.\n",
				"No companion is following you.\n" );
			return;
		}
		bot->health = 0;
		GEntity_DieFunc( bot, bot, bot, 100, MOD_UNKNOWN );
		G_CoopPrintTr( -1, "print", "^7%s a congedie son compagnon.\n",
			"^7%s dismissed a companion.\n", ent->client->pers.netname );
		return;
	}
	if ( G_CoopVersusOn() && G_CoopSideOf( ent ) == TEAM_ENEMY )
	{
		G_CoopPrintTr( ent->s.number, "print", "Les mechants n'ont pas de compagnons.\n",
			"The hunters don't get companions.\n" );
		return;
	}
	if ( ent->health <= 0 || G_CoopIsDowned( ent ) )
	{
		return;
	}
	if ( G_CoopMyBot( ent ) )
	{
		G_CoopPrintTr( ent->s.number, "print", "Un compagnon te suit deja (coop_bot remove pour le congedier).\n",
			"A companion already follows you (coop_bot remove to dismiss it).\n" );
		return;
	}
	if ( G_CoopBotCount() >= g_coopBots->integer )
	{
		G_CoopPrintTr( ent->s.number, "print", "Le groupe a deja %d compagnon(s) (g_coopBots).\n",
			"The party already has %d companion(s) (g_coopBots).\n", g_coopBots->integer );
		return;
	}

	const char	*type = NULL;
	int			i;
	if ( arg[0] )
	{
		for ( i = 0; i < (int)COOP_BOT_TYPES; i++ )
		{
			if ( !Q_stricmp( arg, coopBotTypes[i] ) )
			{
				type = coopBotTypes[i];
				break;
			}
		}
		if ( !type )
		{
			G_CoopPrintTr( ent->s.number, "print", "Compagnon inconnu '%s' - jedi, jedif, jedimaster, jeditrainer, kyle, luke, jan, chewie, lando, rebel...\n",
				"Unknown companion '%s' - jedi, jedif, jedimaster, jeditrainer, kyle, luke, jan, chewie, lando, rebel...\n", arg );
			return;
		}
	}
	else
	{
		type = coopBotDefault[ rand() % COOP_BOT_DEFAULTS ];
	}

	bot = G_CoopSpawnBot( ent, type );
	if ( !bot )
	{
		return;
	}
	G_CoopPrintTr( -1, "print", "^5%s^7 rejoint le groupe aux cotes de %s.\n",
		"^5%s^7 joins the party at %s's side.\n", type, ent->client->pers.netname );
}

void G_CoopBotFrame( void )
{
	extern cvar_t *g_coopBots;
	int			i;

	if ( !g_coopBots || g_coopBots->integer <= 0 )
	{
		return;
	}
	if ( level.time < coopBotsCheck )
	{
		return;
	}
	coopBotsCheck = level.time + 1000;

	// a bot whose owner left or turned enemy adopts another Jedi-side player
	for ( i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( !e->inuse || !e->NPC || !e->client || e->health <= 0
			|| !e->targetname || Q_stricmp( e->targetname, "coopbot" ) )
		{
			continue;
		}
		gentity_t *leader = e->client->leader;
		if ( !leader || !leader->inuse || !leader->client
			|| leader->client->pers.connected != CON_CONNECTED
			|| ( G_CoopVersusOn() && G_CoopSideOf( leader ) == TEAM_ENEMY ) )
		{
			e->client->leader = G_CoopBotLeader();
		}
	}

	// first fill a few seconds in so someone has actually spawned, then keep
	// the headcount topped up on a cooldown
	if ( !coopBotsFilled )
	{
		gentity_t *leader = G_CoopBotLeader();
		if ( !leader )
		{
			return;	// nobody alive yet - try again next second
		}
		coopBotsFilled = qtrue;
		for ( i = 0; i < g_coopBots->integer; i++ )
		{
			leader = G_CoopBotLeader();
			if ( leader )
			{
				G_CoopSpawnBot( leader, coopBotDefault[ i % COOP_BOT_DEFAULTS ] );
			}
		}
		return;
	}
	if ( G_CoopBotCount() < g_coopBots->integer && level.time >= coopBotsRespawn )
	{
		gentity_t *leader = G_CoopBotLeader();
		if ( leader && G_CoopSpawnBot( leader, coopBotDefault[ rand() % COOP_BOT_DEFAULTS ] ) )
		{
			coopBotsRespawn = level.time + COOP_BOT_RESPAWN;
			G_CoopPrintTr( -1, "print", "Un compagnon rejoint le groupe.\n",
				"A companion joins the party.\n" );
		}
	}
}

/*
==============================================================================
Playable beasts (coop_become <type>)

Possession rides ps.viewEntity - the same channel the stock "control" cheat
and level-4 mind trick use, generalized past player 0 so joiners can drive
too. "coop_become sandworm" takes over the map's own sand creature when one
is out, else drops one in front of the caller; rancor and wampa always
spawn fresh. Attack bites everything in a frontal arc; jump is the stock
possession exit, "coop_become release" works too. A coop-spawned beast is
tagged "coopbeast" and freed when its driver lets go or leaves.
==============================================================================
*/
static const struct { const char *name; const char *npcType; } coopBeastTypes[] = {
	{ "sandworm",		"sand_creature" },
	{ "sand_creature",	"sand_creature" },
	{ "rancor",			"rancor" },
	{ "wampa",			"wampa" },
};
#define COOP_BEAST_TYPES	( sizeof( coopBeastTypes ) / sizeof( coopBeastTypes[0] ) )

extern void NPC_SetAnim( gentity_t *ent, int setAnimParts, int anim, int setAnimFlags, int iBlend );
extern void G_SetViewEntity( gentity_t *self, gentity_t *viewEntity );
extern qboolean G_ClearViewEntity( gentity_t *ent );

// the player currently driving this NPC through viewEntity, or NULL - the
// stock checks only ask player 0, joiners needed this scan
gentity_t *G_CoopBeastController( const gentity_t *beast )
{
	int	i;
	for ( i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->client && e->client->pers.connected == CON_CONNECTED
			&& e->client->ps.viewEntity == beast->s.number )
		{
			return e;
		}
	}
	return NULL;
}

// drop a beast NPC near the caller (same spot math as the bot spawn)
static gentity_t *G_CoopSpawnBeast( gentity_t *owner, const char *npcType )
{
	static int	seq = 0;
	char		tag[32];
	gentity_t	*spawner, *beast = NULL;
	vec3_t		forward, end;
	trace_t		tr;
	int			i;

	spawner = G_Spawn();
	if ( !spawner )
	{
		return NULL;
	}
	spawner->e_ThinkFunc = thinkF_G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;

	AngleVectors( owner->client->ps.viewangles, forward, NULL, NULL );
	VectorNormalize( forward );
	VectorMA( owner->currentOrigin, 160, forward, end );
	gi.trace( &tr, owner->currentOrigin, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	VectorCopy( tr.endpos, end );
	end[2] -= 48;
	gi.trace( &tr, tr.endpos, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	VectorCopy( tr.endpos, end );
	end[2] += 48;
	G_SetOrigin( spawner, end );
	VectorCopy( spawner->currentOrigin, spawner->s.origin );
	gi.linkentity( spawner );

	Com_sprintf( tag, sizeof( tag ), "coopbeast_new%d", seq++ );
	spawner->NPC_type = G_NewString( npcType );
	spawner->NPC_targetname = G_NewString( tag );
	spawner->count = 1;
	spawner->delay = 0;
	spawner->wait = 500;
	NPC_PrecacheByClassName( spawner->NPC_type );
	NPC_Spawn( spawner, spawner, spawner );

	for ( i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->client && e->targetname && !Q_stricmp( e->targetname, tag ) )
		{
			beast = e;
			break;
		}
	}
	if ( !beast )
	{
		return NULL;
	}
	beast->targetname = (char *)"coopbeast";	// freed when released
	beast->client->playerTeam = TEAM_ENEMY;	// hunts the players, allied with the map
	return beast;
}

// the caller's possessed beast, if they are driving one
static gentity_t *G_CoopMyBeast( const gentity_t *ent )
{
	if ( !ent->client || ent->client->ps.viewEntity <= 0
		|| ent->client->ps.viewEntity >= ENTITYNUM_WORLD )
	{
		return NULL;
	}
	gentity_t *beast = &g_entities[ent->client->ps.viewEntity];
	return ( beast->inuse && beast->NPC ) ? beast : NULL;
}

void G_CoopBeastCommand( gentity_t *ent )
{
	const char	*arg = gi.argv( 1 );
	gentity_t	*beast;
	int			i;

	if ( !G_CoopIsPlayer( ent ) )
	{
		return;
	}
	if ( !arg[0] || !Q_stricmp( arg, "release" ) || !Q_stricmp( arg, "off" ) )
	{
		if ( G_CoopMyBeast( ent ) || ent->client->ps.viewEntity > 0 )
		{
			G_ClearViewEntity( ent );
			G_CoopPrintTr( ent->s.number, "print", "Tu reprends ton corps.\n",
				"Back in your own body.\n" );
		}
		else
		{
			G_CoopPrintTr( ent->s.number, "print", "coop_become sandworm|rancor|wampa - coop_become release pour lacher.\n",
				"coop_become sandworm|rancor|wampa - coop_become release to let go.\n" );
		}
		return;
	}
	if ( G_CoopMyBeast( ent ) )
	{
		G_CoopPrintTr( ent->s.number, "print", "Tu controles deja une bete (coop_become release).\n",
			"Already driving a beast (coop_become release).\n" );
		return;
	}
	if ( ent->health <= 0 || G_CoopIsDowned( ent ) )
	{
		return;
	}

	const char	*npcType = NULL;
	for ( i = 0; i < (int)COOP_BEAST_TYPES; i++ )
	{
		if ( !Q_stricmp( arg, coopBeastTypes[i].name ) )
		{
			npcType = coopBeastTypes[i].npcType;
			break;
		}
	}
	if ( !npcType )
	{
		G_CoopPrintTr( ent->s.number, "print", "Bete inconnue '%s' - sandworm, rancor, wampa.\n",
			"Unknown beast '%s' - sandworm, rancor, wampa.\n", arg );
		return;
	}

	// a sandworm call takes over the map's own free sand creature first -
	// it's already out there hunting, no need to spawn a second
	beast = NULL;
	if ( !Q_stricmp( npcType, "sand_creature" ) )
	{
		for ( i = 0; i < globals.num_entities && !beast; i++ )
		{
			gentity_t *e = &g_entities[i];
			if ( e->inuse && e->NPC && e->client && e->health > 0
				&& e->client->NPC_class == CLASS_SAND_CREATURE
				&& !G_CoopBeastController( e ) )
			{
				beast = e;
			}
		}
	}
	if ( !beast )
	{
		beast = G_CoopSpawnBeast( ent, npcType );
	}
	if ( !beast )
	{
		return;
	}
	beast->NPC->behaviorState = beast->NPC->defaultBehavior = BS_CINEMATIC;	// no brain - the driver is the brain
	G_SetViewEntity( ent, beast );
	G_CoopPrintTr( -1, "print", "^3%s^7 incarne un %s !\n",
		"^3%s^7 becomes a %s!\n", ent->client->pers.netname, arg );
}

void G_CoopBeastFrame( void )
{
	int	i;

	for ( i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED
			|| ent->client->ps.viewEntity <= 0 || ent->client->ps.viewEntity >= ENTITYNUM_WORLD )
		{
			continue;
		}
		gentity_t *beast = &g_entities[ent->client->ps.viewEntity];
		if ( !beast->inuse || !beast->NPC || beast->health <= 0 )
		{//the beast died or was freed - hand the body back
			G_ClearViewEntity( ent );
			continue;
		}

		if ( beast->client->NPC_class != CLASS_SAND_CREATURE )
		{
			continue;	// walkers get their anims from pmove like stock
		}

		// the worm has no pmove-driven anims - drive them off the forwarded
		// usercmd ourselves: bite on attack, slither while moving
		const usercmd_t	*cmd = &beast->client->usercmd;
		if ( ( cmd->buttons & BUTTON_ATTACK ) && TIMER_Done( beast, "coopbite" ) )
		{
			NPC_SetAnim( beast, SETANIM_LEGS, Q_irand( BOTH_ATTACK1, BOTH_ATTACK2 ),
				SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
			TIMER_Set( beast, "coopbite", beast->client->ps.legsAnimTimer );
			TIMER_Set( beast, "coopbitehit", beast->client->ps.legsAnimTimer / 2 );
		}
		if ( !TIMER_Done( beast, "coopbite" ) )
		{//mid-attack: the lunge lands once, partway through the anim
			if ( TIMER_Done( beast, "coopbitehit" ) )
			{
				vec3_t		fwd, maw;
				trace_t		tr;
				AngleVectors( beast->client->ps.viewangles, fwd, NULL, NULL );
				VectorMA( beast->currentOrigin, 140, fwd, maw );
				gi.trace( &tr, beast->currentOrigin, beast->mins, beast->maxs, maw, beast->s.number, MASK_SOLID, (EG2_Collision)0, 0 );
				G_RadiusDamage( tr.endpos, beast, 2000, 160, beast, MOD_UNKNOWN );
				TIMER_Set( beast, "coopbitehit", beast->client->ps.legsAnimTimer );
			}
		}
		else if ( VectorLengthSquared( beast->client->ps.velocity ) > 30.0f * 30.0f )
			{
				if ( beast->client->ps.legsAnim != BOTH_WALK2 )
				{
					NPC_SetAnim( beast, SETANIM_LEGS, BOTH_WALK2,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
				}
				if ( TIMER_Done( beast, "coopslither" ) )
				{
					vec3_t	up = { 0, 0, 1 };
					G_PlayEffect( G_EffectIndex( "env/sand_move" ), beast->currentOrigin, up );
					TIMER_Set( beast, "coopslither", 300 );
				}
			}
			else if ( beast->client->ps.legsAnim != BOTH_STAND1 )
			{
				NPC_SetAnim( beast, SETANIM_LEGS, BOTH_STAND1,
					SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
			}
	}

	// a coop-spawned beast nobody drives anymore goes away - the map's own
	// creatures keep their AI and stay
	for ( i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->targetname && !Q_stricmp( e->targetname, "coopbeast" )
			&& !G_CoopBeastController( e ) )
		{
			G_FreeEntity( e );
		}
	}
}

/*
==============================================================================
Catch-up teleport ("coop_cometome" client command)

Drops the caller beside its nearest living teammate, the same spot-finding
a co-op respawn uses (G_CoopPlaceBeside). No use while dead (the respawn
already does this), on the ground, in a vehicle or mid-cinematic, and a
short cooldown keeps it from being a get-out-of-a-fight key.
==============================================================================
*/
#define COOP_CATCHUP_THROTTLE	10000	// ms between two teleports of the same player

static int	coopCatchupTime[MAX_CLIENTS];

void G_CoopCatchupCommand( gentity_t *ent )
{
	if ( !G_CoopIsPlayer( ent )
		|| !g_coopCatchup || !g_coopCatchup->integer
		|| in_camera
		|| ent->health <= 0 || G_CoopIsDowned( ent )
		|| ent->s.m_iVehicleNum != 0
		|| level.time - coopCatchupTime[ent->s.number] < COOP_CATCHUP_THROTTLE )
	{
		return;
	}
	gentity_t	*mate = G_CoopLivingTeammate( ent );
	if ( !mate )
	{
		return;
	}
	coopCatchupTime[ent->s.number] = level.time;
	G_CoopPlaceBeside( ent, mate );
	G_CoopPrintTr( -1, "print", "^5%s ^7rejoint ^5%s\n", "^5%s ^7caught up with ^5%s\n",
		ent->client->pers.netname, mate->client->pers.netname );
}

/*
==============================================================================
Quick chat ("coop_qchat <n>" client command)

A few preset lines a key bind can drop into the chat feed. The table lives
server-side so one edit reaches every client; the wire format is the same
"chat" svcmd as Cmd_Say_f.
==============================================================================
*/
static const char *coopQChat[] = {
	"Follow me!",
	"Wait here.",
	"Help!",
	"On my way!",
	"Nice one!",
};

void G_CoopQChatCommand( gentity_t *ent )
{
	if ( !G_CoopIsPlayer( ent ) || gi.argc() < 2 )
	{
		return;
	}
	const int n = atoi( gi.argv( 1 ) ) - 1;
	if ( n < 0 || n >= (int)( sizeof( coopQChat ) / sizeof( coopQChat[0] ) ) )
	{
		return;
	}
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if ( G_CoopPlayerSlot( i ) )
		{
			gi.SendServerCommand( i, "chat \"%s%c%c: %c%c%s\"",
				ent->client->pers.netname,
				Q_COLOR_ESCAPE, COLOR_WHITE,
				Q_COLOR_ESCAPE, COLOR_GREEN,
				coopQChat[n] );
		}
	}
}

/*
==============================================================================
Pickup sharing (g_coopPickupShare, a percent)

A health/armor/ammo pickup also hands nearby living teammates that slice of
it. Range-limited on purpose: the player who walked into the room still has
to be near the loot for the party to benefit.
==============================================================================
*/
#define COOP_SHARE_RANGE	384.0f

void G_CoopSharePickup( const gentity_t *item, gentity_t *toucher )
{
	if ( !g_coopPickupShare || g_coopPickupShare->integer <= 0
		|| !item->item || !G_CoopIsPlayer( toucher ) )
	{
		return;
	}
	const int type = item->item->giType;
	if ( type != IT_AMMO && type != IT_HEALTH && type != IT_ARMOR )
	{
		return;
	}
	int quantity = item->item->quantity;
	if ( type == IT_HEALTH && item->count )
	{	// Pickup_Health: count overrides the item's quantity
		quantity = item->count;
	}
	quantity = quantity * Com_Clampi( 0, 100, g_coopPickupShare->integer ) / 100;
	if ( quantity <= 0 )
	{
		return;
	}
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t	*mate = G_CoopPlayerSlot( i );
		if ( !mate || mate == toucher || !G_CoopIsUp( mate )
			|| ( G_CoopVersusOn() && G_CoopSideOf( mate ) != G_CoopSideOf( toucher ) )
			|| DistanceSquared( mate->currentOrigin, toucher->currentOrigin ) > COOP_SHARE_RANGE * COOP_SHARE_RANGE )
		{
			continue;
		}
		switch ( type )
		{
		case IT_AMMO:
			Add_Ammo2( mate, item->item->giTag, quantity );
			break;
		case IT_HEALTH:
			mate->health += quantity;
			if ( mate->health > mate->client->ps.stats[STAT_MAX_HEALTH] )
			{
				mate->health = mate->client->ps.stats[STAT_MAX_HEALTH];
			}
			mate->client->ps.stats[STAT_HEALTH] = mate->health;
			break;
		case IT_ARMOR:
			mate->client->ps.stats[STAT_ARMOR] += quantity;
			if ( mate->client->ps.stats[STAT_ARMOR] > mate->client->ps.stats[STAT_MAX_HEALTH] )
			{
				mate->client->ps.stats[STAT_ARMOR] = mate->client->ps.stats[STAT_MAX_HEALTH];
			}
			mate->client->ps.powerups[PW_BATTLESUIT] = Q3_INFINITE;
			break;
		}
	}
}

/*
================
G_CoopDownedThink

Every ClientThink of a co-op player: a downed one keeps only its view and
its knockdown pose; a standing one holding the revive key beside a downed
teammate revives it.
================
*/
/*
================
G_CoopWatchWeapon

A co-op player's weapon lives on two machines: the cgame picks it and the host
decides. Anything that changes it host-side without the cgame asking (a weapon
taken away, a respawn, leaving an emplaced gun) leaves the two disagreeing, and
the joiner ends up holding nothing while his usercmd asks for something he no
longer owns - JD's "l'arme a disparu et je ne pouvais plus rien faire".

So: tell the client whenever the weapon changed behind its back, and re-arm a
living player who ends up with empty hands.
================
*/
static int coopLastWeapon[MAX_CLIENTS];

static void G_CoopWatchWeapon( gentity_t *ent, const usercmd_t *ucmd )
{
	const int	slot = ent->s.number;
	playerState_t *ps = &ent->client->ps;

	if ( slot >= MAX_CLIENTS || ent->health <= 0 || G_CoopIsDowned( ent ) || in_camera
		|| ( ps->eFlags & EF_LOCKED_TO_WEAPON ) || ent->s.m_iVehicleNum != 0 )
	{
		coopLastWeapon[slot] = ps->weapon;
		return;
	}
	if ( ps->weapon <= WP_NONE && G_CoopBestOwnedWeapon( ps ) > WP_NONE && ps->weaponTime <= 0 )
	{	// empty-handed with a full inventory: the watchdog
		G_CoopNote( va( "weapon watchdog: %s had none, re-arming", ent->client->pers.netname ) );
		G_CoopReEquip( ent );
	}
	else if ( slot != 0 && ps->weapon != coopLastWeapon[slot] && ucmd && ps->weapon != ucmd->weapon )
	{	// it moved for a reason this joiner did not ask for: tell its cgame,
		// or its next usercmd drags the host back to the old selection
		G_CoopChangeWeapon( ent, ps->weapon );
	}
	coopLastWeapon[slot] = ps->weapon;
}

void G_CoopDownedThink( gentity_t *ent, usercmd_t *ucmd )
{
	if ( ent && ent->client && G_CoopIsPlayer( ent ) )
	{
		G_CoopWatchWeapon( ent, ucmd );
	}
	if ( !G_CoopIsPlayer( ent ) || !ucmd )
	{
		return;
	}
	playerState_t	*ps = &ent->client->ps;
	coopDown_t		*me = &coopDown[ent->s.number];

	if ( coopRespawnTime[ent->s.number] )
	{	// dead and the co-op respawn is on its way: USE held picks the
		// checkpoint spawn (G_CoopRespawn), else he lands beside a teammate
		coopRespawnAtSpawn[ent->s.number] = ( ucmd->buttons & BUTTON_USE ) ? 1 : 0;
	}

	if ( me->downTime )
	{	// on the ground: no moves, no buttons, still lying down
		// coop: holding JUMP gives up - bleed out now and come back beside a
		// teammate after the respawn delay, watching him meanwhile (CG_CoopSpectate).
		// A review (29/09): "When dying as the host, I was stuck in 1 spot while my
		// friend completed the level". Only while somebody else is up to come back to.
		if ( g_developer && g_developer->integer > 1 )
		{
			gi.Printf( "coop: downed think %i upmove %i teammate %i reviver %i\n", ent->s.number, ucmd->upmove,
				G_CoopLivingTeammate( ent ) ? G_CoopLivingTeammate( ent )->s.number : -1, me->reviverNum );
		}
		if ( ucmd->upmove > 0 && G_CoopLivingTeammate( ent ) && me->reviverNum == ENTITYNUM_NONE )
		{
			if ( !me->giveUpTime )
			{
				me->giveUpTime = level.time;
			}
			else if ( level.time - me->giveUpTime >= COOP_GIVEUP_HOLD && me->bleedOutTime > level.time )
			{
				gi.Printf( "coop: %s gives up waiting for a revive\n", ent->client->pers.netname );
				me->bleedOutTime = level.time;	// G_CoopDownedFrame does the rest
			}
		}
		else
		{
			me->giveUpTime = 0;
		}
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		ucmd->buttons = 0;
		ucmd->generic_cmd = 0;
		if ( ent->health > 0 )
		{
			ent->health = 1;
		}
		ps->stats[STAT_HEALTH] = ent->health;
		ps->stats[STAT_ARMOR] = 0;
		ps->weaponTime = 500;
		if ( !PM_InKnockDownNoGetup( ps ) )
		{
			NPC_SetAnim( ent, SETANIM_BOTH, BOTH_KNOCKDOWN1, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD );
		}
		if ( ps->legsAnimTimer < 3000 )
		{
			ps->legsAnimTimer = 3000;
		}
		if ( ps->torsoAnimTimer < 3000 )
		{
			ps->torsoAnimTimer = 3000;
		}
		return;
	}

	if ( ent->health <= 0 || G_CoopIsVersusEnemy( ent ) )
	{	// versus hunters can't revive - keeping a Jedi breathing is the other side's job
		return;
	}

	// standing: reviving someone? (the use key works too, for a player whose revive key is taken)
	const qboolean	key = (qboolean)( ( ucmd->buttons & ( BUTTON_COOP_REVIVE|BUTTON_USE ) ) != 0 );
	const qboolean	moving = (qboolean)( ucmd->forwardmove || ucmd->rightmove || ucmd->upmove );
	const float		range = g_coopReviveRange->value > 0 ? g_coopReviveRange->value : 80.0f;

	if ( me->revivingNum != ENTITYNUM_NONE )
	{
		gentity_t	*target = &g_entities[me->revivingNum];
		coopDown_t	*t = &coopDown[me->revivingNum];
		if ( !key || moving || !G_CoopIsDowned( target ) || t->reviverNum != ent->s.number
			|| Distance( ent->currentOrigin, target->currentOrigin ) > range * 1.25f )
		{
			G_CoopReviveCancel( ent );
			return;
		}
		const int	total = ( g_coopReviveTime->value > 0 ? g_coopReviveTime->value : 3.0f ) * 1000;
		int			progress = ( level.time - t->reviveStartTime ) * 100 / total;
		if ( progress >= 100 )
		{
			me->revivingNum = ENTITYNUM_NONE;
			G_CoopSetReviveStats( ent, 0, ENTITYNUM_NONE );
			G_CoopRevive( target, ent );
			return;
		}
		// hold the kneeling pose, no moves, no shots, and no door/panel use meanwhile
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		ucmd->buttons &= ~( BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_USE_FORCE|BUTTON_FORCE_LIGHTNING|BUTTON_FORCE_DRAIN|BUTTON_FORCEGRIP|BUTTON_USE );
		ps->weaponTime = 200;
		if ( ps->legsAnim != BOTH_FORCEHEAL_START )
		{
			NPC_SetAnim( ent, SETANIM_BOTH, BOTH_FORCEHEAL_START, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD );
		}
		if ( ps->legsAnimTimer < 500 )
		{
			ps->legsAnimTimer = 500;
		}
		if ( ps->torsoAnimTimer < 500 )
		{
			ps->torsoAnimTimer = 500;
		}
		G_CoopSetReviveStats( ent, progress > 0 ? progress : 1, target->s.number );
		G_CoopSetReviveStats( target, progress > 0 ? progress : 1, ent->s.number );
		return;
	}

	if ( !key || moving )
	{
		return;
	}
	// the nearest downed teammate in reach that nobody else is reviving
	gentity_t	*best = NULL;
	float		bestDist = 0;
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *other = &g_entities[i];
		if ( other == ent || !G_CoopIsDowned( other ) || coopDown[i].reviverNum != ENTITYNUM_NONE )
		{
			continue;
		}
		const float dist = Distance( ent->currentOrigin, other->currentOrigin );
		if ( dist <= range && ( !best || dist < bestDist ) )
		{
			best = other;
			bestDist = dist;
		}
	}
	if ( best )
	{
		me->revivingNum = best->s.number;
		coopDown[best->s.number].reviverNum = ent->s.number;
		coopDown[best->s.number].reviveStartTime = level.time;
		NPC_SetAnim( ent, SETANIM_BOTH, BOTH_FORCEHEAL_START, SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD );
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		ucmd->buttons &= ~BUTTON_USE;
		G_CoopSetReviveStats( ent, 1, best->s.number );
		G_CoopSetReviveStats( best, 1, ent->s.number );
	}
}

/*
================
G_CoopDownedFrame

Once per server frame: bleed-out clocks (paused in cutscenes), the HUD
stats, and the "everyone is down" flow.
================
*/
void G_CoopDownedFrame( void )
{
	const int frame = level.time - level.previousTime;

	G_CoopRunDeferredAutosave();

	// scoreboard pings drift constantly: push the table every couple seconds
	static int	coopStatsNextTime = 0;
	if ( level.time >= coopStatsNextTime )
	{
		coopStatsNextTime = level.time + 2000;
		G_CoopBroadcastStats();
	}

	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t	*ent = &g_entities[i];
		coopDown_t	*d = &coopDown[i];
		if ( !d->downTime )
		{
			continue;
		}
		if ( !G_CoopIsPlayer( ent ) || ent->client->pers.connected != CON_CONNECTED || ent->health <= 0 )
		{	// gone, or died of something the immunity does not cover
			G_CoopClearDowned( ent );
			continue;
		}
		if ( in_camera )
		{	// the story is talking: nobody bleeds meanwhile
			d->bleedOutTime += frame;
		}
		ent->client->ps.stats[STAT_COOP_DOWN] = ( d->bleedOutTime > level.time ) ? d->bleedOutTime - level.time : 1;
		if ( d->reviverNum == ENTITYNUM_NONE )
		{
			G_CoopSetReviveStats( ent, 0, ENTITYNUM_NONE );
		}
		if ( level.time >= d->bleedOutTime && !in_camera )
		{	// bled out: a real death now (respawn beside someone up, or the all-down flow)
			gi.Printf( "coop: %s bled out\n", ent->client->pers.netname );
			G_CoopClearDowned( ent );
			coopBleedingOutNum = i;
			G_Damage( ent, &g_entities[ENTITYNUM_WORLD], &g_entities[ENTITYNUM_WORLD], NULL, ent->currentOrigin, 1000, DAMAGE_NO_PROTECTION|DAMAGE_NO_ARMOR|DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN );
			coopBleedingOutNum = -1;
		}
	}

	// everyone down (or dead): offer the last checkpoint, reload it after a while
	if ( coopAllDownTime < 0 )
	{	// the reload is on its way; a failed load (no checkpoint yet) leaves us here
		if ( coopReloadTime && level.time - coopReloadTime > 3000 )
		{
			coopReloadTime = level.time;
			coopReloadTries++;
			if ( coopReloadTries == 1 )
			{
				gi.Printf( "coop: the checkpoint did not load, loading the level start instead\n" );
				G_CoopPrintTr( -1, "print", "^3Pas de point de controle : retour au debut du niveau...\n", "^3No checkpoint: back to the start of the level...\n" );
				gi.SendConsoleCommand( "load current\n" );
			}
			else
			{
				gi.Printf( "coop: no save to reload, restarting the map\n" );
				gi.SendConsoleCommand( va( "map %s\n", level.mapname ) );
			}
		}
		return;
	}
	if ( !G_CoopDownedActive() || in_camera )
	{
		return;
	}
	if ( G_CoopAnyPlayerUp() )
	{
		if ( coopAllDownTime )
		{	// someone came back (a scripted respawn): drop the screens
			coopAllDownTime = 0;
			gi.SendServerCommand( -1, "coopmenu closeall" );
		}
		return;
	}
	if ( !coopAllDownTime )
	{
		coopAllDownTime = level.time;
		coopAllDownLastSec = -1;
		G_CoopPrintTr( -1, "print", "^1Tous les joueurs sont a terre.\n", "^1All players are down.\n" );
		gi.Printf( "coop: everyone is down\n" );
		for ( int i = 0; i < MAX_CLIENTS; i++ )
		{
			if ( G_CoopPlayerSlot( i ) )
			{
				gi.SendServerCommand( i, "coopmenu %s", i == 0 ? "coopAllDownHost" : "coopAllDownClient" );
			}
		}
	}
	const int auto_ = g_coopAllDownAuto->integer;
	const int left = auto_ > 0 ? auto_ - ( level.time - coopAllDownTime ) / 1000 : -1;
	if ( left != coopAllDownLastSec )
	{
		coopAllDownLastSec = left;
		gi.SendServerCommand( -1, "cad %i", left );
	}
	if ( auto_ > 0 && left <= 0 )
	{
		G_CoopReloadCheckpoint();
	}
}

/*
================
Deferred checkpoint

target_autosave fired while the host was downed or dead: the save would
record it with 1 hp (or none) and the all-down reload would loop on it.
Remember the checkpoint and write it as soon as the host is up again.
================
*/
qboolean G_CoopDeferAutosave( void )
{
	const gentity_t *host = &g_entities[0];
	if ( !G_CoopIsDowned( host ) && !G_CoopRespawnPending( host ) && !( host->inuse && host->client && host->health <= 0 ) )
	{
		return qfalse;
	}
	if ( !coopPendingAutosave )
	{
		coopPendingAutosave = qtrue;
		G_CoopPrintTr( -1, "print", "^3Point de controle en attente : l'hote est a terre\n", "^3Checkpoint on hold: the host is down\n" );
		gi.Printf( "coop: autosave deferred, the host is down\n" );
	}
	return qtrue;
}

static void G_CoopRunDeferredAutosave( void )
{
	if ( !coopPendingAutosave || in_camera )
	{
		return;
	}
	const gentity_t *host = &g_entities[0];
	if ( !G_CoopIsUp( host ) || G_CoopRespawnPending( host ) )
	{
		return;
	}
	coopPendingAutosave = qfalse;
	gi.Printf( "coop: writing the deferred autosave\n" );
	gi.SendConsoleCommand( "wait 2;save auto\n" );
}

// the host reloads the last checkpoint (all-down screen, or its button)
void G_CoopReloadCheckpoint( void )
{
	if ( coopAllDownTime < 0 )
	{
		return;	// already asked
	}
	coopAllDownTime = -1;
	coopReloadTime = level.time;
	G_CoopPrintTr( -1, "print", "^3Retour au dernier point de controle...\n", "^3Back to the last checkpoint...\n" );
	gi.Printf( "coop: reloading the last checkpoint\n" );
	gi.SendConsoleCommand( "load *respawn\n" );
}

/*
==============================================================================
Per-player character settings

Single player builds the player from the host's cvars (g_char_*, g_saber*).
A joiner sends the same cvars in its userinfo (they are CVAR_USERINFO in
G_InitCvars), and the spawn code reads them through here so every player
gets the look and sabers they chose. Slot 0 keeps reading the cvars.
==============================================================================
*/
const char *G_CoopPlayerVar( const gentity_t *ent, const char *key, const cvar_t *hostCvar )
{
	static char	buffers[8][MAX_QPATH];
	static int	next = 0;

	if ( ent && ent->s.number > 0 && ent->s.number < MAX_CLIENTS && ent->client )
	{
		char userinfo[MAX_INFO_STRING];
		gi.GetUserinfo( ent->s.number, userinfo, sizeof( userinfo ) );
		const char *v = Info_ValueForKey( userinfo, key );
		if ( v && v[0] )
		{
			char *buf = buffers[next++ & 7];
			Q_strncpyz( buf, v, MAX_QPATH );
			return buf;
		}
	}
	return hostCvar ? hostCvar->string : "";
}

// Rebuild a joiner's model/sabers when the character keys in its userinfo change
// after it spawned (the flags may only reach the client after connect).
/*
================
G_CoopCheckPaksGen

The engine bumps cl_coopPaksGen every time a pk3 enters the search path while
the game runs. On a joiner that is a pack downloaded from the host; on the HOST
it is a pack a joiner pushed up (its own character or hilt mod,
sv_coop_transfer.cpp). Every character that was built while those files were
missing is a stormtrooper with a default hilt - G_SetG2PlayerModel fell back
when the model failed to load, and G_CoopRecordModel wrote the fallback into
the spec the joiners read. So build them all again now that the files are
there, which republishes the spec and costs nothing when nothing changed.
The joiners do the same on their side (CG_CoopCheckPaksGen).
================
*/
extern void WP_SaberLoadParms( void );

void G_CoopCheckPaksGen( void )
{
	static int	seen = -1;
	const int	gen = g_coopPaksGen ? g_coopPaksGen->integer : 0;

	if ( seen == gen )
	{
		return;
	}
	if ( seen == -1 )
	{	// first look of this map: everything was built with what is loaded now
		seen = gen;
		return;
	}
	seen = gen;
	if ( !G_CoopIsLobby() )
	{	// mid-mission a pack can still arrive (a joiner connecting in game): parse the new
		// hilts, but do not rebuild everyone's character in the middle of a fight - the next
		// level (or the next character change) picks the models up
		WP_SaberLoadParms();
		gi.Printf( "coop: pk3 list changed (gen %i) in game, hilts reloaded\n", gen );
		return;
	}
	// the hilt definitions are read once at InitGame into SaberParms; a pack that
	// arrives now carries ext_data/sabers/*.sab nobody has parsed yet, so the
	// hilt a player picked from it would fall back to the default one
	WP_SaberLoadParms();
	int n = 0;
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = G_CoopPlayerSlot( i );
		if ( ent && ent->inuse && ent->client && ent->health > 0 && ent->client->pers.connected == CON_CONNECTED )
		{
			G_InitPlayerFromCvars( ent );
			n++;
		}
	}
	gi.Printf( "coop: pk3 list changed (gen %i), %i player(s) rebuilt by the game\n", gen, n );
}

void G_CoopCheckCharacterChange( gentity_t *ent )
{
	static char	lastKeys[MAX_CLIENTS][MAX_INFO_STRING];
	static const char *keys[] = { "g_char_model", "g_char_skin", "g_char_skin_head", "g_char_skin_torso", "g_char_skin_legs",
		"g_char_color_red", "g_char_color_green", "g_char_color_blue", "g_saber", "g_saber2", "g_saber_color", "g_saber2_color", "snd" };

	if ( !ent || ent->s.number <= 0 || ent->s.number >= MAX_CLIENTS || !ent->client )
	{
		return;
	}
	char userinfo[MAX_INFO_STRING], now[MAX_INFO_STRING] = "";
	gi.GetUserinfo( ent->s.number, userinfo, sizeof( userinfo ) );
	for ( size_t i = 0; i < ARRAY_LEN( keys ); i++ )
	{
		Q_strcat( now, sizeof( now ), Info_ValueForKey( userinfo, keys[i] ) );
		Q_strcat( now, sizeof( now ), "|" );
	}
	if ( !strcmp( now, lastKeys[ent->s.number] ) )
	{
		return;
	}
	const qboolean hadKeys = (qboolean)( lastKeys[ent->s.number][0] != '\0' );
	Q_strncpyz( lastKeys[ent->s.number], now, sizeof( lastKeys[0] ) );
	if ( hadKeys && ent->inuse && ent->health > 0 && ent->client->pers.connected == CON_CONNECTED )
	{
		G_InitPlayerFromCvars( ent );
	}
}

/*
==============================================================================
Mission objectives

ICARUS updates the host's client->sess.mission_objectives; joiners read them
back from a configstring (one character per objective: display bit + status)
into their own placeholder client so the datapad and the HUD flash work.
==============================================================================
*/
void G_CoopUpdateObjectives( void )
{
	static char	last[MAX_MISSION_OBJ + 1];
	char		now[MAX_MISSION_OBJ + 1];
	const gclient_t *host = g_entities[0].client;

	if ( !host || G_CoopNumPlayers() < 2 )
	{
		return;
	}
	for ( int i = 0; i < MAX_MISSION_OBJ; i++ )
	{
		const objectives_t *o = &host->sess.mission_objectives[i];
		now[i] = (char)( 'A' + ( o->display ? 8 : 0 ) + ( o->status & 7 ) );
	}
	now[MAX_MISSION_OBJ] = '\0';
	if ( strcmp( now, last ) )
	{
		Q_strncpyz( last, now, sizeof( last ) );
		gi.SetConfigstring( CS_COOP_OBJECTIVES, now );
	}
}

/*
==============================================================================
Sounds played straight into the host's sound system

"Bypass network for sounds on specific channels" (g_utils.cpp): dialogue,
NPC voices and a few others never become entity events, so a remote client
would stay silent (and its lips still, since facial animation follows the
voice volume). Forward them as a "snd" server command to every remote client.
==============================================================================
*/
extern qboolean CG_TryPlayCustomSound( vec3_t origin, int entityNum, soundChannel_t channel, const char *soundName, int customSoundSet );

/*
================
G_CoopChunks

CG_Chunks is a direct game -> cgame call: the debris of a func_breakable, a
misc_model_breakable or a shattered wall only ever appeared on the host.
Spawn the chunks locally as before and broadcast the same parameters as an
unreliable temp entity for the remote clients (cg_event.cpp EV_COOP_CHUNKS):
  origin = origin, origin2 = normal, angles = mins, angles2 = maxs,
  time = speed, eventParm = numChunks, weapon = material,
  modelindex = custom chunk model (CS_MODELS), time2 = scale x 100,
  modelindex2 = custom sound (CS_SOUNDS), otherEntityNum = owner
================
*/
extern void CG_Chunks( int owner, vec3_t origin, const vec3_t normal, const vec3_t mins, const vec3_t maxs,
						float speed, int numChunks, material_t chunkType, int customChunk, float baseScale, int customSound );
void G_CoopChunks( int owner, vec3_t origin, const vec3_t normal, const vec3_t mins, const vec3_t maxs,
						float speed, int numChunks, material_t chunkType, int customChunk, float baseScale, int customSound )
{
	CG_Chunks( owner, origin, normal, mins, maxs, speed, numChunks, chunkType, customChunk, baseScale, customSound );
	if ( G_CoopNumPlayers() < 2 || chunkType == MAT_NONE )
	{
		return;
	}
	gentity_t *te = G_TempEntity( origin, EV_COOP_CHUNKS );
	te->s.otherEntityNum = owner;
	VectorCopy( normal, te->s.origin2 );
	VectorCopy( mins, te->s.angles );
	VectorCopy( maxs, te->s.angles2 );
	te->s.time = (int)speed;
	te->s.eventParm = Q_min( numChunks, 255 );
	te->s.weapon = (int)chunkType;
	te->s.modelindex = customChunk;
	te->s.time2 = (int)( baseScale * 100.0f );
	te->s.modelindex2 = ( customSound > 0 && customSound < 256 ) ? customSound : 0;
}

/*
================
G_CoopMiscModelExplosion

CG_MiscModelExplosion is the same kind of game -> cgame shortcut as
CG_Chunks: the burst of a misc_model_breakable / func_breakable only ever
played on the host. Play it locally and send the parameters as a PVS-bound
temp entity for the remote clients (cg_event.cpp EV_COOP_EXPLOSION).
================
*/
extern void CG_MiscModelExplosion( vec3_t mins, vec3_t maxs, int size, material_t chunkType );
void G_CoopMiscModelExplosion( gentity_t *self, int size, material_t chunkType )
{
	CG_MiscModelExplosion( self->absmin, self->absmax, size, chunkType );	// host, as before
	if ( G_CoopNumPlayers() < 2 || chunkType == MAT_NONE )
	{
		return;
	}
	vec3_t mid;
	VectorAdd( self->absmin, self->absmax, mid );
	VectorScale( mid, 0.5f, mid );
	gentity_t *te = G_TempEntity( mid, EV_COOP_EXPLOSION );
	VectorCopy( self->absmin, te->s.angles );
	VectorCopy( self->absmax, te->s.angles2 );
	te->s.eventParm = size;			// 0..2
	te->s.weapon = (int)chunkType;	// material_t < 256
}

/*
================
G_CoopGlass

func_glass shatters through cgi_R_GetBModelVerts + CG_DoGlass straight into
the host's cgame (and its sound system). Describe the pane to the remote
clients; they own the same inline models and rebuild the shards themselves
(cg_event.cpp EV_COOP_GLASS). Broadcast: a window is a landmark and the
entity is freed right after.
================
*/
void G_CoopGlass( gentity_t *self )
{
	if ( G_CoopNumPlayers() < 2 )
	{
		return;
	}
	vec3_t mid;
	VectorAdd( self->absmin, self->absmax, mid );
	VectorScale( mid, 0.5f, mid );
	gentity_t *te = G_TempEntity( mid, EV_COOP_GLASS );
	te->svFlags |= SVF_BROADCAST;
	te->s.modelindex = self->s.modelindex;
	VectorCopy( self->pos1, te->s.origin2 );
	VectorCopy( self->pos2, te->s.angles );
	te->s.time = (int)self->splashRadius;
}

/*
==============================================================================
Per-player state that stock SP keeps in the host's cgame globals

The game and cgame are one DLL on the host, so the SP code reads
cg.saberAnimLevelPending / cg.zoomMode for "the player". With several
players those globals belong to slot 0 only; joiners get their own copy here
(never saved: joiners are not part of a .sav).
==============================================================================
*/
static int	coopSaberPending[MAX_CLIENTS];
static int	coopZoomMode[MAX_CLIENTS];
static int	coopZoomHold[MAX_CLIENTS];	// level.time until which the usercmd echo of an older mode is ignored

// a new level (level.time restarts) or a new occupant of a slot: nothing pending
void G_CoopResetZoomStance( void )
{
	memset( coopSaberPending, 0, sizeof( coopSaberPending ) );
	memset( coopZoomMode, 0, sizeof( coopZoomMode ) );
	memset( coopZoomHold, 0, sizeof( coopZoomHold ) );
}

// the stance the player asked for (bg_pmove applies it between swings)
int G_SaberPendingLevel( const gentity_t *ent )
{
	if ( !ent || ent->s.number == 0 || ent->s.number >= MAX_CLIENTS )
	{	// the host, or an NPC it controls: the cgame global as in SP
		return cg.saberAnimLevelPending;
	}
	if ( coopSaberPending[ent->s.number] <= SS_NONE && ent->client )
	{	// nothing asked yet: whatever the saber gave it
		return ent->client->ps.saberAnimLevel;
	}
	return coopSaberPending[ent->s.number];
}

void G_SaberSetPendingLevel( gentity_t *ent, int level )
{
	if ( !ent || ent->s.number == 0 || ent->s.number >= MAX_CLIENTS )
	{
		cg.saberAnimLevelPending = level;
		return;
	}
	coopSaberPending[ent->s.number] = level;
}

// what the player's screen is zoomed with: 0 none, 1 binoculars, 2 disruptor scope, 3 LA goggles
int G_CoopZoomMode( const gentity_t *ent )
{
	if ( !ent || ent->s.number == 0 || ent->s.number >= MAX_CLIENTS )
	{	// the host (and the NPC it controls, G_ControlledByPlayer): its own cgame
		return cg.zoomMode;
	}
	return coopZoomMode[ent->s.number];
}

// a host-side decision (disruptor toggle, death, knockdown, view entity): the joiner's cgame follows
void G_CoopSetZoomMode( gentity_t *ent, int mode )
{
	if ( !ent || ent->s.number == 0 || ent->s.number >= MAX_CLIENTS )
	{
		cg.zoomMode = mode;
		cg.zoomTime = cg.time;
		cg.zoomLocked = qfalse;
		return;
	}
	if ( coopZoomMode[ent->s.number] != mode )
	{
		coopZoomMode[ent->s.number] = mode;
		coopZoomHold[ent->s.number] = level.time + 700;	// usercmds already in flight still say the old mode
		gi.SendServerCommand( ent->s.number, "zoom %i", mode );
	}
}

// the joiner's cgame reports its zoom in the usercmd (binoculars/goggles toggled locally, scope echoed back)
void G_CoopReadZoomMode( gentity_t *ent, usercmd_t *ucmd )
{
	if ( !ent || !ucmd )
	{
		return;
	}
	const int mode = ( ucmd->buttons >> BUTTON_COOP_ZOOM_SHIFT ) & 3;
	ucmd->buttons &= ~BUTTON_COOP_ZOOM_MASK;
	if ( ent->s.number == 0 || ent->s.number >= MAX_CLIENTS )
	{
		return;
	}
	if ( mode != coopZoomMode[ent->s.number] && level.time >= coopZoomHold[ent->s.number] )
	{
		coopZoomMode[ent->s.number] = mode;
	}
}

/*
================
G_CoopTr / G_CoopPrintTr

The mod's texts in two languages: French for a game in French, English for any
other language (JD, 24/09). G_CoopTr answers for this machine (the host's game,
or a joiner's cgame). What the host sends goes out in the language of each
receiver: se_language is in the userinfo.
================
*/
static qboolean G_CoopLangFrench( const char *lang )
{
	return (qboolean)!Q_stricmp( lang, "french" );
}

const char *G_CoopTr( const char *fr, const char *en )
{
	char lang[32];

	gi.Cvar_VariableStringBuffer( "se_language", lang, sizeof( lang ) );
	return G_CoopLangFrench( lang ) ? fr : en;
}

static qboolean G_CoopClientFrench( int clientNum )
{
	char info[MAX_INFO_STRING];

	if ( clientNum == 0 )
	{	// the host: its own game
		return (qboolean)( G_CoopTr( "f", "e" )[0] == 'f' );
	}
	gi.GetUserinfo( clientNum, info, sizeof( info ) );
	return G_CoopLangFrench( Info_ValueForKey( info, "se_language" ) );
}

// "cmd" is print or cp; to = a client slot, or -1 for every player
void G_CoopPrintTr( int to, const char *cmd, const char *fr, const char *en, ... )
{
	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		if ( ( to >= 0 && i != to ) || !g_entities[i].client || g_entities[i].client->pers.connected == CON_DISCONNECTED )
		{
			continue;
		}
		char	text[1024];
		va_list	ap;

		va_start( ap, en );
		Q_vsnprintf( text, sizeof( text ), G_CoopClientFrench( i ) ? fr : en, ap );
		va_end( ap );
		gi.SendServerCommand( i, "%s \"%s\"", cmd, text );
	}
}

// game code asking "the player's" cgame to select a weapon: a joiner's cgame is remote
void G_CoopChangeWeapon( gentity_t *ent, int wp )
{
	extern void CG_ChangeWeapon( int num );
	if ( !ent || ent->s.number == 0 || ent->s.number >= MAX_CLIENTS )
	{
		CG_ChangeWeapon( wp );
		return;
	}
	if ( wp == WP_NONE || ( ent->client && ( ent->client->ps.stats[STAT_WEAPONS] & ( 1 << wp ) ) ) )
	{
		gi.SendServerCommand( ent->s.number, "wp %i", wp );
	}
}

/*
================
G_CoopPersonalTime / G_CoopTimeScale

Force Speed (and Rage) are bullet time in SP: the power slows the WHOLE server
clock down (the timescale cvar) and the stock code compensates its user back up
to real time - his pmove frametime is multiplied by 1/timescale, his animation
and weapon timers by timescale. The world crawls, he does not.

That model cannot be shared. In co-op the clock is the host's and it is mirrored
to the joiners, so one player casting Speed slowed everybody's game down (and a
matrix kill cam did the same): the players who cast nothing just felt lag.

So in co-op the clock stays at 1 for everyone and the power becomes a *personal*
speed-up: G_CoopTimeScale returns, for one playerState, the scale that player
would have imposed on the world, and every stock compensation site uses it
instead of the cvar. Its user then runs 1/scale times faster than a world that
keeps running at real time, so the ratio between him and everything else is
exactly the SP one (x1.33 at Speed 1, x2 at Speed 2, x4 at Speed 3) - only nobody
else's clock is touched. It costs nothing to the others and needs no new netfield:
every player
already receives forcePowersActive/forcePowerLevel of the others (msg.cpp, and
s.coopForce for the NPCs and remote players, cg_coop.cpp).

Alone (solo, or a host with no joiner) it gives back the host clock the stock
code read, so the single player game is unchanged.

noMatrixCheck: for the stock sites that did not exclude the matrix kill cam.
================
*/
extern qboolean cg_remoteClient;
extern qboolean MatrixMode;
extern float forceSpeedValue[];
extern cvar_t *g_timescale;

qboolean G_CoopPersonalTime( void )
{
	return (qboolean)( cg_remoteClient || G_CoopNumPlayers() >= 2 );
}

float G_CoopTimeScale( const playerState_t *ps, qboolean noMatrixCheck )
{
	if ( !ps || !( ps->forcePowersActive & ( ( 1 << FP_SPEED ) | ( 1 << FP_RAGE ) ) ) )
	{	// nothing to compensate: the world clock is this player's clock
		return 1.0f;
	}
	if ( G_CoopPersonalTime() )
	{	// co-op: his own scale, the world keeps running at 1 for the others
		if ( ps->forcePowersActive & ( 1 << FP_SPEED ) )
		{
			return forceSpeedValue[ps->forcePowerLevel[FP_SPEED]];
		}
		if ( ps->forcePowerLevel[FP_RAGE] >= FORCE_LEVEL_2 )
		{
			return forceSpeedValue[ps->forcePowerLevel[FP_RAGE] - 1];
		}
		return 1.0f;
	}
	// SP: the world clock itself is slowed (g_timescale), exactly the stock guards
	if ( !g_timescale || g_timescale->value <= 0.0f || g_timescale->value >= 1.0f )
	{
		return 1.0f;
	}
	if ( MatrixMode && !noMatrixCheck )
	{
		return 1.0f;
	}
	return g_timescale->value;
}

/*
================
G_CoopUpdateTimescale

Force Speed / Rage are bullet time for the whole party (SP design, shared
host clock): one arbiter instead of every player's ClientThink rewriting the
cvar. The slowest active power wins; the cvar goes back to 1 once nobody is
speeding or raging. Called once per frame after the client thinks.
Solo / host alone ONLY: with at least two players the clock is never scaled,
the power is a personal speed-up instead (G_CoopTimeScale above).
================
*/
extern float forceSpeedValue[];
extern qboolean MatrixMode;
extern cvar_t *g_timescale;
extern cvar_t *g_skippingcin;
void G_CoopUpdateTimescale( void )
{
	static float	lastTs = 1.0f;
	float			ts = 1.0f;

	if ( G_CoopPersonalTime() )
	{	// coop: Force Speed/Rage are a personal speed-up now (G_CoopTimeScale); the world
		// clock stays at 1 so the players who cast nothing keep a game at normal speed
		if ( lastTs < 1.0f )
		{	// we were alone and slowing the clock when the joiner arrived: give it back
			if ( g_timescale->value != 1.0f && !MatrixMode && !g_skippingcin->integer )
			{
				gi.cvar_set( "timescale", "1" );
			}
			lastTs = 1.0f;
		}
		return;
	}

	for ( int i = 0; i < MAX_CLIENTS; i++ )
	{
		const gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->health <= 0 )
		{
			continue;
		}
		const playerState_t *ps = &ent->client->ps;
		if ( ps->forcePowersActive & ( 1 << FP_SPEED ) )
		{
			ts = Q_min( ts, forceSpeedValue[ps->forcePowerLevel[FP_SPEED]] );
		}
		if ( ( ps->forcePowersActive & ( 1 << FP_RAGE ) ) && ps->forcePowerLevel[FP_RAGE] >= FORCE_LEVEL_2 )
		{
			ts = Q_min( ts, forceSpeedValue[ps->forcePowerLevel[FP_RAGE] - 1] );
		}
	}
	if ( ts < 1.0f )
	{	// as in SP, re-assert it every frame while a power runs
		if ( fabs( g_timescale->value - ts ) > 0.001f )
		{
			gi.cvar_set( "timescale", va( "%4.2f", ts ) );
		}
	}
	else if ( lastTs < 1.0f )
	{	// the last power stopped: back to 1 unless something else owns the clock now
		if ( g_timescale->value != 1.0f && !MatrixMode && !g_skippingcin->integer )
		{
			gi.cvar_set( "timescale", "1" );
		}
	}
	lastTs = ts;
}

void G_CoopForwardSound( int entNum, int channel, int index, const char *path, int customSet )
{
	if ( G_CoopNumPlayers() < 2 )
	{
		return;
	}
	// Indexed, non-voice sounds (weapon fire, saber swings, footsteps, movers:
	// dozens per second in a fight) travel as an unreliable temp-entity event,
	// which the snapshot delivers without the 64-command reliable window ever
	// filling up. Voice lines keep the reliable path: they are rare, must not
	// be lost (captions, lip sync) and are heard from anywhere.
	const qboolean voice = (qboolean)( channel == CHAN_VOICE || channel == CHAN_VOICE_ATTEN || channel == CHAN_VOICE_GLOBAL );
	if ( index > 0 && index < MAX_SOUNDS && !voice && entNum >= 0 && entNum < MAX_GENTITIES )
	{
		gentity_t *te = G_TempEntity( g_entities[entNum].currentOrigin, EV_COOP_SOUND );
		te->svFlags |= SVF_BROADCAST;		// audible range is not the PVS
		te->s.otherEntityNum = entNum;
		te->s.time2 = channel;
		te->s.eventParm = index;
		return;
	}
	for ( int i = 1; i < MAX_CLIENTS; i++ )
	{
		const gentity_t *cl = &g_entities[i];
		if ( cl->inuse && cl->client && cl->client->pers.connected == CON_CONNECTED )
		{
			gi.SendServerCommand( i, "snd %i %i %i %i \"%s\"", entNum, channel, index, customSet, path ? path : "" );
		}
	}
}

// CG_TryPlayCustomSound for the host, forwarded to remote clients. A '*'
// name (pain, anger, taunts...) only means something against the NPC's sound
// tables, which the host alone registered (CG_RegisterNPCCustomSounds): look
// it up here and forward the file it resolved to.
extern sfxHandle_t CG_CustomSoundHandle( int entityNum, const char *soundName, int customSoundSet );
extern void cgi_S_SoundName( sfxHandle_t handle, char *buf, int buflen );
extern void cgi_S_StartSound( const vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx );
qboolean G_CoopCustomSound( vec3_t origin, int entityNum, soundChannel_t channel, const char *soundName, int customSoundSet )
{
	if ( soundName[0] == '*' && G_CoopNumPlayers() >= 2 )
	{
		char path[MAX_QPATH];
		const sfxHandle_t handle = CG_CustomSoundHandle( entityNum, soundName, customSoundSet );
		path[0] = '\0';
		if ( handle )
		{
			cgi_S_SoundName( handle, path, sizeof( path ) );
		}
		if ( path[0] )
		{
			G_CoopForwardSound( entityNum, channel, -1, path, -1 );
		}
		if ( !handle )
		{
			return qfalse;
		}
		cgi_S_StartSound( origin, entityNum, channel, handle );
		return qtrue;
	}
	G_CoopForwardSound( entityNum, channel, -1, soundName, customSoundSet );
	return CG_TryPlayCustomSound( origin, entityNum, channel, soundName, customSoundSet );
}

// cgi_S_StartSound by path for the host, forwarded to remote clients
extern sfxHandle_t cgi_S_RegisterSound( const char *sample );
void G_CoopSoundPath( const vec3_t origin, int entityNum, int channel, const char *path )
{
	G_CoopForwardSound( entityNum, channel, -1, path, -1 );
	cgi_S_StartSound( origin, entityNum, channel, cgi_S_RegisterSound( path ) );
}

/*
================
G_CoopWaveFrame — coop wave mode (g_coopWaves)

When enabled, the map's scripted enemies are left alone; every time no
TEAM_ENEMY NPCs remain (and a short breather has passed), a bigger wave
spawns around the players. Wave number goes out through sv_coopWave so
the engine can ship it with the map-time report for the survival board.
================
*/
static int	coopWaveNum = 0;
static int	coopWaveCooldown = 0;
static int	coopWaveNextCheck = 0;

static const char *coopWaveTypes[] = {
	// tier 0 — waves 1-3
	"stormtrooper", "imperial", "jawa_armed",
	// tier 1 — waves 4-7
	"trandoshan", "weequay", "rodian", "gran",
	// tier 2 — waves 8-11
	"cultist", "swamptrooper", "snowtrooper",
	// tier 3 — waves 12+
	"reborn", "shadowtrooper", "hazardtrooper", "noghri",
};
#define COOP_WAVE_T1 3
#define COOP_WAVE_T2 7
#define COOP_WAVE_T3 11
#define COOP_WAVE_TYPES 14

static void G_CoopWaveSpawnOne( gentity_t *anchor, const char *npcType, int wave )
{
	gentity_t *spawner = G_Spawn();
	if ( !spawner )
	{
		return;
	}
	spawner->e_ThinkFunc = thinkF_G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;

	// ring around a random player: random bearing, 350-700 units out,
	// dropped to the floor
	vec3_t	end, fwd;
	trace_t	tr;
	float	ang = (float)( rand() % 360 ) * M_PI / 180.0f;
	float	dist = 350.0f + (float)( rand() % 350 );
	fwd[0] = cosf( ang ); fwd[1] = sinf( ang ); fwd[2] = 0;
	VectorMA( anchor->currentOrigin, dist, fwd, end );
	end[2] = anchor->currentOrigin[2] + 64;
	VectorCopy( end, tr.endpos );
	gi.trace( &tr, end, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	end[2] -= 512;
	gi.trace( &tr, tr.endpos, NULL, NULL, end, 0, MASK_SOLID, (EG2_Collision)0, 0 );
	if ( tr.fraction == 1.0f )
	{
		// no floor found — fall back to beside the anchor
		VectorCopy( anchor->currentOrigin, tr.endpos );
		tr.endpos[2] += 8;
	}
	VectorCopy( tr.endpos, end );
	end[2] += 24;
	G_SetOrigin( spawner, end );
	VectorCopy( spawner->currentOrigin, spawner->s.origin );
	spawner->s.angles[1] = ang * 180.0f / M_PI + 180.0f; // face the players
	gi.linkentity( spawner );

	spawner->NPC_type = G_NewString( npcType );
	spawner->NPC_targetname = G_NewString( "wave" );
	spawner->count = 1;
	spawner->delay = 0;
	spawner->wait = 500;
	NPC_Spawn( spawner, spawner, spawner );

	// scale HP with the wave number (+15% per wave over 1)
	for ( int i = globals.num_entities - 1; i >= 0; i-- )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->health > 0 && e->targetname
			&& !Q_stricmp( e->targetname, "wave" ) && e->max_health > 0
			&& e->health == e->max_health )
		{
			int hp = (int)( e->max_health * ( 1.0f + 0.15f * ( wave - 1 ) ) );
			e->health = hp;
			e->max_health = hp;
			if ( e->client )
			{
				e->client->ps.stats[STAT_HEALTH] = hp;
				e->client->ps.stats[STAT_MAX_HEALTH] = hp;
			}
			break;
		}
	}
}

void G_CoopWaveFrame( void )
{
	extern cvar_t *g_coopWaves;
	if ( !g_coopWaves || !g_coopWaves->integer )
	{
		if ( coopWaveNum > 0 )
		{
			coopWaveNum = 0;
			gi.cvar_set( "sv_coopWave", "0" );
		}
		return;
	}
	if ( level.time < coopWaveNextCheck )
	{
		return;
	}
	coopWaveNextCheck = level.time + 1000;

	// wave-mode enemy count (Simon doesn't count — he's his own event)
	int enemies = 0;
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && e->health > 0 && e->client
			&& e->client->playerTeam == TEAM_ENEMY
			&& ( !e->targetname || Q_stricmp( e->targetname, "simon" ) ) )
		{
			enemies++;
		}
	}
	if ( enemies > 0 || level.time < coopWaveCooldown )
	{
		return;
	}

	// next wave
	coopWaveNum++;
	int wave = coopWaveNum;
	gi.cvar_set( "sv_coopWave", va( "%i", wave ) );
	coopWaveCooldown = level.time + 8000; // 8s breather between waves
	G_CoopPrintTr( -1, "cp", "VAGUE @%i", "WAVE @%i", wave );

	int count = 2 + wave; // wave 1 = 3, wave 5 = 7, caps at 15
	if ( count > 15 ) count = 15;

	// roster tier by wave
	int lo = 0, hi = COOP_WAVE_T1;
	if ( wave >= 8 && wave < 12 )      { lo = COOP_WAVE_T1; hi = COOP_WAVE_T2; }
	else if ( wave >= 12 )             { lo = COOP_WAVE_T2; hi = COOP_WAVE_TYPES; }
	else if ( wave >= 4 )              { lo = 0; hi = COOP_WAVE_T2; }

	for ( int i = 0; i < count; i++ )
	{
		// anchor on a random living player
		gentity_t *anchor = G_CoopPlayerSlot( rand() % MAX_CLIENTS );
		if ( !anchor || !anchor->client )
		{
			anchor = G_CoopPlayerSlot( 0 );
		}
		if ( !anchor || !anchor->client )
		{
			break;
		}
		G_CoopWaveSpawnOne( anchor, coopWaveTypes[ lo + rand() % ( hi - lo ) ], wave );
	}
}

/*
==============================================================================
Holocron scatter (g_coopHolocrons)

Port of the AdvancedJK multiplayer holocron auto-spawn: when the host sets
g_coopHolocrons N the game scatters N force holocrons at random item/player
spawn points on every map, so co-op runs have loot to hunt. Holocrons the map
already places count toward N, so handcrafted maps stay handcrafted.
==============================================================================
*/
static int		coopHoloTotal = 0;			// holocrons on the current map
static int		coopHoloFound[MAX_CLIENTS];	// pickups per player slot
static char		coopHoloDoneList[64];		// "cn;cn;" slots that completed the set

/*
==============
G_CoopHoloPicked

A player just grabbed a holocron. Track the set — collecting every holocron
on the map earns that player a free site holocron (reported in the run post).
==============
*/
void G_CoopHoloPicked( int cn )
{
	if ( coopHoloTotal <= 0 || cn < 0 || cn >= MAX_CLIENTS )
	{
		return;
	}
	if ( ++coopHoloFound[cn] < coopHoloTotal )
	{
		return;
	}
	if ( strstr( coopHoloDoneList, va( "%i;", cn ) ) )
	{
		return;
	}
	gentity_t *p = G_CoopPlayerSlot( cn );
	if ( p )
	{
		G_CoopPrintTr( -1, "print", "%s ^2a collecte tous les holocrons !\n",
			"%s ^2collected every holocron — site holocron unlocked!\n", p->client->pers.netname );
	}
	// engine relays the completer list to the site with the run report
	Q_strcat( coopHoloDoneList, sizeof( coopHoloDoneList ), va( "%i;", cn ) );
	gi.cvar_set( "sv_coopHolo", coopHoloDoneList );
}

static const char *coopHoloTypes[] = {
	"holocron_force_heal",
	"holocron_force_levitation",
	"holocron_force_speed",
	"holocron_force_push",
	"holocron_force_pull",
	"holocron_force_telepathy",
	"holocron_force_grip",
	"holocron_force_lightining",	// the engine's original classname typo
	"holocron_force_saberthrow",
};
#define COOP_HOLO_TYPES	(sizeof(coopHoloTypes)/sizeof(coopHoloTypes[0]))

void G_CoopHolocronSpawn( void )
{
	extern cvar_t *g_coopHolocrons;
	extern qboolean G_CallSpawn( gentity_t *ent );

	memset( coopHoloFound, 0, sizeof( coopHoloFound ) );
	coopHoloDoneList[0] = 0;
	gi.cvar_set( "sv_coopHolo", "" );

	int want = g_coopHolocrons ? g_coopHolocrons->integer : 0;
	if ( want > 32 ) want = 32;

	int placed = 0;
	gentity_t *spots[256];
	int spotCount = 0;

	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *e = &g_entities[i];
		if ( !e->inuse ) continue;
		if ( e->classname && !Q_stricmpn( e->classname, "holocron_", 9 ) )
		{
			placed++;
			continue;
		}
		if ( e->item && spotCount < 256 )
		{
			spots[spotCount++] = e;
		}
	}

	coopHoloTotal = placed;
	want -= placed;
	if ( want <= 0 ) return;

	if ( spotCount == 0 )
	{ // fallback: player spawn points
		gentity_t *spot = NULL;
		while ( ( spot = G_Find( spot, FOFS(classname), "info_player_deathmatch" ) ) != NULL && spotCount < 256 )
		{
			spots[spotCount++] = spot;
		}
	}
	if ( spotCount == 0 )
	{
		gi.Printf( "Holocron: no spawn points found.\n" );
		return;
	}

	int spawned = 0;
	for ( int n = 0; n < want; n++ )
	{
		gentity_t *holo = G_Spawn();
		if ( !holo ) break;

		holo->classname = G_NewString( coopHoloTypes[ Q_irand( 0, (int)COOP_HOLO_TYPES - 1 ) ] );

		// force level granted — mostly 1-2, the odd full level 3
		int roll = Q_irand( 1, 10 );
		holo->count = ( roll <= 5 ) ? 1 : ( roll <= 8 ) ? 2 : 3;

		gentity_t *spot = spots[ Q_irand( 0, spotCount - 1 ) ];
		VectorCopy( spot->currentOrigin, holo->s.origin );
		holo->s.origin[2] += 16;
		VectorCopy( holo->s.origin, holo->currentOrigin );

		if ( !G_CallSpawn( holo ) )
		{
			G_FreeEntity( holo );
			continue;
		}
		spawned++;
	}
	coopHoloTotal += spawned;
	if ( spawned )
	{
		gi.Printf( "Holocron: scattered %i holocrons over %i spawn points.\n", spawned, spotCount );
		G_CoopPrintTr( -1, "print", "%i holocrons caches sur cette carte...\n", "%i holocrons hidden on this map...\n", spawned );
	}
}

/*
================
G_CoopIsSupporter

True when the client's linked advjk_account appears in sv_coopSupporters
("acct,acct" — the engine refreshes it from every status reply). A bare
name match: worst case a faker wears a gold name, credits still need
the account key.
================
*/
qboolean G_CoopIsSupporter( const gentity_t *ent ) {
	char	userinfo[MAX_INFO_STRING], list[512];
	char	acct[64], *tok;

	gi.GetUserinfo( ent->s.number, userinfo, sizeof( userinfo ) );
	Q_strncpyz( acct, Info_ValueForKey( userinfo, "advjk_account" ), sizeof( acct ) );
	if ( !acct[0] ) {
		return qfalse;
	}
	gi.Cvar_VariableStringBuffer( "sv_coopSupporters", list, sizeof( list ) );
	for ( tok = strtok( list, "," ); tok; tok = strtok( NULL, "," ) ) {
		if ( !Q_stricmp( tok, acct ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
================
G_CoopSupporterFrame

The site answers the status ping with which online accounts are
supporters; when that list changes, refresh the s\ flag in every
CS_PLAYERS string so the HUD recolors without a reconnect.
================
*/
void G_CoopSupporterFrame( void ) {
	static char	lastList[512];
	char		list[512];
	int			i;

	gi.Cvar_VariableStringBuffer( "sv_coopSupporters", list, sizeof( list ) );
	if ( !strcmp( list, lastList ) ) {
		return;
	}
	Q_strncpyz( lastList, list, sizeof( lastList ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t	*ent = &g_entities[i];
		char		cs[MAX_INFO_STRING];

		if ( !ent->inuse || !ent->client ) {
			continue;
		}
		gi.GetConfigstring( CS_PLAYERS + i, cs, sizeof( cs ) );
		if ( !cs[0] ) {
			continue;
		}
		Info_SetValueForKey( cs, "s", G_CoopIsSupporter( ent ) ? "1" : "0" );
		gi.SetConfigstring( CS_PLAYERS + i, cs );
	}
}

/*
=========================================================================
Co-op cheat toys: gravity gun + portal gun (helpUsObi-gated like `control`)

gravgun      - pick up the prop under your crosshair; run again to throw it
gravdrop     - set the held prop down gently
portal       - place your next portal (alternates A then B) where you aim
portal_a / portal_b - place a specific portal
portal_clear - remove both of your portals

Portals preserve the traveller's speed through the exit portal's frame,
so a floor->wall pair flings you sideways — the portal-gun trick.
=========================================================================
*/

#define GRAV_HOLD_DIST	72.0f
#define GRAV_REACH		450.0f
#define GRAV_THROW_VEL	1600.0f
#define PORTAL_ENTER_R	44.0f	// how close to the portal centre line counts
#define PORTAL_ENTER_D	40.0f	// how far in front of the plane the trigger slab reaches
#define PORTAL_CD_MS	600		// re-entry grace after a teleport
#define PORTAL_RANGE	8192.0f
#define PORTAL_HW		34.0f	// oval half-width — must match COOP_PORTAL_HW in cg_ents.cpp
#define PORTAL_HH		56.0f	// oval half-height — must match COOP_PORTAL_HH in cg_ents.cpp

static gentity_t *gravHeldEnt[MAX_CLIENTS];
static int        gravHeldContents[MAX_CLIENTS];	// saved contents while carried
static struct { gentity_t *ent; gentity_t *owner; int until; } gravThrown[64];
static int        gravThrownN;
static gentity_t *coopPortal[MAX_CLIENTS][2];		// [client][A=0,B=1]
static int        coopPortalNext[MAX_CLIENTS];		// which slot `portal` fills next
static int        coopPortalCd[MAX_GENTITIES];
static int        gravHeldFx[MAX_CLIENTS];			// next swirl-effect time per holder

extern void SP_fx_runner( gentity_t *ent );

/*
================
G_CoopGravGrabbable

Anything small enough to sling: items, live projectiles, breakable props.
Players/NPCs are out — Force Grip already owns that space.
================
*/
static qboolean G_CoopGravGrabbable( const gentity_t *e )
{
	if ( !e || !e->inuse || e->client || e->NPC || e->s.number < MAX_CLIENTS )
	{
		return qfalse;
	}
	if ( e->item || e->s.eType == ET_MISSILE )
	{
		return qtrue;
	}
	if ( e->classname && !Q_stricmp( e->classname, "misc_model_breakable" ) )
	{
		return qtrue;
	}
	return qfalse;
}

/*
================
G_CoopGravReleaseDir

Put the held prop back into the world. dir==NULL drops it where it floats;
otherwise it launches along dir and becomes an armed projectile that does
impact damage on the first thing it hits.
================
*/
static void G_CoopGravReleaseDir( gentity_t *player, const vec3_t dir )
{
	const int	cn = player->s.number;
	gentity_t	*e = gravHeldEnt[cn];

	if ( !e || !e->inuse )
	{
		gravHeldEnt[cn] = NULL;
		return;
	}
	gravHeldEnt[cn] = NULL;

	e->contents = gravHeldContents[cn];
	e->s.groundEntityNum = ENTITYNUM_NONE;
	e->s.pos.trType = TR_GRAVITY;
	e->s.pos.trTime = level.time;
	VectorCopy( e->currentOrigin, e->s.pos.trBase );

	if ( dir )
	{
		vec3_t	vel;
		VectorScale( dir, GRAV_THROW_VEL, vel );
		vel[2] += 80.0f;
		VectorCopy( vel, e->s.pos.trDelta );
		if ( gravThrownN < (int)( sizeof( gravThrown ) / sizeof( gravThrown[0] ) ) )
		{
			gravThrown[gravThrownN].ent   = e;
			gravThrown[gravThrownN].owner = player;
			gravThrown[gravThrownN].until = level.time + 5000;
			gravThrownN++;
		}
	}
	else
	{
		VectorClear( e->s.pos.trDelta );
	}
	gi.linkentity( e );
}

/*
================
G_CoopGiveCheatWeapon

Hand a coop cheat weapon over: set the ownership bit, then make the player's
cgame select it (local call for the host, "wp" server command for joiners).
================
*/
static void G_CoopGiveCheatWeapon( gentity_t *ent, int wp )
{
	ent->client->ps.stats[STAT_WEAPONS] |= ( 1 << wp );
	G_CoopChangeWeapon( ent, wp );
}

/*
================
G_CoopGravDo

The gun's actual work: grab the prop you're aiming at, or release the one
you're holding (dropOnly sets it down, otherwise it launches). Shared by the
gravgun command and WP_GRAVGUN's fire buttons.
================
*/
static void G_CoopGravDo( gentity_t *ent, qboolean dropOnly )
{
	const int	cn = ent->s.number;
	vec3_t		eye, fwd, end;
	trace_t		tr;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, fwd, NULL, NULL );

	if ( gravHeldEnt[cn] )
	{
		if ( dropOnly )
		{
			G_CoopGravReleaseDir( ent, NULL );
		}
		else
		{
			G_CoopGravReleaseDir( ent, fwd );
		}
		G_PlayEffect( "force/pushblur", eye, fwd );
		return;
	}
	if ( dropOnly )
	{
		return;
	}

	// pick the grabbable prop nearest the crosshair: roughly forward,
	// within reach, with a clear line (world geometry can't hide it)
	VectorMA( eye, GRAV_REACH, fwd, end );
	gentity_t	*best = NULL;
	float		bestScore = 1.0e9f;
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ )
	{
		gentity_t	*e = &g_entities[i];
		vec3_t		to;
		float		dist, align;

		if ( !G_CoopGravGrabbable( e ) )
		{
			continue;
		}
		VectorSubtract( e->currentOrigin, eye, to );
		dist = VectorNormalize( to );
		if ( dist > GRAV_REACH )
		{
			continue;
		}
		align = DotProduct( to, fwd );
		if ( align < 0.85f )
		{
			continue;
		}
		gi.trace( &tr, eye, NULL, NULL, e->currentOrigin, cn, MASK_SOLID, (EG2_Collision)0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != e->s.number )
		{
			continue;	// behind a wall
		}
		const float score = dist * ( 2.0f - align );	// close + centred wins
		if ( score < bestScore )
		{
			bestScore = score;
			best = e;
		}
	}

	if ( !best )
	{
		gi.SendServerCommand( cn, "print \"gravgun: nothing to grab — aim at an item, prop, or projectile\n\"" );
		return;
	}

	gravHeldEnt[cn] = best;
	gravHeldContents[cn] = best->contents;
	best->contents = 0;							// untouchable/unsolid while carried
	best->s.groundEntityNum = cn;					// keeps items from resuming gravity
	best->s.pos.trType = TR_STATIONARY;
	best->s.pos.trTime = level.time;
	VectorClear( best->s.pos.trDelta );
	gravHeldFx[cn] = 0;

	gi.SendServerCommand( cn, "print \"gravgun: holding %s — fire to throw, alt-fire to drop\n\"",
		best->item ? best->item->classname : ( best->classname ? best->classname : "prop" ) );
	G_PlayEffect( "force/pushblur", best->currentOrigin, fwd );
}

/*
================
G_CoopGravGunCommand

gravgun: gives you WP_GRAVGUN (draws it if needed) and does the grab/throw so
the bare command still works like it did before the gun existed.
gravdrop: release the held prop without throwing.
================
*/
void G_CoopGravGunCommand( gentity_t *ent, qboolean dropOnly )
{
	if ( !CheatsOk( ent ) )
	{
		return;
	}
	if ( dropOnly )
	{
		G_CoopGravDo( ent, qtrue );
		return;
	}
	G_CoopGiveCheatWeapon( ent, WP_GRAVGUN );
	G_CoopGravDo( ent, qfalse );
}

/*
================
G_CoopPortalFrame

Portal-local basis: fwd = into the portal, up = world-up projected into the
plane (a fixed world tangent for floors/ceilings so pairs stay consistent),
right = up x fwd keeping (fwd,right,up) right-handed.  This is the same frame
convention the renderer's portal-view path ends up using after the roll fixup,
so the window image and the travel transform always agree.
================
*/
static void G_CoopPortalFrame( const vec3_t n, vec3_t fwd, vec3_t right, vec3_t up )
{
	vec3_t	hint;
	float	d;

	if ( fabs( n[2] ) < 0.95f ) VectorSet( hint, 0, 0, 1 ); else VectorSet( hint, 1, 0, 0 );
	d = DotProduct( hint, n );
	VectorMA( hint, -d, n, up );
	VectorNormalize( up );

	VectorScale( n, -1.0f, fwd );
	CrossProduct( up, fwd, right );
}

/*
================
G_CoopPortalTransform

Map a vector through the portal pair: express in the source portal's frame,
flip the into/right components (the 180-degree turn about portal-up that sends
"going in" to "coming out"), express out in the destination frame.
================
*/
static void G_CoopPortalTransform( const vec3_t nSrc, const vec3_t nDst, const vec3_t in, vec3_t out )
{
	vec3_t	fA, rA, uA, fB, rB, uB;
	float	cf, cr, cu;

	G_CoopPortalFrame( nSrc, fA, rA, uA );
	G_CoopPortalFrame( nDst, fB, rB, uB );
	cf = DotProduct( in, fA );
	cr = DotProduct( in, rA );
	cu = DotProduct( in, uA );
	VectorScale( fB, -cf, out );
	VectorMA( out, -cr, rB, out );
	VectorMA( out, cu, uB, out );
}

/*
================
G_CoopPortalRoll

Roll the remote camera needs about its -nB axis so the rendered window shows
exactly the G_CoopPortalTransform view.  The renderer builds the camera frame
as (-nB, PerpendicularVector(nB), ...); we rotate that frame's axis[1] onto
M_des(PV(nA)) — the image of the window surface's own tangent.
================
*/
static float G_CoopPortalRoll( const vec3_t nA, const vec3_t nB )
{
	vec3_t	fA, rA, uA, fB, rB, uB, pvA, pvB, want, axis, cross;
	float	cf, cr, cu;

	G_CoopPortalFrame( nA, fA, rA, uA );
	G_CoopPortalFrame( nB, fB, rB, uB );
	PerpendicularVector( pvA, nA );
	PerpendicularVector( pvB, nB );

	cf = DotProduct( pvA, fA );
	cr = DotProduct( pvA, rA );
	cu = DotProduct( pvA, uA );
	VectorScale( fB, -cf, want );
	VectorMA( want, -cr, rB, want );
	VectorMA( want, cu, uB, want );

	VectorScale( nB, -1.0f, axis );
	CrossProduct( pvB, want, cross );
	return (float)( atan2( DotProduct( cross, axis ), DotProduct( pvB, want ) ) * ( 180.0 / M_PI ) );
}

/*
================
G_CoopPortalFx

Marker glow for an unlinked portal; freed once the pair links and the pane
becomes a real see-through window.
================
*/
static void G_CoopPortalFx( gentity_t *p, int slot, qboolean want )
{
	if ( p->owner && p->owner->inuse )
	{
		G_FreeEntity( p->owner );
	}
	p->owner = NULL;
	if ( !want )
	{
		return;
	}
	gentity_t *fx = G_Spawn();
	fx->fxFile = (char *)( slot == 0 ? "mp/bluecrystalrespawn" : "mp/redcrystalrespawn" );
	fx->classname = "coop_portalfx";
	VectorSet( fx->mins, -8.0f, -8.0f, -8.0f );
	VectorSet( fx->maxs, 8.0f, 8.0f, 8.0f );
	VectorMA( p->s.pos.trBase, 4.0f, p->movedir, fx->s.origin );
	VectorCopy( fx->s.origin, fx->currentOrigin );
	SP_fx_runner( fx );
	p->owner = fx;
}

/*
================
G_CoopPortalRelink

Re-resolve both portals of a pair after placement/removal: linked portals sync
their ET_PORTAL snapshot state (origin2 = remote camera anchor, eventParm =
remote camera dir, clientNum = camera roll) so the client renders a real
destination view; unlinked ones fall back to marker glow.
================
*/
static void G_CoopPortalRelink( int cn )
{
	for ( int s = 0; s < 2; s++ )
	{
		gentity_t *p = coopPortal[cn][s];
		gentity_t *twin = coopPortal[cn][s ^ 1];
		if ( !p || !p->inuse )
		{
			continue;
		}
		const qboolean linked = (qboolean)( twin && twin->inuse );
		p->s.angles2[1] = linked ? 1.0f : 0.0f;
		if ( linked )
		{
			VectorCopy( twin->s.pos.trBase, p->s.origin2 );
			p->s.eventParm = DirToByte( twin->movedir );
			p->s.clientNum = (int)( G_CoopPortalRoll( p->movedir, twin->movedir ) * 256.0f / 360.0f );
			G_CoopPortalFx( p, s, qfalse );
		}
		else
		{
			VectorCopy( p->s.pos.trBase, p->s.origin2 );
			p->s.eventParm = DirToByte( p->movedir );
			p->s.clientNum = 0;
			G_CoopPortalFx( p, s, qtrue );
		}
	}
}

/*
================
G_CoopPortalPlace

Spawn (or respawn) a portal marker + its fx buddy on the aimed-at surface.
================
*/
static void G_CoopPortalPlace( gentity_t *ent, int slot )
{
	const int	cn = ent->s.number;
	vec3_t		eye, fwd, end, org;
	trace_t		tr;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, fwd, NULL, NULL );
	VectorMA( eye, PORTAL_RANGE, fwd, end );
	gi.trace( &tr, eye, NULL, NULL, end, cn, MASK_SOLID, (EG2_Collision)0, 0 );

	if ( tr.fraction >= 1.0f )
	{
		gi.SendServerCommand( cn, "print \"portal: aim at a wall, floor, or ceiling\n\"" );
		return;
	}

	// free the old portal in this slot (its fx buddy rides along in ->owner)
	{
		gentity_t *old = coopPortal[cn][slot];
		if ( old && old->inuse )
		{
			if ( old->owner && old->owner->inuse )
			{
				G_FreeEntity( old->owner );
			}
			G_FreeEntity( old );
		}
		coopPortal[cn][slot] = NULL;
	}

	gentity_t *p = G_Spawn();
	p->classname = "coop_portal";
	VectorMA( tr.endpos, 2.0f, tr.plane.normal, org );
	VectorCopy( tr.plane.normal, p->movedir );

	// keep the oval's bottom out of the floor — a portal half-buried means
	// anyone stepping through materialises below the floor.  If a drop to the
	// oval's lower edge hits early, slide the portal up the plane (only when a
	// wall still backs the new centre, so it can't float in mid-air).
	if ( fabs( p->movedir[2] ) < 0.95f )
	{
		vec3_t	f, r, u, t0, t1;
		trace_t	tr2;

		G_CoopPortalFrame( p->movedir, f, r, u );
		VectorMA( org, 4.0f, p->movedir, t0 );
		VectorMA( t0, -( PORTAL_HH + 10.0f ), u, t1 );
		gi.trace( &tr2, t0, NULL, NULL, t1, cn, MASK_SOLID, (EG2_Collision)0, 0 );
		if ( tr2.fraction < 1.0f )
		{
			const float	need = ( PORTAL_HH + 10.0f ) * ( 1.0f - tr2.fraction );
			vec3_t		slid, back;

			VectorMA( org, need, u, slid );
			VectorMA( slid, -12.0f, p->movedir, back );
			gi.trace( &tr2, slid, NULL, NULL, back, cn, MASK_SOLID, (EG2_Collision)0, 0 );
			if ( tr2.fraction < 1.0f )
			{
				VectorCopy( slid, org );
			}
		}
	}

	// reject placements where the oval hangs off an edge: every rim point must
	// have surface right behind it.  A rim point already inside solid (a rock
	// corner poking through the oval) counts as a fail too — the oval would
	// clip into it and funnel travellers into the rock.
	{
		vec3_t	f, r, u, edge, front, back;
		trace_t	tr2;
		int		i, ok = 0;

		G_CoopPortalFrame( p->movedir, f, r, u );
		for ( i = 0; i < 16; i++ )
		{
			const float a = (float)i / 16.0f * 2.0f * (float)M_PI;
			VectorMA( org, cos( a ) * PORTAL_HW, r, edge );
			VectorMA( edge, sin( a ) * PORTAL_HH, u, edge );
			VectorMA( edge, 4.0f, p->movedir, front );
			VectorMA( edge, -6.0f, p->movedir, back );
			gi.trace( &tr2, front, NULL, NULL, back, cn, MASK_SOLID, (EG2_Collision)0, 0 );
			if ( tr2.fraction < 1.0f && !tr2.startsolid )
			{
				ok++;
			}
		}
		if ( ok < 13 )
		{
			G_FreeEntity( p );
			gi.SendServerCommand( cn, "print \"portal: no room — needs a bigger, flatter surface\n\"" );
			return;
		}
	}
	G_SetOrigin( p, org );
	p->contents = 0;
	p->clipmask = 0;
	// a point-sized box can land wholly in a solid leaf and get numClusters=0
	// (never snapshot) — span enough to always reach the room's open leaf
	VectorSet( p->mins, -24.0f, -24.0f, -24.0f );
	VectorSet( p->maxs, 24.0f, 24.0f, 24.0f );
	p->svFlags |= SVF_PORTAL;				// merge the exit's PVS into snapshots
	p->s.eType = ET_PORTAL;					// drives the renderer's see-through path
	p->s.angles[0] = DirToByte( p->movedir );	// our own normal, for the window poly
	p->s.angles2[0] = slot + 1;				// coop marker + color slot
	gi.linkentity( p );

	coopPortal[cn][slot] = p;
	G_CoopPortalRelink( cn );

	gi.SendServerCommand( cn, "print \"portal %s placed — %s\n\"", slot ? "B" : "A",
		( coopPortal[cn][0] && coopPortal[cn][1] ) ? "pair linked, walk in" : "place its twin to link" );
}

/*
================
G_CoopPortalCommand

portal: place the next slot (alternating A/B); portal_a/portal_b: a given
slot; portal_clear: remove both.
================
*/
void G_CoopPortalCommand( gentity_t *ent, int slot )
{
	if ( !CheatsOk( ent ) )
	{
		return;
	}
	const int	cn = ent->s.number;
	if ( slot >= 2 )
	{
		for ( int s = 0; s < 2; s++ )
		{
			gentity_t *p = coopPortal[cn][s];
			if ( p && p->inuse )
			{
				if ( p->owner && p->owner->inuse )
				{
					G_FreeEntity( p->owner );
				}
				G_FreeEntity( p );
			}
			coopPortal[cn][s] = NULL;
		}
		gi.SendServerCommand( cn, "print \"portals cleared\n\"" );
		return;
	}
	// drawing the gun is part of the deal now — the command still places too
	G_CoopGiveCheatWeapon( ent, WP_PORTALGUN );
	if ( slot < 0 )
	{
		slot = coopPortalNext[cn];
		coopPortalNext[cn] ^= 1;
	}
	G_CoopPortalPlace( ent, slot );
}

/*
================
G_CoopWeaponFire

WP_GRAVGUN / WP_PORTALGUN dispatch from FireWeapon. Fire and alt-fire are the
two buttons these guns live on; the cheat commands above just grant them.
================
*/
void G_CoopWeaponFire( gentity_t *ent, qboolean alt_fire )
{
	if ( !ent->client || ent->NPC || ent->s.number >= MAX_CLIENTS )
	{
		return;
	}
	if ( ent->s.weapon == WP_GRAVGUN )
	{
		G_CoopGravDo( ent, alt_fire );	// fire: grab/throw, alt: drop gently
	}
	else if ( ent->s.weapon == WP_PORTALGUN )
	{
		G_CoopPortalPlace( ent, alt_fire ? 1 : 0 );	// fire: blue A, alt: red B
	}
}

/*
================
G_CoopPortalTravel

Teleport through a linked portal pair using the same transform the rendered
window shows: relative position, velocity, and view direction all map through
G_CoopPortalTransform, so stepping through is exactly what the pane promised.
================
*/
static void G_CoopPortalTravel( gentity_t *e, gentity_t *src, gentity_t *dst, const vec3_t vel )
{
	vec3_t	vOut, rel, dest, exAng, vaF;

	G_CoopPortalTransform( src->movedir, dst->movedir, vel, vOut );

	VectorSubtract( e->currentOrigin, src->currentOrigin, rel );
	G_CoopPortalTransform( src->movedir, dst->movedir, rel, dest );
	VectorAdd( dest, dst->currentOrigin, dest );
	VectorMA( dest, 4.0f, dst->movedir, dest );

	// the mapped spot can land inside geometry — the exit portal sits lower
	// than the entry point, or its oval is near an edge.  Box-test the exit
	// point directly, then slide along the plane (up, down, left, right)
	// until the box is free.
	{
		vec3_t	fB, rB, uB, probe, tryd;
		trace_t	tr2;
		int		k, d;
		qboolean placed = qfalse;

		G_CoopPortalFrame( dst->movedir, fB, rB, uB );
		for ( d = 0; d < 4 && !placed; d++ )
		{
			const vec3_t	*dir = ( d < 2 ) ? &uB : &rB;
			const float		step = ( d & 1 ) ? -8.0f : 8.0f;

			VectorCopy( dest, tryd );
			for ( k = 0; k < 12; k++ )
			{
				// stationary box-overlap test: nudge 1u out of the plane
				VectorMA( tryd, 1.0f, dst->movedir, probe );
				gi.trace( &tr2, tryd, e->mins, e->maxs, probe, e->s.number,
					MASK_PLAYERSOLID, (EG2_Collision)0, 0 );
				if ( !tr2.startsolid && !tr2.allsolid )
				{
					VectorCopy( tryd, dest );
					placed = qtrue;
					break;
				}
				VectorMA( tryd, step, *dir, tryd );
			}
		}
	}

	if ( e->client )
	{
		// hand-rolled TeleportPlayer: same steps minus G_KillBox — a buddy
		// parked at the exit shouldn't get telefragged — and keep the mapped
		// velocity instead of zeroing it
		AngleVectors( e->client->ps.viewangles, vaF, NULL, NULL );
		G_CoopPortalTransform( src->movedir, dst->movedir, vaF, vaF );
		vectoangles( vaF, exAng );
		exAng[2] = 0;

		gi.unlinkentity( e );
		VectorCopy( dest, e->client->ps.origin );
		e->client->ps.origin[2] += 1;
		VectorCopy( e->client->ps.origin, e->currentOrigin );
		e->client->ps.eFlags ^= EF_TELEPORT_BIT;
		SetClientViewAngle( e, exAng );
		VectorCopy( vOut, e->client->ps.velocity );
		PlayerStateToEntityState( &e->client->ps, &e->s );
		gi.linkentity( e );
	}
	else
	{
		gi.unlinkentity( e );
		VectorCopy( dest, e->currentOrigin );
		VectorCopy( dest, e->s.origin );
		VectorCopy( dest, e->s.pos.trBase );
		e->s.pos.trType = TR_GRAVITY;
		e->s.pos.trTime = level.time;
		VectorCopy( vOut, e->s.pos.trDelta );
		gi.linkentity( e );
	}
	coopPortalCd[e->s.number] = level.time + PORTAL_CD_MS;
	G_PlayEffect( "mp/spawn", dest );
}

/*
================
G_CoopGravPortalFrame

Per-frame: parked props follow their holder's crosshair, thrown props do
impact damage, and portals carry whoever steps through. Self-healing —
dead or disconnected holders just drop what they held, and orphan portals
are freed.
================
*/
void G_CoopGravPortalFrame( void )
{
	int	cn;

	for ( cn = 0; cn < MAX_CLIENTS; cn++ )
	{
		gentity_t	*player = &g_entities[cn];
		gentity_t	*held = gravHeldEnt[cn];
		const qboolean alive = (qboolean)( player->inuse && player->client &&
			player->client->pers.connected == CON_CONNECTED && player->health > 0 );

		if ( held && !held->inuse )
		{
			gravHeldEnt[cn] = NULL;
			held = NULL;
		}
		if ( held && !alive )
		{
			G_CoopGravReleaseDir( player, NULL );
			held = NULL;
		}
		if ( held )
		{
			vec3_t	eye, fwd, want;
			trace_t	tr;
			float	dist;

			VectorCopy( player->client->ps.origin, eye );
			eye[2] += player->client->ps.viewheight;
			AngleVectors( player->client->ps.viewangles, fwd, NULL, NULL );
			VectorMA( eye, GRAV_HOLD_DIST, fwd, want );
			// don't park it inside a wall — pull it toward the holder
			gi.trace( &tr, eye, held->mins, held->maxs, want, cn, MASK_SOLID, (EG2_Collision)0, 0 );
			dist = GRAV_HOLD_DIST * ( tr.entityNum == held->s.number ? 1.0f : Q_max( tr.fraction, 0.15f ) );
			VectorMA( eye, dist, fwd, want );

			G_SetOrigin( held, want );
			held->s.pos.trType = TR_STATIONARY;	// G_RunItem tries to fall again otherwise
			held->s.pos.trTime = level.time;
			VectorClear( held->s.pos.trDelta );
			held->s.groundEntityNum = cn;
			gi.linkentity( held );

			if ( level.time >= gravHeldFx[cn] )
			{
				gravHeldFx[cn] = level.time + 400;
				G_PlayEffect( "force/pushblur", held->currentOrigin );
			}
		}

		// disconnected owner: their portals go too
		if ( !player->client || player->client->pers.connected != CON_CONNECTED )
		{
			for ( int s = 0; s < 2; s++ )
			{
				gentity_t *p = coopPortal[cn][s];
				if ( p && p->inuse )
				{
					if ( p->owner && p->owner->inuse )
					{
						G_FreeEntity( p->owner );
					}
					G_FreeEntity( p );
				}
				coopPortal[cn][s] = NULL;
			}
		}
	}

	// thrown props: fast ones hurt the first thing they reach
	for ( int i = 0; i < gravThrownN; i++ )
	{
		gentity_t *e = gravThrown[i].ent;
		if ( !e || !e->inuse || level.time > gravThrown[i].until ||
			VectorLength( e->s.pos.trDelta ) < 400.0f )
		{
			gravThrown[i] = gravThrown[--gravThrownN];
			i--;
			continue;
		}
		gentity_t *list[MAX_GENTITIES];
		const int n = G_RadiusList( e->currentOrigin, 64.0f, e, qtrue, list );
		for ( int v = 0; v < n; v++ )
		{
			gentity_t *victim = list[v];
			if ( !victim || victim == e || victim == gravThrown[i].owner ||
				( !victim->client && !victim->NPC ) || victim->health <= 0 )
			{
				continue;
			}
			const float speed = VectorLength( e->s.pos.trDelta );
			int dmg = (int)( 20.0f + ( speed - 400.0f ) / 15.0f );
			if ( dmg > 150 ) dmg = 150;
			G_Damage( victim, e, gravThrown[i].owner, NULL, victim->currentOrigin,
				dmg, 0, MOD_CRUSH, 0 );
			gravThrown[i] = gravThrown[--gravThrownN];
			i--;
			break;
		}
	}

	// portals: approach the plane fast enough and you're through
	for ( cn = 0; cn < MAX_CLIENTS; cn++ )
	{
		gentity_t *A = coopPortal[cn][0], *B = coopPortal[cn][1];
		if ( !A || !A->inuse || !B || !B->inuse )
		{
			continue;
		}
		for ( int i = 0; i < globals.num_entities; i++ )
		{
			gentity_t	*e = &g_entities[i];
			vec3_t		vel;

			if ( !e->inuse || e == A || e == B || e == A->owner || e == B->owner )
			{
				continue;
			}
			if ( coopPortalCd[i] > level.time )
			{
				continue;
			}
			if ( e->client )
			{
				VectorCopy( e->client->ps.velocity, vel );
			}
			else if ( e->item || e->s.eType == ET_MISSILE ||
				( e->classname && !Q_stricmp( e->classname, "misc_model_breakable" ) ) )
			{
				VectorCopy( e->s.pos.trDelta, vel );
			}
			else
			{
				continue;
			}

			for ( int s = 0; s < 2; s++ )
			{
				gentity_t	*src = s ? B : A, *dst = s ? A : B;
				vec3_t		d, radial, c;
				float		dn, vn, best;
				int			k;

				vn = DotProduct( vel, src->movedir );
				if ( vn > -60.0f )
				{
					continue;	// must actually be moving into it
				}

				// the oval is ~112 units tall; a single point test misses bodies
				// whose origin is far from it (wall portal at eye height vs an
				// origin near the feet).  Sample feet/middle/head — any part of
				// the body inside the oval counts as stepping through.
				best = 1e9f;
				for ( k = 0; k < 3; k++ )
				{
					VectorCopy( e->currentOrigin, c );
					c[2] += ( k == 0 ) ? e->mins[2] :
						( k == 1 ) ? ( e->mins[2] + e->maxs[2] ) * 0.5f : e->maxs[2];
					VectorSubtract( c, src->currentOrigin, d );
					dn = DotProduct( d, src->movedir );
					if ( dn < -8.0f || dn > PORTAL_ENTER_D )
					{
						continue;
					}
					VectorMA( d, -dn, src->movedir, radial );
					best = Q_min( best, VectorLength( radial ) );
				}
				if ( best > PORTAL_ENTER_R )
				{
					continue;
				}
				G_CoopPortalTravel( e, src, dst, vel );
				break;
			}
		}
	}
}

/*
================
G_CoopGravPortalInit

Per-map reset — entity pointers die with the level, only the tables need
clearing. Called beside the other coop state resets.
================
*/
void G_CoopGravPortalInit( void )
{
	memset( gravHeldEnt, 0, sizeof( gravHeldEnt ) );
	memset( gravThrown, 0, sizeof( gravThrown ) );
	gravThrownN = 0;
	memset( coopPortal, 0, sizeof( coopPortal ) );
	memset( coopPortalNext, 0, sizeof( coopPortalNext ) );
	memset( coopPortalCd, 0, sizeof( coopPortalCd ) );
	memset( gravHeldFx, 0, sizeof( gravHeldFx ) );
}
