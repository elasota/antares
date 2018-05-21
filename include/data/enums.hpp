// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2018 The Antares Authors
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

#ifndef ANTARES_DATA_ENUMS_HPP_
#define ANTARES_DATA_ENUMS_HPP_

namespace antares {

enum class ActionType {
    AGE,
    ASSUME,
    CAP_SPEED,
    CAPTURE,
    CLOAK,
    CONDITION,
    CREATE,
    DISABLE,
    ENERGIZE,
    EQUIP,
    FIRE,
    FLASH,
    HEAL,
    HOLD,
    KEY,
    KILL,
    LAND,
    MESSAGE,
    MORPH,
    MOVE,
    OCCUPY,
    ORDER,
    PAY,
    PUSH,
    REVEAL,
    SCORE,
    SELECT,
    PLAY,
    SPARK,
    SPIN,
    THRUST,
    WARP,
    WIN,
    ZOOM,
};

enum class ConditionType {
    AUTOPILOT,
    BUILDING,
    COMPUTER,
    COUNTER,
    DESTROYED,
    DISTANCE,
    HEALTH,
    MESSAGE,
    ORDERED,
    OWNER,
    SHIPS,
    SPEED,
    SUBJECT,
    TIME,
    ZOOM,
};

enum class AnimationDirection {
    NONE   = 0,   // 0
    PLUS   = +1,  // +
    MINUS  = -1,  // -
    RANDOM = 2,   // ?
};

// Hues, combined with a shade to get a color.
enum class Hue {
    GRAY        = 0,
    ORANGE      = 1,
    YELLOW      = 2,
    BLUE        = 3,
    GREEN       = 4,
    PURPLE      = 5,
    INDIGO      = 6,
    SALMON      = 7,
    GOLD        = 8,
    AQUA        = 9,
    PINK        = 10,
    PALE_GREEN  = 11,
    PALE_PURPLE = 12,
    SKY_BLUE    = 13,
    TAN         = 14,
    RED         = 15,
};

enum class KillKind {
    // Removes the focus without any further fanfare.
    NONE = 0,

    // Removes the subject without any further fanfare.
    // Essentially, this is NONE, but always reflexive.
    EXPIRE = 1,

    // Removes the subject and executes its destroy action.
    DESTROY = 2,
};

enum class MoveOrigin {
    LEVEL,    // absolute coordinates, in level’s rotated frame of reference
    SUBJECT,  // relative to subject
    OBJECT,   // relative to object
};

enum class ConditionOp { EQ, NE, LT, GT, LE, GE };

enum class IconShape {
    SQUARE,
    TRIANGLE,
    DIAMOND,
    PLUS,
};

enum class InterfaceStyle { LARGE, SMALL };

enum class LevelType { SOLO, NET, DEMO };

// Restricts actions based on the owners of the subject and object.
enum class Owner {
    ANY       = 0,   // Always execute.
    SAME      = 1,   // Execute only if match.
    DIFFERENT = -1,  // Execute only if no match.
};

enum class PlayerType { HUMAN, CPU };

enum class PushKind {
    STOP,        // set focus’s velocity to 0
    COLLIDE,     // impart velocity from subject like a collision (capped)
    DECELERATE,  // decrease focus’s velocity (capped)
    SET,         // set focus’s velocity to value in subject’s direction
    BOOST,       // add to focus’s velocity in subject’s direction
    CRUISE,      // set focus’s velocity in focus’s direction
};

// Different screens on the minicomputer.
enum class Screen {
    MAIN    = 1,
    BUILD   = 2,
    SPECIAL = 3,
    MESSAGE = 4,
    STATUS  = 5,
};

enum class SubjectValue { CONTROL, TARGET, PLAYER };

// The three weapons a ship may have.
enum class Weapon {
    PULSE   = 0,  // Also known as “Weapon 1”
    BEAM    = 1,  // Also known as “Weapon 2”
    SPECIAL = 2,
};

// Zoom level in the main game screen.
enum class Zoom {
    DOUBLE    = 0,
    ACTUAL    = 1,
    HALF      = 2,
    QUARTER   = 3,
    SIXTEENTH = 4,
    FOE       = 5,
    OBJECT    = 6,
    ALL       = 7,
};

}  // namespace antares

#endif  // ANTARES_DATA_ENUMS_HPP_
