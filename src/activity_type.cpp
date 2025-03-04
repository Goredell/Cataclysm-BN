#include "activity_type.h"

#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>

#include "activity_actor.h"
#include "activity_handlers.h"
#include "assign.h"
#include "debug.h"
#include "enum_conversions.h"
#include "json.h"
#include "sounds.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"

// activity_type functions
static std::map< activity_id, activity_type > activity_type_all;

/** @relates string_id */
template<>
const activity_type &string_id<activity_type>::obj() const
{
    const auto found = activity_type_all.find( *this );
    if( found == activity_type_all.end() ) {
        debugmsg( "Tried to get invalid activity_type: %s", c_str() );
        static const activity_type null_activity_type {};
        return null_activity_type;
    }
    return found->second;
}

/** @relates string_id */
template<>
bool string_id<activity_type>::is_valid() const
{
    const auto found = activity_type_all.find( *this );
    return found != activity_type_all.end();
}


void activity_type::load( const JsonObject &jo )
{
    activity_type result;

    result.id_ = activity_id( jo.get_string( "id" ) );
    assign( jo, "rooted", result.rooted_, true );
    assign( jo, "verb", result.verb_, true );
    assign( jo, "suspendable", result.suspendable_, true );
    assign( jo, "no_resume", result.no_resume_, true );
    assign( jo, "special", result.special_, false );
    assign( jo, "multi_activity", result.multi_activity_, false );
    assign( jo, "refuel_fires", result.refuel_fires, false );
    assign( jo, "auto_needs", result.auto_needs, false );
    assign( jo, "morale_blocked", result.morale_blocked_, false );
    assign( jo, "verbose_tooltip", result.verbose_tooltip_, false );
    if( jo.has_member( "complex_moves" ) ) {
        result.complex_moves_ = true;
        auto c_moves = jo.get_object( "complex_moves" );
        result.assistable_ = c_moves.get_bool( "assistable", false );
        result.bench_affected_ = c_moves.get_bool( "bench", false );
        result.light_affected_ = c_moves.get_bool( "light", false );
        result.speed_affected_ = c_moves.get_bool( "speed", false );
        result.morale_affected_ = c_moves.get_bool( "morale", false );

        c_moves.allow_omitted_members();
        if( c_moves.has_bool( "skills" ) ) {
            assign( jo, "skills", result.skill_affected_, false );
        } else if( c_moves.has_array( "skills" ) ) {
            result.skill_affected_ = true;
            for( JsonArray skillobj : c_moves.get_array( "skills" ) ) {
                std::string skill_s = skillobj.get_string( 0 );
                auto skill = skill_id( skill_s );
                int mod = skillobj.get_int( 1 );
                result.skills[skill] = mod;
            }
        }

        if( c_moves.has_bool( "qualities" ) ) {
            assign( jo, "qualities", result.tools_affected_, false );
        } else if( c_moves.has_array( "qualities" ) ) {
            result.tools_affected_ = true;
            for( JsonArray q_obj : c_moves.get_array( "qualities" ) ) {
                std::string quality_s = q_obj.get_string( 0 );
                auto quality = quality_id( quality_s );
                int mod = q_obj.get_int( 1 );
                result.qualities[quality] = mod;
            }
        }

        if( c_moves.has_bool( "stats" ) ) {
            assign( jo, "stats", result.stats_affected_, false );
        } else if( c_moves.has_array( "stats" ) ) {
            result.stats_affected_ = true;
            for( JsonArray stat_obj : c_moves.get_array( "stats" ) ) {
                int mod = stat_obj.get_int( 1 );
                auto stat = io::string_to_enum_fallback( stat_obj.get_string( 0 ), character_stat::DUMMY_STAT );
                if( stat == character_stat::DUMMY_STAT ) {
                    debugmsg( "Unknown quality id %s", stat_obj.get_string( 0 ) );
                } else {
                    result.stats[stat] = mod;
                }

            }
        }
    }

    if( activity_type_all.find( result.id_ ) != activity_type_all.end() ) {
        debugmsg( "Redefinition of %s", result.id_.c_str() );
    } else {
        activity_type_all.insert( { result.id_, result } );
    }
}

void activity_type::check_consistency()
{
    for( const auto &pair : activity_type_all ) {
        if( pair.second.verb_.empty() ) {
            debugmsg( "%s doesn't have a verb", pair.first.c_str() );
        }
        const bool has_actor = activity_actors::deserialize_functions.find( pair.second.id_ ) !=
                               activity_actors::deserialize_functions.end();
        const bool has_turn_func = activity_handlers::do_turn_functions.find( pair.second.id_ ) !=
                                   activity_handlers::do_turn_functions.end();

        if( pair.second.special_ && !( has_turn_func || has_actor ) ) {
            debugmsg( "%s needs a do_turn function or activity actor if it expects a special behaviour.",
                      pair.second.id_.c_str() );
        }
        for( auto &skill : pair.second.skills )
            if( !skill.first.is_valid() ) {
                debugmsg( "Unknown skill id %s", skill.first.str() );
            }

        for( auto &quality : pair.second.qualities )
            if( !quality.first.is_valid() ) {
                debugmsg( "Unknown quality id %s", quality.first.str() );
            }
    }

    for( const auto &pair : activity_handlers::do_turn_functions ) {
        if( activity_type_all.find( pair.first ) == activity_type_all.end() ) {
            debugmsg( "The do_turn function %s doesn't correspond to a valid activity_type.",
                      pair.first.c_str() );
        }
    }

    for( const auto &pair : activity_handlers::finish_functions ) {
        if( activity_type_all.find( pair.first ) == activity_type_all.end() ) {
            debugmsg( "The finish_function %s doesn't correspond to a valid activity_type",
                      pair.first.c_str() );
        }
    }
}

void activity_type::call_do_turn( player_activity *act, player *p ) const
{
    const auto &pair = activity_handlers::do_turn_functions.find( id_ );
    if( pair != activity_handlers::do_turn_functions.end() ) {
        pair->second( act, p );
    }
}

bool activity_type::call_finish( player_activity *act, player *p ) const
{
    const auto &pair = activity_handlers::finish_functions.find( id_ );
    if( pair != activity_handlers::finish_functions.end() ) {
        pair->second( act, p );
        // kill activity sounds at finish
        sfx::end_activity_sounds();
        return true;
    }
    return false;
}

void activity_type::reset()
{
    activity_type_all.clear();
}

std::string activity_type::stop_phrase() const
{
    return string_format( _( "Stop %s?" ), verb_ );
}
