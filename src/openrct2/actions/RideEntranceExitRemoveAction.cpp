/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "RideEntranceExitRemoveAction.h"

#include "../ride/Ride.h"
#include "../ride/Station.h"
#include "../world/Entrance.h"
#include "../world/TileElementsView.h"

using namespace OpenRCT2;

RideEntranceExitRemoveAction::RideEntranceExitRemoveAction(
    const CoordsXY& loc, ride_id_t rideIndex, StationIndex stationNum, bool isExit)
    : _loc(loc)
    , _rideIndex(rideIndex)
    , _stationNum(stationNum)
    , _isExit(isExit)
{
}

void RideEntranceExitRemoveAction::AcceptParameters(GameActionParameterVisitor& visitor)
{
    visitor.Visit(_loc);
    visitor.Visit("ride", _rideIndex);
    visitor.Visit("station", _stationNum);
    visitor.Visit("isExit", _isExit);
}

uint16_t RideEntranceExitRemoveAction::GetActionFlags() const
{
    return GameAction::GetActionFlags();
}

void RideEntranceExitRemoveAction::Serialise(DataSerialiser& stream)
{
    GameAction::Serialise(stream);

    stream << DS_TAG(_loc) << DS_TAG(_rideIndex) << DS_TAG(_stationNum) << DS_TAG(_isExit);
}

static TileElement* FindEntranceElement(
    const CoordsXY& loc, ride_id_t rideIndex, int32_t stationNum, int32_t entranceType, uint32_t flags)
{
    const bool isGhost = flags & GAME_COMMAND_FLAG_GHOST;
    for (auto* entranceElement : TileElementsView<EntranceElement>(loc))
    {
        // If we are removing ghost elements
        if (isGhost && entranceElement->IsGhost() == false)
            continue;

        if (entranceElement->GetRideIndex() != rideIndex)
            continue;

        if (entranceElement->GetStationIndex() != stationNum)
            continue;

        if (entranceElement->GetEntranceType() != entranceType)
            continue;

        return entranceElement->as<TileElement>();
    }
    return nullptr;
}

GameActions::Result RideEntranceExitRemoveAction::Query() const
{
    auto ride = get_ride(_rideIndex);
    if (ride == nullptr)
    {
        log_warning("Invalid ride id %d for entrance/exit removal", EnumValue(_rideIndex));
        return GameActions::Result(GameActions::Status::InvalidParameters, STR_NONE, STR_NONE);
    }

    if (ride->status != RideStatus::Closed && ride->status != RideStatus::Simulating)
    {
        return GameActions::Result(GameActions::Status::InvalidParameters, STR_MUST_BE_CLOSED_FIRST, STR_NONE);
    }

    if (ride->lifecycle_flags & RIDE_LIFECYCLE_INDESTRUCTIBLE_TRACK)
    {
        return GameActions::Result(GameActions::Status::InvalidParameters, STR_NOT_ALLOWED_TO_MODIFY_STATION, STR_NONE);
    }

    if (!LocationValid(_loc))
    {
        return GameActions::Result(GameActions::Status::InvalidParameters, STR_LAND_NOT_OWNED_BY_PARK, STR_NONE);
    }

    auto* entranceElement = FindEntranceElement(
        _loc, _rideIndex, _stationNum, _isExit ? ENTRANCE_TYPE_RIDE_EXIT : ENTRANCE_TYPE_RIDE_ENTRANCE, GetFlags());

    if (entranceElement == nullptr)
    {
        log_warning(
            "Track Element not found. x = %d, y = %d, ride = %d, station = %d", _loc.x, _loc.y, EnumValue(_rideIndex),
            _stationNum);
        return GameActions::Result(GameActions::Status::InvalidParameters, STR_NONE, STR_NONE);
    }

    return GameActions::Result();
}

GameActions::Result RideEntranceExitRemoveAction::Execute() const
{
    auto ride = get_ride(_rideIndex);
    if (ride == nullptr)
    {
        log_warning("Invalid ride id %d for entrance/exit removal", EnumValue(_rideIndex));
        return GameActions::Result(GameActions::Status::InvalidParameters, STR_NONE, STR_NONE);
    }

    const bool isGhost = GetFlags() & GAME_COMMAND_FLAG_GHOST;
    if (!isGhost)
    {
        ride_clear_for_construction(ride);
        ride->RemovePeeps();
        invalidate_test_results(ride);
    }

    auto* entranceElement = FindEntranceElement(
        _loc, _rideIndex, _stationNum, _isExit ? ENTRANCE_TYPE_RIDE_EXIT : ENTRANCE_TYPE_RIDE_ENTRANCE, GetFlags());

    if (entranceElement == nullptr)
    {
        log_warning(
            "Track Element not found. x = %d, y = %d, ride = %d, station = %d", _loc.x, _loc.y, EnumValue(_rideIndex),
            _stationNum);
        return GameActions::Result(GameActions::Status::InvalidParameters, STR_NONE, STR_NONE);
    }

    auto res = GameActions::Result();
    res.Position.x = _loc.x + 16;
    res.Position.y = _loc.y + 16;
    res.Position.z = tile_element_height(res.Position);

    footpath_queue_chain_reset();
    maze_entrance_hedge_replacement({ _loc, entranceElement });
    footpath_remove_edges_at(_loc, entranceElement);

    tile_element_remove(entranceElement);

    if (_isExit)
    {
        ride_clear_exit_location(ride, _stationNum);
    }
    else
    {
        ride_clear_entrance_location(ride, _stationNum);
    }

    footpath_update_queue_chains();

    map_invalidate_tile_full(_loc);
    return res;
}
