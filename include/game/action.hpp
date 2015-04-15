// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2015 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#ifndef ANTARES_GAME_ACTION_HPP_
#define ANTARES_GAME_ACTION_HPP_

#include "data/space-object.hpp"

namespace antares {

// Returns true iff {in,ex}clusive_filter() allows `action` to run over
// `target`. The baseObject version uses the default attributes of the
// object, and the spaceObject version uses the actual attributes in
// effect (e.g. kStaticDestination or kOnAutopilot).
bool action_filter_applies_to(const objectActionType& action, const baseObjectType& target);
bool action_filter_applies_to(const objectActionType& action, const spaceObjectType& target);

void execute_actions(
        int32_t whichAction, int32_t actionNum,
        spaceObjectType *sObject, spaceObjectType *dObject, Point* offset,
        bool allowDelay);

void reset_action_queue();
void execute_action_queue(int32_t);

}  // namespace antares

#endif // ANTARES_GAME_ACTION_HPP_