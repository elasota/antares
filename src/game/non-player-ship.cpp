// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2012 The Antares Authors
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

#include "game/non-player-ship.hpp"

#include "config/keys.hpp"
#include "data/string-list.hpp"
#include "drawing/color.hpp"
#include "game/action.hpp"
#include "game/admiral.hpp"
#include "game/globals.hpp"
#include "game/messages.hpp"
#include "game/motion.hpp"
#include "game/player-ship.hpp"
#include "game/scenario-maker.hpp"
#include "game/space-object.hpp"
#include "game/starfield.hpp"
#include "math/macros.hpp"
#include "math/random.hpp"
#include "math/rotation.hpp"
#include "math/special.hpp"
#include "math/units.hpp"
#include "sound/fx.hpp"
#include "video/transitions.hpp"

using sfz::Exception;
using sfz::StringSlice;

namespace antares {

const int32_t kDirectionError       = 5;        // how picky in degrees we are about angle
const int32_t kShootAngle           = 15;       // how picky we are about shooting in degrees
const int32_t kParanoiaAngle        = 30;       // angle of terror
const int32_t kEvadeAngle           = 30;       // we'd like to turn this far away

const uint32_t kMotionMargin        = 5000;    // margin of change in distance before we care
const uint32_t kLandingDistance     = 1000;
const uint32_t kWarpInDistance      = 16777216;

const int8_t kCloserThanClosest     = 0x01;
const int8_t kFartherThanFarther    = 0x02;

const int32_t kRechargeSpeed        = 4;
const int32_t kHealthRatio          = 5;
const int32_t kWeaponRatio          = 2;
const int32_t kEnergyChunk          = kHealthRatio + (kWeaponRatio * 3);
const int32_t kWarpInEnergyFactor   = 3;

const int32_t kDefaultTurnRate      = 0x00000200;

enum {
    kFriendlyColor  = GREEN,
    kHostileColor   = RED,
    kNeutralColor   = SKY_BLUE,
};

uint32_t ThinkObjectNormalPresence( spaceObjectType *, baseObjectType *, int32_t);
uint32_t ThinkObjectWarpingPresence( spaceObjectType *);
uint32_t ThinkObjectWarpInPresence( spaceObjectType *);
uint32_t ThinkObjectWarpOutPresence( spaceObjectType *, baseObjectType *);
uint32_t ThinkObjectLandingPresence( spaceObjectType *);
void ThinkObjectGetCoordVector( spaceObjectType *, coordPointType *, uint32_t *, int16_t *);
void ThinkObjectGetCoordDistance( spaceObjectType *, coordPointType *, uint32_t *);
void ThinkObjectResolveDestination( spaceObjectType *, coordPointType *, spaceObjectType **);
bool ThinkObjectResolveTarget( spaceObjectType *, coordPointType *, uint32_t *, spaceObjectType **);
uint32_t ThinkObjectEngageTarget( spaceObjectType *, spaceObjectType *, uint32_t, int16_t *, int32_t);

spaceObjectType *HackNewNonplayerShip( int32_t owner, int16_t type, Rect *bounds)

{
#pragma unused( owner, type, bounds)
    return( NULL);
}

static void recharge(spaceObjectType* anObject) {
    auto baseObject = anObject->baseType;
    if ((anObject->energy < (baseObject->energy - kEnergyChunk))
            && (anObject->battery > kEnergyChunk)) {
        anObject->battery -= kEnergyChunk;
        anObject->energy += kEnergyChunk;
    }

    if ((anObject->health < (baseObject->health / 2))
            && (anObject->energy > kHealthRatio)) {
        anObject->health++;
        anObject->energy -= kHealthRatio;
    }

    for (auto* weapon: {&anObject->pulse, &anObject->beam, &anObject->special}) {
        if (weapon->type != kNoWeapon) {
            if ((weapon->ammo < (weapon->base->frame.weapon.ammo >> 1))
                    && (anObject->energy >= kWeaponRatio)) {
                weapon->charge++;
                anObject->energy -= kWeaponRatio;

                if ((weapon->base->frame.weapon.restockCost >= 0)
                        && (weapon->charge >= weapon->base->frame.weapon.restockCost)) {
                    weapon->charge -= weapon->base->frame.weapon.restockCost;
                    weapon->ammo++;
                }
            }
        }
    }
}

static void tick_weapon(
        spaceObjectType* subject, spaceObjectType* target, int32_t timePass,
        uint32_t key, const baseObjectType::Weapon& base_weapon, spaceObjectType::Weapon& weapon) {
    if (weapon.time > 0) {
        weapon.time -= timePass;
    }
    if (!(subject->keysDown & key)
            || (weapon.time > 0)
            || (weapon.type == kNoWeapon)) {
        return;
    }

    baseObjectType* baseObject = subject->baseType;
    baseObjectType* weaponObject = weapon.base;
    if ((subject->energy < weaponObject->frame.weapon.energyCost)
            || ((weaponObject->frame.weapon.ammo > 0) && (weapon.ammo <= 0))) {
        return;
    }
    if ((&weapon != &subject->special)
            && (subject->cloakState > 0)) {
        AlterObjectCloakState(subject, false);
    }
    subject->energy -= weaponObject->frame.weapon.energyCost;
    weapon.position++;
    if (weapon.position >= base_weapon.positionNum) {
        weapon.position = 0;
    }

    int16_t angle = subject->direction;
    mAddAngle(angle, -90);
    Fixed fcos, fsin;
    GetRotPoint(&fcos, &fsin, angle);
    fcos = -fcos;
    fsin = -fsin;

    Point offset;
    Point* at = nullptr;
    if (&weapon != &subject->special) {
        offset.h = mMultiplyFixed(base_weapon.position[weapon.position].h, fcos);
        offset.h -= mMultiplyFixed(base_weapon.position[weapon.position].v, fsin);
        offset.v = mMultiplyFixed(base_weapon.position[weapon.position].h, fsin);
        offset.v += mMultiplyFixed(base_weapon.position[weapon.position].v, fcos);
        offset.h = mFixedToLong(offset.h);
        offset.v = mFixedToLong(offset.v);
        at = &offset;
    }

    weapon.time = weaponObject->frame.weapon.fireTime;
    if (weaponObject->frame.weapon.ammo > 0) {
        weapon.ammo--;
    }
    execute_actions(
            weaponObject->activateAction,
            weaponObject->activateActionNum,
            subject, target, at, true);
}

static void tick_pulse(spaceObjectType* subject, spaceObjectType* target, int32_t timePass) {
    tick_weapon(subject, target, timePass, kOneKey, subject->baseType->pulse, subject->pulse);
}

static void tick_beam(spaceObjectType* subject, spaceObjectType* target, int32_t timePass) {
    tick_weapon(subject, target, timePass, kTwoKey, subject->baseType->beam, subject->beam);
}

static void tick_special(spaceObjectType* subject, spaceObjectType* target, int32_t timePass) {
    tick_weapon(subject, target, timePass, kEnterKey, subject->baseType->special, subject->special);
}

#ifdef kUseOldThinking
#else   // if NOT kUseOldThinking
void NonplayerShipThink(int32_t timePass)
{
    admiralType     *anAdmiral;
    spaceObjectType *targetObject;
    baseObjectType  *baseObject, *weaponObject;
    Point           offset;
    int32_t         count, difference;
    uint32_t        keysDown;
    int16_t         h;
    Fixed           fcos, fsin;
    RgbColor        friendSick, foeSick, neutralSick;
    uint32_t        sickCount = usecs_to_ticks(globals()->gGameTime) / 9;

    globals()->gSynchValue = gRandomSeed.seed;
    sickCount &= 0x00000003;
    if (sickCount == 0) {
        friendSick = GetRGBTranslateColorShade(kFriendlyColor, MEDIUM);
        foeSick = GetRGBTranslateColorShade(kHostileColor, MEDIUM);
        neutralSick = GetRGBTranslateColorShade(kNeutralColor, MEDIUM);
    } else if (sickCount == 1) {
        friendSick = GetRGBTranslateColorShade(kFriendlyColor, DARK);
        foeSick = GetRGBTranslateColorShade(kHostileColor, DARK);
        neutralSick = GetRGBTranslateColorShade(kNeutralColor, DARK);
    } else if (sickCount == 2) {
        friendSick = GetRGBTranslateColorShade(kFriendlyColor, DARKER);
        foeSick = GetRGBTranslateColorShade(kHostileColor, DARKER);
        neutralSick = GetRGBTranslateColorShade(kNeutralColor, DARKER);
    } else if (sickCount == 3) {
        friendSick = GetRGBTranslateColorShade(kFriendlyColor, DARKEST);
        foeSick = GetRGBTranslateColorShade(kHostileColor, DARKER-1);
        neutralSick = GetRGBTranslateColorShade(kNeutralColor, DARKEST);
    } else {
        throw Exception("invalid value of sickCount");
    }

    for (int32_t count = 0; count < kMaxPlayerNum; count++) {
        mGetAdmiralPtr(count)->shipsLeft = 0;
    }

    // it probably doesn't matter what order we do this in, but we'll do
    // it in the "ideal" order anyway
    for (auto anObject = gRootObject; anObject; anObject = anObject->nextObject) {
        if (!anObject->active) {
            continue;
        }

        globals()->gSynchValue += anObject->location.h;
        globals()->gSynchValue += anObject->location.v;

        keysDown = anObject->keysDown & kSpecialKeyMask;

        // strobe its symbol if it's not feeling well
        if (anObject->sprite) {
            if ((anObject->health > 0) && (anObject->health <= (anObject->baseType->health >> 2))) {
                if (anObject->owner == globals()->gPlayerAdmiralNumber) {
                    anObject->sprite->tinyColor = friendSick;
                } else if (anObject->owner < 0) {
                    anObject->sprite->tinyColor = neutralSick;
                } else {
                    anObject->sprite->tinyColor = foeSick;
                }
            } else {
                anObject->sprite->tinyColor = anObject->tinyColor;
            }
        }

        // if the object can think, or is human controlled
        if (!(anObject->attributes & (kCanThink | kRemoteOrHuman))) {
            continue;
        }

        // get the object's base object
        baseObject = anObject->baseType;
        anObject->targetAngle = anObject->directionGoal = anObject->direction;

        // incremenent its admiral's # of ships
        if (anObject->owner > kNoOwner) {
            mGetAdmiralPtr(anObject->owner)->shipsLeft++;
        }

        switch (anObject->presenceState) {
          case kNormalPresence:
            keysDown = ThinkObjectNormalPresence(anObject, baseObject, timePass);
            break;

          case kWarpingPresence:
            keysDown = ThinkObjectWarpingPresence(anObject);
            break;

          case kWarpInPresence:
            keysDown = ThinkObjectWarpInPresence(anObject);
            break;

          case kWarpOutPresence:
            keysDown = ThinkObjectWarpOutPresence(anObject, baseObject);
            break;

          case kLandingPresence:
            keysDown = ThinkObjectLandingPresence(anObject);
            break;

          case kTakeoffPresence:
            break;
        }

        if (!(anObject->attributes & kRemoteOrHuman)
                || (anObject->attributes & kOnAutoPilot)) {
            if (anObject->attributes & kHasDirectionGoal) {
                if (anObject->attributes & kShapeFromDirection) {
                    if ((anObject->attributes & kIsGuided)
                            && (anObject->targetObjectNumber != kNoShip)) {
                        difference = anObject->targetAngle - anObject->direction;
                        if ((difference < -60) || (difference > 60)) {
                            anObject->targetObjectNumber = kNoShip;
                            anObject->targetObjectID = kNoShip;
                            anObject->directionGoal = anObject->direction;
                        }
                    }

                    offset.h = mAngleDifference(anObject->directionGoal, anObject->direction);
                    offset.v = mFixedToLong(baseObject->frame.rotation.maxTurnRate << 1);
                    difference = ABS(offset.h);
                } else {
                    offset.h = mAngleDifference(anObject->directionGoal, anObject->direction);
                    offset.v = mFixedToLong(kDefaultTurnRate << 1);
                    difference = ABS(offset.h);
                }
                if (difference > offset.v) {
                    if (offset.h < 0) {
                        keysDown |= kRightKey;
                    } else if (offset.h > 0) {
                        keysDown |= kLeftKey;
                    }
                }
            }
            // and here?
            if (!(anObject->keysDown & kManualOverrideFlag)) {
                if (anObject->closestDistance < kEngageRange) {
                    // why do we only do this randomly when closest is within engagerange?
                    // to simulate the innaccuracy of battle
                    // (to keep things from wiggling, really)
                    if  (anObject->randomSeed.next(baseObject->skillDen) < baseObject->skillNum) {
                        anObject->keysDown &= ~kMotionKeyMask;
                        anObject->keysDown |= keysDown & kMotionKeyMask;
                    }
                    if (anObject->randomSeed.next(3) == 1) {
                        anObject->keysDown &= ~kWeaponKeyMask;
                        anObject->keysDown |= keysDown & kWeaponKeyMask;
                    }
                    anObject->keysDown &= ~kMiscKeyMask;
                    anObject->keysDown |= keysDown & kMiscKeyMask;
                } else {
                    anObject->keysDown = (anObject->keysDown & kSpecialKeyMask) | keysDown;
                }
            } else {
                anObject->keysDown &= ~kManualOverrideFlag;
            }
        }

        // Take care of any "keys" being pressed
        if (anObject->keysDown & kAdoptTargetKey) {
            SetObjectDestination(anObject, NULL);
        }
        if (anObject->keysDown & kAutoPilotKey) {
            TogglePlayerAutoPilot(anObject);
        }
        if (anObject->keysDown & kGiveCommandKey) {
            PlayerShipGiveCommand(anObject->owner);
        }
        anObject->keysDown &= ~kSpecialKeyMask;

        if (anObject->offlineTime > 0) {
            if (anObject->randomSeed.next(anObject->offlineTime) > 5) {
                anObject->keysDown = 0;
            }
            anObject->offlineTime--;
        }

        if ((anObject->attributes & kRemoteOrHuman)
                && (!(anObject->attributes & kCanThink))
                && (anObject->age < 120)) {
            PlayerShipBodyExpire(anObject, true);
        }

        if ((anObject->attributes & kHasDirectionGoal)
                && (anObject->offlineTime <= 0)) {
            // design flaw: can't have turn rate unless shapefromdirection
            if (anObject->attributes & kShapeFromDirection) {
                if (anObject->keysDown & kLeftKey) {
                    anObject->turnVelocity = -baseObject->frame.rotation.maxTurnRate;
                } else if (anObject->keysDown & kRightKey) {
                    anObject->turnVelocity = baseObject->frame.rotation.maxTurnRate;
                } else {
                    anObject->turnVelocity = 0;
                }
            } else {
                if (anObject->keysDown & kLeftKey) {
                    anObject->turnVelocity = -kDefaultTurnRate;
                } else if (anObject->keysDown & kRightKey) {
                    anObject->turnVelocity = kDefaultTurnRate;
                } else {
                    anObject->turnVelocity = 0;
                }
            }
        }

        if (anObject->keysDown & kUpKey) {
            if ((anObject->presenceState != kWarpInPresence)
                    && (anObject->presenceState != kWarpingPresence)
                    && (anObject->presenceState != kWarpOutPresence)) {
                anObject->thrust = baseObject->maxThrust;
            }
        } else if (anObject->keysDown & kDownKey) {
            if ((anObject->presenceState != kWarpInPresence)
                    && (anObject->presenceState != kWarpingPresence)
                    && (anObject->presenceState != kWarpOutPresence)) {
                anObject->thrust = -baseObject->maxThrust;
            }
            anObject->thrust = -baseObject->maxThrust;
        } else {
            anObject->thrust = 0;
        }

        if (anObject->rechargeTime < kRechargeSpeed) {
            anObject->rechargeTime++;
        } else {
            anObject->rechargeTime = 0;

            if (anObject->presenceState == kWarpingPresence) {
                anObject->energy -= 1;
                anObject->warpEnergyCollected += 1;
                if (anObject->energy <= 0) {
                    anObject->energy = 0;
                }
            }

            if (anObject->presenceState == kNormalPresence) {
                recharge(anObject);
            }
        }

        // targetObject is set for all three weapons -- do not change
        if (anObject->targetObjectNumber >= 0) {
            targetObject = mGetSpaceObjectPtr(anObject->targetObjectNumber);
        } else {
            targetObject = nullptr;
        }

        tick_pulse(anObject, targetObject, timePass);
        tick_beam(anObject, targetObject, timePass);
        tick_special(anObject, targetObject, timePass);

        if ((anObject->keysDown & kWarpKey)
                && (baseObject->warpSpeed > 0)
                && (anObject->energy > 0)) {
            if ((anObject->presenceState == kWarpingPresence)
                    || (anObject->presenceState == kWarpOutPresence)) {
                anObject->thrust = mMultiplyFixed(baseObject->maxThrust, anObject->presenceData);
            } else if ((anObject->presenceState == kNormalPresence)
                    && (anObject->energy > (anObject->baseType->energy >> kWarpInEnergyFactor))) {
                anObject->presenceState = kWarpInPresence;
                anObject->presenceData = 0;
            }
        } else {
            if (anObject->presenceState == kWarpInPresence) {
                anObject->presenceState = kNormalPresence;
            } else if (anObject->presenceState == kWarpingPresence) {
                anObject->presenceState = kWarpOutPresence;
            } else if (anObject->presenceState == kWarpOutPresence) {
                anObject->thrust = mMultiplyFixed(baseObject->maxThrust, anObject->presenceData);
            }
        }

    }
}
#endif  // kUseOldThinking

uint32_t ThinkObjectNormalPresence( spaceObjectType *anObject, baseObjectType *baseObject, int32_t timePass)
{
    uint32_t        keysDown = anObject->keysDown & kSpecialKeyMask, distance, dcalc;
    spaceObjectType *targetObject;
    baseObjectType  *bestWeapon, *weaponObject;
    coordPointType  dest;
    int32_t         difference;
    Fixed           slope;
    int16_t         angle, theta, beta;
    Fixed           calcv, fdist;
    Point           offset;

    if ((!(anObject->attributes & kRemoteOrHuman))
        || ( anObject->attributes & kOnAutoPilot))
    {
        // set all keys off
        keysDown &= kSpecialKeyMask;
        // if target object exists and is within engage range

        ThinkObjectResolveTarget( anObject, &dest, &distance, &targetObject);

///--->>> BEGIN TARGETING <<<---///
        if (    ( anObject->targetObjectNumber != kNoShip) &&
                (
                    ( anObject->attributes & kIsGuided) ||
                    (
                        ( anObject->attributes & kCanEngage) &&
                        ( !(anObject->attributes &
                            kRemoteOrHuman)) &&
                        ( distance < static_cast<uint32_t>(anObject->engageRange)) &&
                        ( anObject->timeFromOrigin < kTimeToCheckHome) &&
                        ( targetObject->attributes & kCanBeEngaged)
                    )
                )
            )
        {
            keysDown |= ThinkObjectEngageTarget( anObject, targetObject, distance, &theta, timePass);
///--->>> END TARGETING <<<---///


            // if I'm in target object's range & it's looking at us & my health is less
            // than 1/2 its -- or I can't engage it
            if (    ( anObject->attributes & kCanEvade) &&
                    ( targetObject->attributes & kCanBeEvaded) &&
                    ( distance < static_cast<uint32_t>(targetObject->longestWeaponRange)) &&
                    (targetObject->attributes & kHated) &&
                    ( ABS( theta ) < kParanoiaAngle) &&
                    (
                        (!(targetObject->attributes & kCanBeEngaged))
                        ||
                        ( anObject->health <= targetObject->health )
                    )
                )
            {
                // try to evade, flee, run away
                if ( anObject->attributes & kHasDirectionGoal)
                {
                    if ( anObject->beam.type != kNoWeapon)
                    {
                        weaponObject = anObject->beam.base;
                        if ( weaponObject->frame.weapon.usage &
                            kUseForDefense)
                        {
                            keysDown |= kTwoKey;
                        }
                    }

                    if ( anObject->pulse.type != kNoWeapon)
                    {
                        weaponObject = anObject->pulse.base;
                        if ( weaponObject->frame.weapon.usage &
                            kUseForDefense)
                        {
                            keysDown |= kOneKey;
                        }
                    }

                    if ( anObject->special.type != kNoWeapon)
                    {
                        weaponObject = anObject->special.base;
                        if ( weaponObject->frame.weapon.usage &
                            kUseForDefense)
                        {
                            keysDown |= kEnterKey;
                        }
                    }

                    anObject->directionGoal = targetObject->direction;

                    if ( targetObject->attributes & kIsGuided)
                    {
                        if ( theta > 0)
                        {
                            mAddAngle( anObject->directionGoal, 90);
                        }
                        else if ( theta < 0)
                        {
                            mAddAngle( anObject->directionGoal, -90);
                        } else
                        {
                            beta = 90;
                            if ( anObject->location.h & 0x00000001)
//                          if (anObject->randomSeed.next(2))
                                beta = -90;
                            mAddAngle( anObject->directionGoal, beta);
                        }
                        theta =
                            mAngleDifference( anObject->directionGoal,
                            anObject->direction);
                        if ( ABS( theta) < 90)
                        {
                            keysDown |= kUpKey;
                        } else
                        {
                            keysDown |= kUpKey; // try an always thrust strategy
                        }
                    } else
                        {
                        if ( theta > 0)
                        {
                            mAddAngle( anObject->directionGoal, kEvadeAngle);
                        }
                        else if ( theta < 0)
                        {
                            mAddAngle( anObject->directionGoal, -kEvadeAngle);
                        } else
                        {
                            beta = kEvadeAngle;
//                          if (anObject->randomSeed.next(2))
                            if ( anObject->location.h & 0x00000001)
                                beta = -kEvadeAngle;
                            mAddAngle( anObject->directionGoal, beta);
                        }
                        theta = mAngleDifference( anObject->directionGoal,
                            anObject->direction);
                        if ( ABS( theta) < kEvadeAngle)
                        {
                            keysDown |= kUpKey;
                        } else
                        {
                            keysDown |= kUpKey; // try an always thrust strategy
                        }
                    }
                } else
                {
                    beta = kEvadeAngle;
                    if (anObject->randomSeed.next(2)) {
                        beta = -kEvadeAngle;
                    }
                    mAddAngle( anObject->direction, beta);
                    keysDown |= kUpKey;
                }

            // if we're not afraid, then
            } else
            {
                // if we are not within our closest weapon range then

                if (( distance > static_cast<uint32_t>(anObject->shortestWeaponRange)) ||
                        ( anObject->attributes & kIsGuided))
                    keysDown |= kUpKey;

                // if we are as close as we like
                else
                {
                    // if we're getting closer
                    if (( distance < kMotionMargin) ||
                        ((distance + kMotionMargin) <
                        static_cast<uint32_t>(anObject->lastTargetDistance)))
                    {
                        keysDown |= kDownKey;
                        anObject->lastTargetDistance = distance;
                    // if we're not getting closer, then if we're getting farther
                    } else if ( ( distance - kMotionMargin) >
                        static_cast<uint32_t>(anObject->lastTargetDistance))
                    {
                        keysDown |= kUpKey;
                        anObject->lastTargetDistance = distance;
                    }
                }
            }

            if ( anObject->targetObjectNumber ==
                    anObject->destinationObject)
            {
                if ( distance < static_cast<uint32_t>(baseObject->arriveActionDistance))
                {
                    if ( baseObject->arriveAction >= 0)
                    {
                        if ( !(anObject->runTimeFlags & kHasArrived))
                        {
                            offset.h = offset.v = 0;
                            execute_actions(
                                baseObject->arriveAction,
                                baseObject->arriveActionNum,
                                anObject, anObject->destObjectPtr, &offset, true);
                            anObject->runTimeFlags |= kHasArrived;
                        }
                    }
                }
            }
        } else if ( anObject->attributes & kIsGuided)
        {
            keysDown |= kUpKey;
        } else  // not guided & no target object or target object is out of engage range
        {
///--->>> BEGIN TARGETING <<<---///
            if (    (anObject->targetObjectNumber != kNoShip) &&
                    (
                        (
                            (!(anObject->attributes &
                                kRemoteOrHuman)) &&
                            ( distance < static_cast<uint32_t>(anObject->engageRange))
                        ) ||
                        ( anObject->attributes & kIsGuided)
                    )
                )
            {
                keysDown |= ThinkObjectEngageTarget( anObject, targetObject, distance, &theta, timePass);
                if (( targetObject->attributes & kCanBeEngaged) &&
                    ( anObject->attributes & kCanEngage) &&
                    ( distance < static_cast<uint32_t>(anObject->longestWeaponRange)) &&
                    ( targetObject->attributes & kHated))
                {
                } else if ( ( anObject->attributes & kCanEvade) &&
                            (targetObject->attributes & kHated) &&
                            ( targetObject->attributes & kCanBeEvaded) &&
                            (
                                (
                                    ( distance <
                                        static_cast<uint32_t>(targetObject->longestWeaponRange))
                                    &&
                                    ( ABS( theta ) < kParanoiaAngle)
                                ) ||
                                ( targetObject->attributes & kIsGuided)
                            )
                        )
                {

                    // try to evade, flee, run away
                    if ( anObject->attributes & kHasDirectionGoal)
                    {
                        if ( distance < static_cast<uint32_t>(anObject->longestWeaponRange))
                        {
                            if ( anObject->beam.type != kNoWeapon)
                            {
                                weaponObject = anObject->beam.base;
                                if ( weaponObject->frame.weapon.usage &
                                    kUseForDefense)
                                {
                                    keysDown |= kTwoKey;
                                }
                            }

                            if ( anObject->pulse.type != kNoWeapon)
                            {
                                weaponObject = anObject->pulse.base;
                                if ( weaponObject->frame.weapon.usage &
                                    kUseForDefense)
                                {
                                    keysDown |= kOneKey;
                                }
                            }

                            if ( anObject->special.type != kNoWeapon)
                            {
                                weaponObject = anObject->special.base;
                                if ( weaponObject->frame.weapon.usage &
                                    kUseForDefense)
                                {
                                    keysDown |= kEnterKey;
                                }
                            }
                        }

                        anObject->directionGoal =
                            targetObject->direction;

                        if ( theta > 0)
                        {
                            mAddAngle( anObject->directionGoal, kEvadeAngle);
                        }
                        else if ( theta < 0)
                        {
                            mAddAngle( anObject->directionGoal, -kEvadeAngle);
                        } else
                        {
                            beta = kEvadeAngle;
//                          if (anObject->randomSeed.next(2))
                            if ( anObject->location.h & 0x00000001)
                                beta = -kEvadeAngle;
                            mAddAngle( anObject->directionGoal, beta);
                        }
                        theta = mAngleDifference( anObject->directionGoal,
                            anObject->direction);
                        if ( ABS( theta) < kEvadeAngle)
                        {
                            keysDown |= kUpKey;
                        } else
                        {
                            keysDown |= kUpKey;
                        }
                    } else
                    {
                        beta = kEvadeAngle;
                        if (anObject->randomSeed.next(2)) {
                            beta = -kEvadeAngle;
                        }
                        mAddAngle( anObject->direction, beta);
                        keysDown |= kUpKey;
                    }
                }
            }
///--->>> END TARGETING <<<---///
            if (( anObject->attributes & kIsDestination) ||
                (( anObject->destinationObject == kNoDestinationObject)
                && ( anObject->destinationLocation.h ==
                kNoDestinationCoord)))
            {
                if (anObject->attributes & kOnAutoPilot)
                {
                    TogglePlayerAutoPilot( anObject);
                }
                keysDown |= kDownKey;
                anObject->timeFromOrigin = 0;
            } else
            {
                if ( anObject->destinationObject !=
                    kNoDestinationObject)
                {
                    targetObject =
                        anObject->destObjectPtr;
                    if (( targetObject != NULL) &&
                        ( targetObject->active) &&
                        ( targetObject->id == anObject->destObjectID))
                    {
                            if ( targetObject->seenByPlayerFlags &
                                anObject->myPlayerFlag)
                            {
                                dest.h = targetObject->location.h;
                                dest.v = targetObject->location.v;
                                anObject->destinationLocation.h =
                                    dest.h;
                                anObject->destinationLocation.v =
                                    dest.v;
                            } else
                            {
                                dest.h =
                                    anObject->destinationLocation.h;
                                dest.v =
                                    anObject->destinationLocation.v;
                            }
                            anObject->destObjectDest =
                                targetObject->destinationObject;
                            anObject->destObjectDestID = targetObject->destObjectID;
                    } else
                    {
                        anObject->duty = eNoDuty;
                        anObject->attributes &= ~kStaticDestination;
                        if ( targetObject == NULL)
                        {
                            keysDown |= kDownKey;
                            anObject->destObjectDest = kNoDestinationObject;
                            anObject->destinationObject =
                                kNoDestinationObject;
                            dest.h = anObject->location.h;
                            dest.v = anObject->location.v;
                            if (anObject->attributes & kOnAutoPilot)
                            {
                                TogglePlayerAutoPilot( anObject);
                            }
                        } else
                        {
                            anObject->destinationObject =
                                anObject->destObjectDest;
                            if ( anObject->destinationObject !=
                                kNoDestinationObject)
                            {
                                targetObject = mGetSpaceObjectPtr(anObject->destinationObject);
                                if ( targetObject->id != anObject->destObjectDestID) targetObject = NULL;
                            } else targetObject = NULL;
                            if ( targetObject != NULL)
                            {
                                anObject->destObjectPtr =
                                    targetObject;
                                anObject->destObjectID =
                                    targetObject->id;
                                anObject->destObjectDest =
                                    targetObject->destinationObject;
                                anObject->destObjectDestID = targetObject->destObjectID;
                                dest.h = targetObject->location.h;
                                dest.v = targetObject->location.v;
                            } else
                            {
                                anObject->duty = eNoDuty;
                                keysDown |= kDownKey;
                                anObject->destinationObject =
                                    kNoDestinationObject;
                                anObject->destObjectDest = kNoDestinationObject;
                                anObject->destObjectPtr = NULL;
                                dest.h = anObject->location.h;
                                dest.v = anObject->location.v;
                                if (anObject->attributes & kOnAutoPilot)
                                {
                                    TogglePlayerAutoPilot( anObject);
                                }
                            }
                        }
                    }
                } else // no destination object; just coords
                {
                    if (anObject->attributes & kOnAutoPilot)
                    {
                        TogglePlayerAutoPilot( anObject);
                    }
                    targetObject = NULL;
                    dest.h = anObject->destinationLocation.h;
                    dest.v = anObject->destinationLocation.v;
                }

                ThinkObjectGetCoordVector( anObject, &dest, &distance, &angle);

                if ( anObject->attributes & kHasDirectionGoal)
                {
                    theta = mAngleDifference( angle, anObject->directionGoal);
                    if ( ABS( theta) > kDirectionError)
                    {
                        anObject->directionGoal = angle;
                    }

                    theta = mAngleDifference( anObject->direction,
                            anObject->directionGoal);
                    theta = ABS( theta);
                } else
                {
                    anObject->direction = angle;
                    theta = 0;
                }

                if ( distance < kEngageRange)
                {
                    anObject->timeFromOrigin = 0;
                }

                if ( distance > static_cast<uint32_t>(baseObject->arriveActionDistance))
                {
                    if ( theta < kEvadeAngle)
                        keysDown |= kUpKey;
                    anObject->lastTargetDistance = distance;
                    if (( anObject->special.type != kNoWeapon) &&
                        ( distance > kWarpInDistance)
                        && ( theta <= kDirectionError))
                    {
                        if ( anObject->special.base->frame.weapon.usage
                            & kUseForTransportation)
                        {
                            keysDown |= kEnterKey;
                        }
                    }
                    if (( baseObject->warpSpeed > 0) && ( anObject->energy >
                        ( anObject->baseType->energy >> kWarpInEnergyFactor)) &&
                        ( distance > kWarpInDistance)
                        && ( theta <= kDirectionError))
                    {
                        keysDown |= kWarpKey;
                    }
                } else
                {
                    if (( targetObject != NULL) &&
                        ( targetObject->owner == anObject->owner) &&
                        ( targetObject->attributes &
                             anObject->attributes & kHasDirectionGoal))
                    {
                        anObject->directionGoal =
                            targetObject->direction;
                        if (( targetObject->keysDown & kWarpKey) &&
                            ( baseObject->warpSpeed > 0))
                        {
                            theta = mAngleDifference( anObject->direction, targetObject->direction);
                            if ( ABS( theta) < kDirectionError)
                                keysDown |= kWarpKey;
                        }
                    }

                    if ( distance < static_cast<uint32_t>(baseObject->arriveActionDistance))
                    {
                        if ( baseObject->arriveAction >= 0)
                        {
                            if ( !(anObject->runTimeFlags &
                                kHasArrived))
                            {
                                offset.h = offset.v = 0;
                                execute_actions(
                                    baseObject->arriveAction,
                                    baseObject->arriveActionNum,
                                    anObject, anObject->destObjectPtr, &offset, true);
                                anObject->runTimeFlags |= kHasArrived;
                            }
                        }
                    }

                    // if we're getting closer
                    if ((distance + kMotionMargin) <
                            static_cast<uint32_t>(anObject->lastTargetDistance) )
                    {
                        keysDown |= kDownKey;
                        anObject->lastTargetDistance = distance;
                    // if we're not getting closer, then if we're getting farther
                    } else if ( ( distance - kMotionMargin) >
                        static_cast<uint32_t>(anObject->lastTargetDistance))
                    {
                        if ( theta < kEvadeAngle)
                            keysDown |= kUpKey;
                        else keysDown |= kDownKey;
                        anObject->lastTargetDistance = distance;
                    }
                }
            }
        }
    } else // object is human controlled -- we need to calc target angle
    {
        ThinkObjectResolveTarget( anObject, &dest, &distance, &targetObject);

        if (( anObject->attributes & kCanEngage) &&
            ( distance < static_cast<uint32_t>(anObject->engageRange)) &&
            (anObject->targetObjectNumber != kNoShip))
        {
            // if target is in our weapon range & we hate the object
            if (( distance < static_cast<uint32_t>(anObject->longestWeaponRange)) &&
                ( targetObject->attributes & kHated))
            {
                // find "best" weapon (how do we want to aim?)
                // difference = closest range

                difference = anObject->longestWeaponRange;

                bestWeapon = NULL;

                if ( anObject->beam.type != kNoWeapon)
                {
                    bestWeapon = weaponObject = anObject->beam.base;
                    if ( ( weaponObject->frame.weapon.usage &
                        kUseForAttacking) &&
                        ( static_cast<uint32_t>(weaponObject->frame.weapon.range) >=
                            distance) &&
                        ( weaponObject->frame.weapon.range <
                            difference))
                    {
                        bestWeapon = weaponObject;
                        difference = weaponObject->frame.weapon.range;
                    }
                }

                if ( anObject->pulse.type != kNoWeapon)
                {
                    weaponObject = anObject->pulse.base;
                    if ( ( weaponObject->frame.weapon.usage &
                        kUseForAttacking) &&
                        ( static_cast<uint32_t>(weaponObject->frame.weapon.range) >= distance)
                        && ( weaponObject->frame.weapon.range <
                        difference))
                    {
                        bestWeapon = weaponObject;
                        difference = weaponObject->frame.weapon.range;
                    }
                }

                if ( anObject->special.type != kNoWeapon)
                {
                    weaponObject = anObject->special.base;
                    if ( ( weaponObject->frame.weapon.usage &
                        kUseForAttacking) &&
                        ( static_cast<uint32_t>(weaponObject->frame.weapon.range) >= distance)
                        && ( weaponObject->frame.weapon.range <
                        difference))
                    {
                        bestWeapon = weaponObject;
                        difference = weaponObject->frame.weapon.range;
                    }
                }

                // offset dest for anticipated position -- overkill?

                if ( bestWeapon != NULL)
                {

                    dcalc = lsqrt( distance);

                    calcv = targetObject->velocity.h -
                        anObject->velocity.h;
                    fdist = mLongToFixed( dcalc);
                    fdist = mMultiplyFixed( bestWeapon->frame.weapon.inverseSpeed, fdist);
                    calcv = mMultiplyFixed( calcv, fdist);
                    difference = mFixedToLong( calcv);
                    dest.h -= difference;

                    calcv = targetObject->velocity.v -
                        anObject->velocity.v;
                    calcv = mMultiplyFixed( calcv, fdist);
                    difference = mFixedToLong( calcv);
                    dest.v -= difference;
                }
            } // target is not in our weapon range (or we don't hate it)

            // this is human controlled--if it's too far away, tough nougies
            // find angle between me & dest
            slope = MyFixRatio(anObject->location.h - dest.h,
                        anObject->location.v - dest.v);
            angle = AngleFromSlope( slope);

            if ( dest.h < anObject->location.h)
                mAddAngle( angle, 180);
            else if (( anObject->location.h == dest.h) &&
                    ( dest.v < anObject->location.v))
                angle = 0;

            if ( targetObject->cloakState > 250)
            {
                angle -= 45;
                mAddAngle(angle, anObject->randomSeed.next(90));
            }
            anObject->targetAngle = angle;
        }
    }

    return( keysDown);
}

uint32_t ThinkObjectWarpInPresence( spaceObjectType *anObject)
{
    uint32_t        keysDown = anObject->keysDown & kSpecialKeyMask;
    int32_t         longscrap;
    fixedPointType  newVel;

    if (( !(anObject->attributes & kRemoteOrHuman)) ||
            ( anObject->attributes & kOnAutoPilot))
    {
        keysDown = kWarpKey;
    }
    anObject->presenceData = ( anObject->presenceData & ~0x000000ff)
        | (( anObject->presenceData & 0x000000ff) + kDecideEveryCycles);
    if ( !(anObject->presenceData & 0x10000000))
    {
        longscrap = kMaxSoundVolume;
        mPlayDistanceSound(longscrap, anObject, kWarpOne, kMediumPersistence, kPrioritySound);
        anObject->presenceData |= 0x10000000;
    } else if (( !(anObject->presenceData & 0x20000000)) &&
        (( anObject->presenceData & 0x000000ff) > 25))
    {
        longscrap = kMaxSoundVolume;
        mPlayDistanceSound(longscrap, anObject, kWarpTwo, kMediumPersistence, kPrioritySound);
        anObject->presenceData |= 0x20000000;
    } if (( !(anObject->presenceData & 0x40000000)) &&
        (( anObject->presenceData & 0x000000ff) > 50))
    {
        longscrap = kMaxSoundVolume;
        mPlayDistanceSound(longscrap, anObject, kWarpThree, kMediumPersistence, kPrioritySound);
        anObject->presenceData |= 0x40000000;
    } if (( !(anObject->presenceData & 0x80000000)) &&
        (( anObject->presenceData & 0x000000ff) > 75))
    {
        longscrap = kMaxSoundVolume;
        mPlayDistanceSound(longscrap, anObject, kWarpFour, kMediumPersistence, kPrioritySound);
        anObject->presenceData |= 0x80000000;
    }

    if ( (anObject->presenceData & 0x000000ff) > 100)
    {
        anObject->energy -= anObject->baseType->energy >> kWarpInEnergyFactor;
        anObject->warpEnergyCollected += anObject->baseType->energy >> kWarpInEnergyFactor;
        if ( anObject->energy <= 0)
        {
            anObject->presenceState = kNormalPresence;
            anObject->energy = 0;
        } else
        {

            anObject->presenceState = kWarpingPresence;
            anObject->presenceData = anObject->baseType->warpSpeed;
            anObject->attributes &= ~kOccupiesSpace;
            newVel.h = newVel.v = 0;
    /*
            CreateAnySpaceObject( globals()->scenarioFileInfo.warpInFlareID, &(newVel),
                &(anObject->location), anObject->direction, kNoOwner,
                0, nil, -1, -1, -1);
    */
            CreateAnySpaceObject( globals()->scenarioFileInfo.warpInFlareID, &(newVel),
                &(anObject->location), anObject->direction, kNoOwner,
                0, -1);
        }
    }

    return( keysDown);
}

uint32_t ThinkObjectWarpingPresence( spaceObjectType *anObject)
{
    uint32_t        keysDown = anObject->keysDown & kSpecialKeyMask, distance;
    coordPointType  dest;
    spaceObjectType *targetObject = NULL;
    int16_t         angle, theta;

    if ( anObject->energy <= 0)
    {
        anObject->presenceState = kWarpOutPresence;
    }
    if (( !(anObject->attributes & kRemoteOrHuman)) ||
                ( anObject->attributes & kOnAutoPilot))
    {
        ThinkObjectResolveDestination( anObject, &dest, &targetObject);
        ThinkObjectGetCoordVector( anObject, &dest, &distance, &angle);


        if ( anObject->attributes & kHasDirectionGoal)
        {
            theta = mAngleDifference( angle, anObject->directionGoal);
            if ( ABS( theta) > kDirectionError)
            {
                anObject->directionGoal = angle;
            }
        } else
        {
            anObject->direction = angle;
        }

        if ( distance < anObject->baseType->warpOutDistance)
        {
            if ( targetObject != NULL)
            {
                if (( targetObject->presenceState == kWarpInPresence)
                    || ( targetObject->presenceState == kWarpingPresence))
                {
                    keysDown |= kWarpKey;
                }
            }
        } else
        {
            keysDown |= kWarpKey;
        }
    }
    return( keysDown);
}

uint32_t ThinkObjectWarpOutPresence( spaceObjectType *anObject, baseObjectType *baseObject)
{
    uint32_t        keysDown = anObject->keysDown & kSpecialKeyMask;
    Fixed           calcv, fdist;
    fixedPointType  newVel;

    anObject->presenceData -= mLongToFixed(kWarpAcceleration);
    if ( anObject->presenceData < anObject->maxVelocity)
    {
        AlterObjectBattery( anObject, anObject->warpEnergyCollected);
        anObject->warpEnergyCollected = 0;

        anObject->presenceState = kNormalPresence;
        anObject->attributes |=
            baseObject->attributes & kOccupiesSpace;

        // warp out

        GetRotPoint(&fdist, &calcv, anObject->direction);

        // multiply by max velocity

        fdist = mMultiplyFixed( anObject->maxVelocity, fdist);
        calcv = mMultiplyFixed( anObject->maxVelocity, calcv);
        anObject->velocity.h = fdist;
        anObject->velocity.v = calcv;
        newVel.h = newVel.v = 0;


        CreateAnySpaceObject( globals()->scenarioFileInfo.warpOutFlareID, &(newVel),
            &(anObject->location), anObject->direction, kNoOwner, 0,
            -1);
    }
    return( keysDown);
}

uint32_t ThinkObjectLandingPresence( spaceObjectType *anObject)
{
    uint32_t        keysDown = anObject->keysDown & kSpecialKeyMask, distance, dcalc;
    spaceObjectType *targetObject = NULL;
    coordPointType  dest;
    int32_t         difference;
    Fixed           slope;
    int16_t         angle, theta, shortx, shorty;

    keysDown = 0;

    // we repeat an object's normal action for having a destination

    if (( anObject->attributes & kIsDestination) ||
        (( anObject->destinationObject == kNoDestinationObject) &&
        ( anObject->destinationLocation.h == kNoDestinationCoord)))
    {
        if (anObject->attributes & kOnAutoPilot)
        {
            TogglePlayerAutoPilot( anObject);
        }
        keysDown |= kDownKey;
        distance = 0;
    } else
    {
        if ( anObject->destinationObject != kNoDestinationObject)
        {
            targetObject = anObject->destObjectPtr;
            if (( targetObject != NULL) && ( targetObject->active) &&
                ( targetObject->id == anObject->destObjectID))
            {
                if ( targetObject->seenByPlayerFlags &
                    anObject->myPlayerFlag)
                {
                    dest.h = targetObject->location.h;
                    dest.v = targetObject->location.v;
                    anObject->destinationLocation.h = dest.h;
                    anObject->destinationLocation.v = dest.v;
                } else
                {
                    dest.h = anObject->destinationLocation.h;
                    dest.v = anObject->destinationLocation.v;
                }
                anObject->destObjectDest =
                    targetObject->destinationObject;
                anObject->destObjectDestID = targetObject->destObjectID;
            } else
            {
                anObject->duty = eNoDuty;
                anObject->attributes &= ~kStaticDestination;
                if ( targetObject == NULL)
                {
                    keysDown |= kDownKey;
                    anObject->destinationObject = kNoDestinationObject;
                    anObject->destObjectDest = kNoDestinationObject;
                    dest.h = anObject->location.h;
                    dest.v = anObject->location.v;
                } else
                {
                    anObject->destinationObject =
                        anObject->destObjectDest;
                    if ( anObject->destinationObject !=
                        kNoDestinationObject)
                    {
                        targetObject = mGetSpaceObjectPtr(anObject->destinationObject);
                        if ( targetObject->id != anObject->destObjectDestID) targetObject = NULL;
                    } else targetObject = NULL;
                    if ( targetObject != NULL)
                    {
                        anObject->destObjectPtr = targetObject;
                        anObject->destObjectID = targetObject->id;
                        anObject->destObjectDest =
                            targetObject->destinationObject;
                        anObject->destObjectDestID = targetObject->destObjectID;
                        dest.h = targetObject->location.h;
                        dest.v = targetObject->location.v;
                    } else
                    {
                        keysDown |= kDownKey;
                        anObject->destinationObject =
                            kNoDestinationObject;
                        anObject->destObjectDest = kNoDestinationObject;
                        anObject->destObjectPtr = NULL;
                        dest.h = anObject->location.h;
                        dest.v = anObject->location.v;
                    }
                }
            }
        } else // no destination object; just coords
        {
            if (anObject->attributes & kOnAutoPilot)
            {
                TogglePlayerAutoPilot( anObject);
            }
            targetObject = NULL;
            dest.h = anObject->location.h;
            dest.v = anObject->location.v;
        }

        difference = ABS<int>( dest.h - anObject->location.h);
        dcalc = difference;
        difference =  ABS<int>( dest.v - anObject->location.v);
        distance = difference;
        if (( dcalc > kMaximumAngleDistance) ||
            ( distance > kMaximumAngleDistance))
        {
            if (( dcalc > kMaximumRelevantDistance) ||
                ( distance > kMaximumRelevantDistance))
            {
                distance = kMaximumRelevantDistanceSquared;
            } else
            {
                distance = distance * distance + dcalc * dcalc;
            }
            shortx = (anObject->location.h - dest.h) >> 4;
            shorty = (anObject->location.v - dest.v) >> 4;
            // find angle between me & dest
            slope = MyFixRatio( shortx, shorty);
            angle = AngleFromSlope( slope);
            if ( shortx > 0)
            {
                mAddAngle( angle, 180);
            } else if (( shortx == 0) && ( shorty > 0))
            {
                angle = 0;
            }
        }
        else
        {
            distance = distance * distance + dcalc * dcalc;

            // find angle between me & dest
            slope = MyFixRatio(anObject->location.h - dest.h,
                        anObject->location.v - dest.v);
            angle = AngleFromSlope( slope);

            if ( dest.h < anObject->location.h)
                mAddAngle( angle, 180);
            else if (( anObject->location.h == dest.h) &&
                    ( dest.v < anObject->location.v))
                angle = 0;
        }

        if ( anObject->attributes & kHasDirectionGoal)
        {
            theta = mAngleDifference( angle, anObject->directionGoal);
            if ( ABS( theta) > kDirectionError)
            {
                anObject->directionGoal = angle;

            }

            theta = mAngleDifference( anObject->direction,
                    anObject->directionGoal);
            theta = ABS( theta);
        } else
        {
            anObject->direction = angle;
            theta = 0;
        }


    }
    if ( distance > kLandingDistance)
    {
        if ( theta < kEvadeAngle)
            keysDown |= kUpKey;
        else keysDown |= kDownKey;
        anObject->lastTargetDistance = distance;
    } else
    {
        keysDown |= kDownKey;
        anObject->presenceData =
            (
                (
                    anObject->presenceData &
                        kPresenceDataLoWordMask
                ) -
                (
                    (
                        anObject->presenceData &
                            kPresenceDataHiWordMask
                    )
                    >> kPresenceDataHiWordShift
                )
            ) |
            (
                anObject->presenceData & kPresenceDataHiWordMask
            );
    }

    if ( (anObject->presenceData & kPresenceDataLoWordMask) <= 0)
    {
        execute_actions(
                anObject->baseType->expireAction,
                anObject->baseType->expireActionNum & kDestroyActionNotMask,
                anObject, targetObject, NULL, true);
        anObject->active = kObjectToBeFreed;

    } else if ( anObject->sprite != NULL)
        anObject->sprite->scale = (anObject->presenceData &
            kPresenceDataLoWordMask);

    return( keysDown);
}

// this gets the distance & angle between an object and arbitrary coords
void ThinkObjectGetCoordVector( spaceObjectType *anObject, coordPointType *dest, uint32_t *distance, int16_t *angle)
{
    int32_t         difference;
    uint32_t        dcalc;
    int16_t         shortx, shorty;
    Fixed           slope;

    difference = ABS<int>( dest->h - anObject->location.h);
    dcalc = difference;
    difference =  ABS<int>( dest->v - anObject->location.v);
    *distance = difference;
    if (( *distance == 0) && ( dcalc == 0))
    {
        *angle = anObject->direction;
        return;
    }

    if (( dcalc > kMaximumAngleDistance) ||
        ( *distance > kMaximumAngleDistance))
    {
        if (( dcalc > kMaximumRelevantDistance) ||
            ( *distance > kMaximumRelevantDistance))
        {
            *distance = kMaximumRelevantDistanceSquared;
        } else
        {
            *distance = *distance * *distance + dcalc * dcalc;
        }
        shortx = (anObject->location.h - dest->h) >> 4;
        shorty = (anObject->location.v - dest->v) >> 4;
        // find angle between me & dest
        slope = MyFixRatio( shortx, shorty);
        *angle = AngleFromSlope( slope);
        if ( shortx > 0)
        {
            mAddAngle( *angle, 180);
        } else if (( shortx == 0) && ( shorty > 0))
        {
            *angle = 0;
        }
    }
    else
    {
        *distance = *distance * *distance + dcalc * dcalc;

        // find angle between me & dest
        slope = MyFixRatio(anObject->location.h - dest->h,
                    anObject->location.v - dest->v);
        *angle = AngleFromSlope( slope);

        if ( dest->h < anObject->location.h)
            mAddAngle( *angle, 180);
        else if (( anObject->location.h == dest->h) &&
                ( dest->v < anObject->location.v))
            *angle = 0;
    }
}

void ThinkObjectGetCoordDistance( spaceObjectType *anObject, coordPointType *dest, uint32_t *distance)
{
    int32_t         difference;
    uint32_t        dcalc;

    difference = ABS<int>( dest->h - anObject->location.h);
    dcalc = difference;
    difference =  ABS<int>( dest->v - anObject->location.v);
    *distance = difference;
    if (( *distance == 0) && ( dcalc == 0))
    {
        return;
    }

    if (( dcalc > kMaximumAngleDistance) ||
        ( *distance > kMaximumAngleDistance))
    {
        if (( dcalc > kMaximumRelevantDistance) ||
            ( *distance > kMaximumRelevantDistance))
        {
            *distance = kMaximumRelevantDistanceSquared;
        } else
        {
            *distance = *distance * *distance + dcalc * dcalc;
        }
    } else
    {
        *distance = *distance * *distance + dcalc * dcalc;
    }
}

// this resolves an object's destination to its coordinates, returned in dest
void ThinkObjectResolveDestination( spaceObjectType *anObject, coordPointType *dest, spaceObjectType **targetObject)
{
    *targetObject = NULL;

    if (( anObject->attributes & kIsDestination) ||
        (( anObject->destinationObject == kNoDestinationObject) &&
        ( anObject->destinationLocation.h == kNoDestinationCoord)))
    {
        if (anObject->attributes & kOnAutoPilot)
        {
            TogglePlayerAutoPilot( anObject);
        }
        dest->h = anObject->location.h;
        dest->v = anObject->location.v;
    } else
    {
        if ( anObject->destinationObject != kNoDestinationObject)
        {
            *targetObject = anObject->destObjectPtr;
            if (( *targetObject != NULL) && ( (*targetObject)->active) &&
                ( (*targetObject)->id == anObject->destObjectID))
            {
                if ( (*targetObject)->seenByPlayerFlags &
                    anObject->myPlayerFlag)
                {
                    dest->h = (*targetObject)->location.h;
                    dest->v = (*targetObject)->location.v;
                    anObject->destinationLocation.h = dest->h;
                    anObject->destinationLocation.v = dest->v;
                } else
                {
                    dest->h = anObject->destinationLocation.h;
                    dest->v = anObject->destinationLocation.v;
                }
                anObject->destObjectDest =
                    (*targetObject)->destinationObject;
                anObject->destObjectDestID = (*targetObject)->destObjectID;
            } else
            {
                anObject->duty = eNoDuty;
                anObject->attributes &= ~kStaticDestination;
                if ( (*targetObject) == NULL)
                {
                    anObject->destinationObject = kNoDestinationObject;
                    anObject->destObjectDest = kNoDestinationObject;
                    dest->h = anObject->location.h;
                    dest->v = anObject->location.v;
                } else
                {
                    anObject->destinationObject =
                        anObject->destObjectDest;
                    if ( anObject->destinationObject !=
                        kNoDestinationObject)
                    {
                        (*targetObject) = mGetSpaceObjectPtr(anObject->destinationObject);
                        if ( (*targetObject)->id != anObject->destObjectDestID) *targetObject = NULL;
                    } else *targetObject = NULL;
                    if ( *targetObject != NULL)
                    {
                        anObject->destObjectPtr = (*targetObject);
                        anObject->destObjectID = (*targetObject)->id;
                        anObject->destObjectDest =
                            (*targetObject)->destinationObject;
                        anObject->destObjectDestID = (*targetObject)->destObjectID;
                        dest->h = (*targetObject)->location.h;
                        dest->v = (*targetObject)->location.v;
                    } else
                    {
                        anObject->duty = eNoDuty;
                        anObject->destinationObject =
                            kNoDestinationObject;
                        anObject->destObjectDest = kNoDestinationObject;
                        anObject->destObjectPtr = NULL;
                        dest->h = anObject->location.h;
                        dest->v = anObject->location.v;
                    }
                }
            }
        } else // no destination object; just coords
        {
            (*targetObject) = NULL;
            if ( anObject->destinationLocation.h == kNoDestinationCoord)
            {
                if (anObject->attributes & kOnAutoPilot)
                {
                    TogglePlayerAutoPilot( anObject);
                }
                dest->h = anObject->location.h;
                dest->v = anObject->location.v;
            } else
            {
                dest->h = anObject->destinationLocation.h;
                dest->v = anObject->destinationLocation.v;
            }
        }
    }
}

bool ThinkObjectResolveTarget( spaceObjectType *anObject, coordPointType *dest,
    uint32_t *distance, spaceObjectType **targetObject)
{
    spaceObjectType *closestObject;

    dest->h = dest->v = 0xffffffff;
    *distance = 0xffffffff;

    if ( anObject->closestObject != kNoShip)
    {
        closestObject = mGetSpaceObjectPtr(anObject->closestObject);
    } else closestObject = NULL;

    // if we have no target  then
    if (anObject->targetObjectNumber == kNoShip)
    {
        // if the closest object is appropriate (if it exists, it should be, then
        if (( closestObject != NULL) &&
            ( closestObject->attributes & kPotentialTarget))
        {
            // select closest object as target (and for now be satisfied with our direction
            if ( anObject->attributes & kHasDirectionGoal)
            {
                anObject->directionGoal = anObject->direction;
            }
            anObject->targetObjectNumber = anObject->closestObject;
            anObject->targetObjectID = closestObject->id;
        } else // otherwise, no target, no closest, cancel
        {
            closestObject = *targetObject = NULL;
            anObject->targetObjectNumber = kNoShip;
            anObject->targetObjectID = kNoShip;
            dest->h = anObject->location.h;
            dest->v = anObject->location.v;
            *distance = anObject->engageRange;
            return ( false);
        }
    }

    // if we have a target of any kind (we must by now)
    if ( anObject->targetObjectNumber != kNoShip)
    {
        // make sure we're still talking about the same object
        *targetObject = mGetSpaceObjectPtr(anObject->targetObjectNumber);

        // if the object is wrong or smells at all funny, then
        if  (
                (
                    !((*targetObject)->active)
                )
            ||
                ( (*targetObject)->id != anObject->targetObjectID)
            ||
                (
                    ( (*targetObject)->owner == anObject->owner)
                &&
                    ( (*targetObject)->attributes & kHated)
                )
            ||
                (
                    (
                        !((*targetObject)->attributes & kPotentialTarget)
                    )
                &&
                    (
                        !((*targetObject)->attributes & kHated)
                    )
                )
            )
        {
            // if we have a closest ship
            if ( anObject->closestObject != kNoShip)
            {
                // make it our target
                anObject->targetObjectNumber = anObject->closestObject;
                closestObject = *targetObject = mGetSpaceObjectPtr(anObject->targetObjectNumber);
                anObject->targetObjectID = closestObject->id;
                if ( !((*targetObject)->attributes & kPotentialTarget))
                {   // cancel
                    *targetObject = NULL;
                    anObject->targetObjectNumber = kNoShip;
                    anObject->targetObjectID = kNoShip;
                    dest->h = anObject->location.h;
                    dest->v = anObject->location.v;
                    *distance = anObject->engageRange;
                    return ( false);
                }
            } else // no legal target, no closest, cancel
            {
                closestObject = *targetObject = NULL;
                anObject->targetObjectNumber = kNoShip;
                anObject->targetObjectID = kNoShip;
                dest->h = anObject->location.h;
                dest->v = anObject->location.v;
                *distance = anObject->engageRange;
                return ( false);
            }
        }/* else // the target *is* legal
        {
            if ( anObject->attributes & kIsGuided)
            {
                if (((!(targetObject->attributes & kHated)) ||
                    ( !(targetObject->active))) &&
                    ( anObject->closestObject != kNoShip))
                {
                    closestObject = gSpaceObjectData.get() + anObject->closestObject;
                    if ( ( closestObject->attributes & kHated))
                    {
                        targetObject = closestObject;
                        anObject->targetObjectNumber =
                            anObject->closestObject;
                        anObject->targetObjectID = targetObject->id;
                    }
                }
            }
        }*/

        dest->h = (*targetObject)->location.h;
        dest->v = (*targetObject)->location.v;

        // if it's not the closest object & we have a closest object
        if ((anObject->closestObject != kNoShip) &&
            ( anObject->targetObjectNumber != anObject->closestObject)
            && ( !(anObject->attributes & kIsGuided))
            && ( closestObject->attributes & kPotentialTarget))
        {
            // then calculate the distance
            ThinkObjectGetCoordDistance( anObject, dest, distance);

            if (( ( *distance >> 1L) > anObject->closestDistance) ||
                ( ! (anObject->attributes & kCanEngage)) ||
                ( anObject->attributes &
                kRemoteOrHuman))
            {
                anObject->targetObjectNumber = anObject->closestObject;
                *targetObject = mGetSpaceObjectPtr(anObject->targetObjectNumber);
                anObject->targetObjectID = (*targetObject)->id;
                dest->h = (*targetObject)->location.h;
                dest->v = (*targetObject)->location.v;
                *distance = anObject->closestDistance;
                if ( (*targetObject)->cloakState > 250)
                {
                    dest->h -= 200;
                    dest->v -= 200;
                }
            }
            return( true);
        } else // if target is closest object
        {
            // otherwise distance is the closestDistance
            *distance = anObject->closestDistance;
            return( true);
        }
    } else // we don't have a target object
    {
        // set the distance to the engage range ie nothing to engage
        closestObject = *targetObject = NULL;
        anObject->targetObjectNumber = kNoShip;
        anObject->targetObjectID = kNoShip;
        dest->h = anObject->location.h;
        dest->v = anObject->location.v;
        *distance = anObject->engageRange;
        return ( false);
    }
}

uint32_t ThinkObjectEngageTarget( spaceObjectType *anObject, spaceObjectType *targetObject,
    uint32_t distance, int16_t *theta, int32_t timePass)
{
    uint32_t        keysDown = 0;
    baseObjectType  *bestWeapon, *weaponObject;
    coordPointType  dest;
    int32_t         difference;
    int16_t         angle, beta;
    Fixed           slope;

    *theta = 0xffff;

    dest.h = targetObject->location.h;
    dest.v = targetObject->location.v;
    if ( targetObject->cloakState > 250)
    {
        dest.h -= 70;
        dest.h += anObject->randomSeed.next(140);
        dest.v -= 70;
        dest.v += anObject->randomSeed.next(140);
    }

    // if target is in our weapon range & we hate the object
    if (    ( distance < static_cast<uint32_t>(anObject->longestWeaponRange)) &&
            ( targetObject->attributes & kCanBeEngaged) &&
            ( targetObject->attributes & kHated)
        )
    {
        // find "best" weapon (how do we want to aim?)
        // difference = closest range

        if ( anObject->attributes & kCanAcceptDestination)
        {
            anObject->timeFromOrigin += timePass;
        }

        difference = anObject->longestWeaponRange;

        if ( anObject->beam.type != kNoWeapon)
        {
            bestWeapon = weaponObject = anObject->beam.base;
            if ( ( weaponObject->frame.weapon.usage &
                kUseForAttacking) &&
                ( static_cast<uint32_t>(weaponObject->frame.weapon.range) >= distance)
                &&
                ( weaponObject->frame.weapon.range <
                difference))
            {
                bestWeapon = weaponObject;
                difference = weaponObject->frame.weapon.range;
            }
        }

        if ( anObject->pulse.type != kNoWeapon)
        {
            weaponObject = anObject->pulse.base;
            if ( ( weaponObject->frame.weapon.usage &
                kUseForAttacking) &&
                ( static_cast<uint32_t>(weaponObject->frame.weapon.range) >= distance)
                && ( weaponObject->frame.weapon.range <
                difference))
            {
                bestWeapon = weaponObject;
                difference = weaponObject->frame.weapon.range;
            }
        }

        if ( anObject->special.type != kNoWeapon)
        {
            weaponObject = anObject->special.base;
            if ( ( weaponObject->frame.weapon.usage &
                kUseForAttacking) &&
                ( static_cast<uint32_t>(weaponObject->frame.weapon.range) >= distance)
                && ( weaponObject->frame.weapon.range <
                difference))
            {
                bestWeapon = weaponObject;
                difference = weaponObject->frame.weapon.range;
            }
        }
//      dest.h = targetObject->location.h;
//      dest.v = targetObject->location.v;
    } // target is not in our weapon range (or we don't hate it)

    // We don't need to worry if it is very far away, since it must be within farthest weapon range
    // find angle between me & dest
    slope = MyFixRatio(anObject->location.h - dest.h,
                anObject->location.v - dest.v);
    angle = AngleFromSlope( slope);

    if ( dest.h < anObject->location.h)
        mAddAngle( angle, 180);
    else if (( anObject->location.h == dest.h) &&
            ( dest.v < anObject->location.v))
        angle = 0;

    if ( targetObject->cloakState > 250)
    {
        angle -= 45;
        mAddAngle(angle, anObject->randomSeed.next(90));
    }
    anObject->targetAngle = angle;

    if ( anObject->attributes & kHasDirectionGoal)
    {
        *theta = mAngleDifference( angle, anObject->directionGoal);
        if (( ABS( *theta) > kDirectionError) ||
            ( !(anObject->attributes & kIsGuided)))
        {
            anObject->directionGoal = angle;

        }

        beta = targetObject->direction;
        mAddAngle( beta, ROT_180);
        *theta = mAngleDifference( beta, angle);
    } else
    {
        anObject->direction = angle;
        *theta = 0;
    }

    // if target object is in range
    if (( distance < static_cast<uint32_t>(anObject->longestWeaponRange)) &&
        ( targetObject->attributes & kHated))
    {
        // fire away
        beta = anObject->direction;
        beta = mAngleDifference( beta, angle);

        if ( anObject->pulse.type != kNoWeapon)
        {
            weaponObject = anObject->pulse.base;
            if (( weaponObject->frame.weapon.usage &
                kUseForAttacking) &&
                (( ABS( beta) <= kShootAngle) ||
                ( weaponObject->attributes & kAutoTarget)) &&
                ( distance < static_cast<uint32_t>(weaponObject->frame.weapon.range)))
            {
                keysDown |= kOneKey;
            }
        }

        if ( anObject->beam.type != kNoWeapon)
        {
            weaponObject = anObject->beam.base;
            if (( weaponObject->frame.weapon.usage &
                kUseForAttacking) &&
                (( ABS( beta) <= kShootAngle) ||
                ( weaponObject->attributes & kAutoTarget)) &&
                ( distance < static_cast<uint32_t>(weaponObject->frame.weapon.range)))
            {
                keysDown |= kTwoKey;
            }
        }

        if ( anObject->special.type != kNoWeapon)
        {
            weaponObject = anObject->special.base;
            if (( weaponObject->frame.weapon.usage &
                kUseForAttacking) &&
                (( ABS( beta) <= kShootAngle) ||
                ( weaponObject->attributes & kAutoTarget)) &&
                ( distance < static_cast<uint32_t>(weaponObject->frame.weapon.range)))
            {
                keysDown |= kEnterKey;
            }
        }
    }// target is not in range
    return( keysDown);
}

void HitObject(spaceObjectType *anObject, spaceObjectType *sObject) {
    if (anObject->active != kObjectInUse) {
        return;
    }

    anObject->timeFromOrigin = 0;
    if (((anObject->health - sObject->baseType->damage) < 0)
            && (anObject->attributes & (kIsPlayerShip | kRemoteOrHuman))
            && !(anObject->baseType->destroyActionNum & kDestroyActionDontDieFlag)) {
        CreateFloatingBodyOfPlayer( anObject);
    }
    AlterObjectHealth(anObject, -sObject->baseType->damage);
    if (anObject->shieldColor != 0xFF) {
        anObject->hitState = ( anObject->health * kHitStateMax) / anObject->baseType->health;
        anObject->hitState += 16;
    }

    if (anObject->cloakState > 0) {
        anObject->cloakState = 1;
    }

    if (anObject->health < 0
            && (anObject->owner == globals()->gPlayerAdmiralNumber)
            && (anObject->attributes & kCanAcceptDestination)) {
        const StringSlice& object_name = get_object_name(anObject->whichBaseObject);
        int count = CountObjectsOfBaseType(anObject->whichBaseObject, anObject->owner) - 1;
        Messages::add(format(" {0} destroyed.  {1} remaining. ", object_name, count));
    }

    if (sObject->active == kObjectInUse) {
        execute_actions(
                sObject->baseType->collideAction,
                sObject->baseType->collideActionNum,
                sObject, anObject, NULL, true);
    }

    if (anObject->owner == globals()->gPlayerAdmiralNumber
            && (anObject->attributes & kIsHumanControlled)
            && (sObject->baseType->damage > 0)) {
        globals()->transitions.start_boolean(128, 128, WHITE);
    }
}


// GetManualSelectObject:
//  For the human player selecting a ship.  If friend or foe = 0, will get any ship.  If it's
//  positive, will get only friendly ships.  If it's negative, only unfriendly ships.

int32_t GetManualSelectObject(
        spaceObjectType *sourceObject, int32_t direction, uint32_t inclusiveAttributes,
        uint32_t anyOneAttribute, uint32_t exclusiveAttributes,
        const uint64_t* fartherThan, int32_t currentShipNum, int16_t friendOrFoe) {
    spaceObjectType *anObject;
    int32_t         whichShip = 0, resultShip = -1, closestShip = -1, startShip = -1, hdif, vdif;
    uint32_t        distance, dcalc, myOwnerFlag = 1 << sourceObject->owner;
    int32_t         difference;
    Fixed           slope;
    int16_t         angle;
//  const wide      kMaxAngleDistance = {0, 1073676289}; // kMaximumAngleDistance ^ 2
    uint64_t        wideClosestDistance, wideFartherDistance, thisWideDistance, wideScrap;
    uint8_t         thisDistanceState;

    wideClosestDistance = 0x3fffffff3fffffffull;
    wideFartherDistance = 0x3fffffff3fffffffull;

    // Here's what you've got to do next:
    // start with the currentShipNum
    // try to get any ship but the current ship
    // stop trying when we've made a full circle (we're back on currentShipNum)

    whichShip = startShip = currentShipNum;
    if ( whichShip >= 0)
    {
        anObject = mGetSpaceObjectPtr(startShip);
        if ( anObject->active != kObjectInUse) // if it's not in the loop
        {
            anObject = gRootObject;
            startShip = whichShip = gRootObjectNumber;
        }

    } else
    {
        anObject = gRootObject;
        startShip = whichShip = gRootObjectNumber;
    }

    do
    {
        if (( anObject->active) && ( anObject != sourceObject) &&
            ( anObject->seenByPlayerFlags & myOwnerFlag) &&
            (( anObject->attributes & inclusiveAttributes) == inclusiveAttributes) &&
            (( anyOneAttribute == 0) || (( anObject->attributes & anyOneAttribute) != 0)) &&
            ( !(anObject->attributes & exclusiveAttributes)) && ((( friendOrFoe < 0) &&
            ( anObject->owner != sourceObject->owner)) || (( friendOrFoe > 0) &&
            ( anObject->owner == sourceObject->owner)) || ( friendOrFoe == 0)))
        {
            difference = ABS<int>( sourceObject->location.h - anObject->location.h);
            dcalc = difference;
            difference =  ABS<int>( sourceObject->location.v - anObject->location.v);
            distance = difference;

            if (( dcalc > kMaximumRelevantDistance) ||
                ( distance > kMaximumRelevantDistance))
            {
                wideScrap = dcalc;   // must be positive
                MyWideMul( wideScrap, wideScrap, &thisWideDistance);  // ppc automatically generates WideMultiply
                wideScrap = distance;
                MyWideMul( wideScrap, wideScrap, &wideScrap);
                thisWideDistance += wideScrap;
            } else
            {
                thisWideDistance = distance * distance + dcalc * dcalc;
            }

            thisDistanceState = 0;
            if (wideClosestDistance > thisWideDistance) {
                thisDistanceState |= kCloserThanClosest;
            }

            if ((thisWideDistance > *fartherThan
                        && wideFartherDistance > thisWideDistance)
                    || (wideFartherDistance > thisWideDistance
                        && thisWideDistance >= *fartherThan
                        && whichShip > currentShipNum))
            {
                thisDistanceState |= kFartherThanFarther;
            }

            if ( thisDistanceState)
            {
                hdif = sourceObject->location.h - anObject->location.h;
                vdif = sourceObject->location.v - anObject->location.v;
                while (((ABS(hdif)) > kMaximumAngleDistance) || ( (ABS(vdif)) > kMaximumAngleDistance))
                {
                    hdif >>= 1;
                    vdif >>= 1;
                }

                slope = MyFixRatio(hdif, vdif);
                angle = AngleFromSlope( slope);

                if ( hdif > 0)
                    mAddAngle( angle, 180);
                else if (( hdif == 0) &&
                        ( vdif > 0))
                    angle = 0;

                angle = mAngleDifference( angle, direction);

                if ( ABS( angle) < 30)
                {
                    if ( thisDistanceState & kCloserThanClosest)
                    {
                        closestShip = whichShip;
                        wideClosestDistance = thisWideDistance;
                    }

                    if ( thisDistanceState & kFartherThanFarther)
                    {
                        resultShip = whichShip;
                        wideFartherDistance = thisWideDistance;
                    }
                }
            }
        }
        whichShip = anObject->nextObjectNumber;
        anObject = anObject->nextObject;
        if ( anObject == NULL)
        {
            whichShip = gRootObjectNumber;
            anObject = gRootObject;
        }
    } while ( whichShip != startShip);
    if ((( resultShip == -1) && ( closestShip != -1)) || ( resultShip == currentShipNum)) resultShip = closestShip;

    return ( resultShip);
}

int32_t GetSpritePointSelectObject( Rect *bounds, spaceObjectType *sourceObject, uint32_t inclusiveAttributes,
                            uint32_t anyOneAttribute, uint32_t exclusiveAttributes,
                            int32_t currentShipNum, int16_t friendOrFoe)

{
    int32_t         resultShip = -1, closestShip = -1;
    uint32_t        myOwnerFlag = 1 << sourceObject->owner;


    for (int32_t whichShip = 0; whichShip < kMaxSpaceObject; whichShip++) {
        spaceObjectType* anObject = mGetSpaceObjectPtr(whichShip);
        if (( anObject->active) && ( anObject->sprite != NULL) &&
            ( anObject->seenByPlayerFlags & myOwnerFlag) &&
            (( anObject->attributes & inclusiveAttributes) == inclusiveAttributes) &&
            (( anyOneAttribute == 0) || (( anObject->attributes & anyOneAttribute) != 0)) &&
            ( !(anObject->attributes & exclusiveAttributes)) && ((( friendOrFoe < 0) &&
            ( anObject->owner != sourceObject->owner)) || (( friendOrFoe > 0) &&
            ( anObject->owner == sourceObject->owner)) || ( friendOrFoe == 0)))
        {
/*          if ( !((bounds->right < anObject->sprite->thisRect.left) || (bounds->bottom <
                anObject->sprite->thisRect.top) || ( bounds->left > anObject->sprite->thisRect.right)
                || ( bounds->top > anObject->sprite->thisRect.bottom)))
            {
                if ( anObject->sprite->thisRect.right > anObject->sprite->thisRect.left)
                {
                    if ( closestShip < 0) closestShip = whichShip;
                    if (( whichShip > currentShipNum) && ( resultShip < 0)) resultShip = whichShip;
                }
            }
*/          if ( !((bounds->right < anObject->sprite->where.h) || (bounds->bottom <
                anObject->sprite->where.v) || ( bounds->left > anObject->sprite->where.h)
                || ( bounds->top > anObject->sprite->where.v)))
            {
//              if ( anObject->sprite->thisRect.right > anObject->sprite->thisRect.left)
                {
                    if ( closestShip < 0) closestShip = whichShip;
                    if (( whichShip > currentShipNum) && ( resultShip < 0)) resultShip = whichShip;
                }
            }
        }
    }
    if ((( resultShip == -1) && ( closestShip != -1)) || ( resultShip == currentShipNum)) resultShip = closestShip;

    return ( resultShip);
}

}  // namespace antares
