// Ares, a tactical space combat game.
// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

// Interface Handling.c

//
// Liaison between the application and Interface Drawing.  Takes in events ( key events, mouse
// down events), hilights and scrolls as needed, and returns results.  Also handles editable text.
//

#include "InterfaceHandling.hpp"

#include "AnyChar.hpp"
#include "AresGlobalType.hpp"
#include "BinaryStream.hpp"
#include "ColorTranslation.hpp"
#include "Debug.hpp"
#include "Error.hpp"
#include "KeyMapTranslation.hpp"
#include "OffscreenGWorld.hpp"
#include "PlayerInterfaceDrawing.hpp"
#include "PlayerInterfaceItems.hpp"
#include "SoundFX.hpp"              // for button on/off

namespace antares {

#define kMakeInterfaceItem      20

#define kInterfaceError         "\pINTF"
#define kInterfaceResFileName   "\p:Ares Data Folder:Ares Interfaces"

#define kInterfaceResourceType  'intr'

#define kClickLoopInfo          ( kRegisterBased | RESULT_SIZE( SIZE_CODE ( sizeof ( bool))) | REGISTER_RESULT_LOCATION( kRegisterD0))

#define kTargetScreenWidth      640
#define kTargetScreenHeight     480

#define mPlayButtonDown         PlayVolumeSound( kComputerBeep1, kMediumLoudVolume, kShortPersistence, kMustPlaySound)
#define mPlayButtonUp           PlayVolumeSound( kComputerBeep2, kMediumLowVolume, kShortPersistence, kMustPlaySound)
#define mPlayScreenSound        PlayVolumeSound( kComputerBeep3, kMediumLowVolume, kShortPersistence, kVeryLowPrioritySound)

extern long             WORLD_WIDTH, WORLD_HEIGHT;
extern PixMap*          gOffWorld;
extern PixMap*          gSaveWorld;

TypedHandle<interfaceItemType> gInterfaceItemData;
short               gInterfaceFileRefID = -1;
long                gInterfaceScreenHBuffer = 0, gInterfaceScreenVBuffer = 0;

ScopedOpenInterface::ScopedOpenInterface(int id) {
    OpenInterface(id);
}

ScopedOpenInterface::~ScopedOpenInterface() {
    CloseInterface();
}

int InterfaceHandlingInit() {
    gInterfaceScreenHBuffer = ( WORLD_WIDTH / 2) - ( kTargetScreenWidth / 2);
    gInterfaceScreenVBuffer = ( WORLD_HEIGHT / 2) - ( kTargetScreenHeight / 2);

    return kNoError;
}

void InterfaceHandlingCleanup( void)

{
    if (gInterfaceItemData.get() != nil) {
        gInterfaceItemData.destroy();
    }
//  CloseResFile( gInterfaceFileRefID);         // not needed-done automatically when program quits
}

int OpenInterface( short resID)

{
    interfaceItemType   *item;
    long                count, number;

    if (gInterfaceItemData.get() != nil) {
        gInterfaceItemData.destroy();
    }
    gInterfaceItemData.load_resource(kInterfaceResourceType, resID);
    if (gInterfaceItemData.get() == nil) {
        return RESOURCE_ERROR;
    }

    /*
    MoveHHi( gInterfaceItemData);
    HLock( gInterfaceItemData);
    */
    InvalidateInterfaceFunctions(); // if they've been set, they shouldn't be yet

    number = gInterfaceItemData.count();
    item = *gInterfaceItemData;
    for ( count = 0; count < number; count++)
    {
        item->bounds.left += gInterfaceScreenHBuffer;
        item->bounds.right += gInterfaceScreenHBuffer;
        item->bounds.top += gInterfaceScreenVBuffer;
        item->bounds.bottom += gInterfaceScreenVBuffer;

        item++;
    }

    return ( kNoError);
}

long AppendInterface( short resID, long relativeNumber, bool center)
{
    TypedHandle<interfaceItemType> appendData;
    long                count, originalNumber, number;
    interfaceItemType   *item, *relativeItem;

//#pragma unused( center)
    appendData.load_resource(kInterfaceResourceType, resID);
    if ((appendData.get() != nil) && (gInterfaceItemData.get() != nil)) {
        originalNumber = gInterfaceItemData.count();
        gInterfaceItemData.extend(appendData);

        number = gInterfaceItemData.count();
        item = *gInterfaceItemData + originalNumber;

        if ( relativeNumber < 0)
        {
            for ( count = originalNumber; count < number; count++)
            {
                item->bounds.left += gInterfaceScreenHBuffer;
                item->bounds.right += gInterfaceScreenHBuffer;
                item->bounds.top += gInterfaceScreenVBuffer;
                item->bounds.bottom += gInterfaceScreenVBuffer;

                item++;
            }
        } else if ( !center)
        {
            relativeItem = *gInterfaceItemData + relativeNumber;

            for ( count = originalNumber; count < number; count++)
            {
                item->bounds.left += relativeItem->bounds.left;
                item->bounds.right += relativeItem->bounds.left;
                item->bounds.top += relativeItem->bounds.top;
                item->bounds.bottom += relativeItem->bounds.top;

                item++;
            }
        }
    } else return( -1);

    if ( center)
    {
        Rect    tRect;

        relativeItem = *gInterfaceItemData + relativeNumber;
        tRect = relativeItem->bounds;
        CenterItemRangeInRect( &tRect, originalNumber, number);
    }
    return( number - originalNumber);
}

void ShortenInterface( long howMany)
{
    long    number;

    if (gInterfaceItemData.get() != nil) {
        number = gInterfaceItemData.count();

        if ( howMany <= number) {
            gInterfaceItemData.resize(number - howMany);
        }
    }
}

void CloseInterface() {
    if (gInterfaceItemData.get() != nil) {
        gInterfaceItemData.destroy();
    }
}

void DrawEntireInterface( void)

{
    long                number, count;
    interfaceItemType   *item;
    Rect                tRect;

    DrawInOffWorld();
    tRect = Rect(0, 0, WORLD_WIDTH, WORLD_HEIGHT);
    SetTranslateColorFore( BLACK);
    PaintRect(tRect);

    number = gInterfaceItemData.count();
    item = *gInterfaceItemData;

    for ( count = 0; count < number; count++)
    {
        DrawAnyInterfaceItem( item, gOffWorld, 0, 0);
        item++;
    }
    DrawInRealWorld();
    CopyOffWorldToRealWorld(tRect);
}

void DrawInterfaceRange( long from, long to, long withinItem)

{
    long                number, count;
    interfaceItemType   *item;
    Rect                tRect;

    DrawInOffWorld();
    if ( withinItem >= 0)
    {
        item = GetAnyInterfaceItemPtr( withinItem);
        tRect = item->bounds;
        SetTranslateColorFore( BLACK);
        PaintRect(tRect);
    }
    number = gInterfaceItemData.count();
    if ( from < number)
    {
        if ( to > number) to = number;
        item = *gInterfaceItemData + from;

        for ( count = from; count < to; count++)
        {
            DrawAnyInterfaceItem( item, gOffWorld, 0, 0);
            item++;
        }
        DrawInRealWorld();
        if ( withinItem >= 0)
            CopyOffWorldToRealWorld(tRect);
    }
}

void DrawAllItemsOfKind( interfaceKindType kind, bool sound, bool clearFirst,
                            bool showAtEnd)

{
    long                number, count;
    interfaceItemType   *item;
    Rect                tRect;

    DrawInOffWorld();
    tRect = Rect(0, 0, WORLD_WIDTH, WORLD_HEIGHT);
    SetTranslateColorFore( BLACK);
    if ( clearFirst)
        PaintRect(tRect);

    number = gInterfaceItemData.count();
    item = *gInterfaceItemData;

    for ( count = 0; count < number; count++)
    {
        if ( sound)
            mPlayScreenSound;
        if ( item->kind == kind)
        {
            if ( showAtEnd)
                DrawAnyInterfaceItem( item, gOffWorld, 0, 0);
            else
                DrawAnyInterfaceItemOffToOn( item);
        }
        item++;
    }
    DrawInRealWorld();
    if ( showAtEnd)
        CopyOffWorldToRealWorld(tRect);
}

void DrawAnyInterfaceItemOffToOn( interfaceItemType *item)

{
    Rect            bounds;

    GetAnyInterfaceItemGraphicBounds( item, &bounds);
    DrawInOffWorld();
    DrawAnyInterfaceItem( item, gOffWorld, 0, 0);
    DrawInRealWorld();
    CopyOffWorldToRealWorld(bounds);
}

void DrawAnyInterfaceItemSaveToOffToOn( interfaceItemType   *item)

{
    Rect            bounds;

    GetAnyInterfaceItemGraphicBounds( item, &bounds);
    DrawInSaveWorld();
    DrawAnyInterfaceItem( item, gSaveWorld, 0, 0);
    DrawInOffWorld();
    CopySaveWorldToOffWorld(bounds);
    DrawInRealWorld();
    CopyOffWorldToRealWorld(bounds);
}

void OffsetAllItems( long hoffset, long voffset)
{
    OffsetItemRange(hoffset, voffset, 0, gInterfaceItemData.count());
}

void OffsetItemRange( long hoffset, long voffset, long from, long to)
{
    long                number, count;
    interfaceItemType   *item;

    number = to - from;//GetHandleSize( gInterfaceItemData) / sizeof( interfaceItemType);
    item = *gInterfaceItemData + from;

    for ( count = 0; count < number; count++)
    {
        item->bounds.left += hoffset;
        item->bounds.right += hoffset;
        item->bounds.top += voffset;
        item->bounds.bottom += voffset;
        item++;
    }
}

void CenterAllItemsInRect( Rect *destRect)
{
    CenterItemRangeInRect(destRect, 0, gInterfaceItemData.count());
}

void CenterItemRangeInRect( Rect *destRect, long from, long to)
{
    long                number, count, hoffset, voffset;
    interfaceItemType   *item;
    Rect            itemsBounds(0x7fffffff, 0x7fffffff, 0, 0);

    number = to - from;//GetHandleSize( gInterfaceItemData) / sizeof( interfaceItemType);
    item = *gInterfaceItemData + from;

    // first calc the rect that encloses all the interface items
    for ( count = 0; count < number; count++)
    {
        if ( item->bounds.left < itemsBounds.left) itemsBounds.left = item->bounds.left;
        if ( item->bounds.right > itemsBounds.right) itemsBounds.right = item->bounds.right;
        if ( item->bounds.top < itemsBounds.top) itemsBounds.top = item->bounds.top;
        if (item->bounds.bottom > itemsBounds.bottom) itemsBounds.bottom = item->bounds.bottom;
        item++;
    }

    // then center it in the destRect

    hoffset =   (
                    (
                        (
                            (
                                destRect->right - destRect->left
                            )
                            / 2
                        )
                        + destRect->left
                    ) -
                    (
                        (
                            itemsBounds.right - itemsBounds.left
                        )
                        / 2
                    )
                ) - itemsBounds.left;
    voffset =   (
                    (
                        (
                            (
                                destRect->bottom - destRect->top
                            )
                            / 2
                        )
                        + destRect->top
                    ) -
                    (
                        (
                            itemsBounds.bottom - itemsBounds.top
                        )
                        / 2
                    )
                ) - itemsBounds.top;
//  OffsetAllItems( hoffset, voffset);
    OffsetItemRange( hoffset, voffset, from, to);
}

void InvalidateInterfaceFunctions( void)

{
    long                number, count;
    interfaceItemType   *item;

    number = gInterfaceItemData.count();
    item = *gInterfaceItemData;

    for ( count = 0; count < number; count++)
    {
        if ( item->kind == kListRect)
        {
            item->item.listRect.getListLength = nil;
            item->item.listRect.getItemString = nil;
            item->item.listRect.itemHilited = nil;
        }
        item++;
    }
}

short PtInInterfaceItem( Point where)
{
    long                number, count;
    interfaceItemType   *item;
    Rect                tRect;

    number = gInterfaceItemData.count();
    item = *gInterfaceItemData;

    for ( count = 0; count < number; count++)
    {
        GetAnyInterfaceItemGraphicBounds( item, &tRect);

        if (tRect.contains(where)) {
            if (( item->kind != kTabBox) && ( item->kind != kPictureRect))
            {
                return( count);
            }
        }
        item++;
    }

    return( -1);
}

short InterfaceMouseDown( Point where)

{
    long                number, count;
    interfaceItemType   *item;
    Rect                tRect;
    short               result = -1;

    number = gInterfaceItemData.count();
    item = *gInterfaceItemData;

    for ( count = 0; count < number; count++)
    {
//      LongRectToRect( &(item->bounds), &tRect);
        GetAnyInterfaceItemGraphicBounds( item, &tRect);

        if (tRect.contains(where)) {
            switch ( item->kind)
            {
                case kPlainButton:
                    if ( InterfaceButtonHit( item))
                        result = count;
                    return( result);
                    break;
                case kCheckboxButton:
                    if ( InterfaceCheckboxHit( item))
                        result = count;
                    return( result);
                    break;
                case kRadioButton:
                    if ( InterfaceRadioButtonHit( item))
                        result = count;
                    return( result);
                    break;
                case kTabBoxButton:
                    if ( InterfaceTabBoxButtonHit( item))
                        result = count;
                    return( result);
                    break;

                case kLabeledRect:
                    return( result);
                    break;
                case kListRect:
                    InterfaceListRectHit( item, where);
                    result = count;
                    return( result);
                    break;
                default:
                    break;
            }

        }
        item++;
    }
    return ( -1);
}

short InterfaceKeyDown( long message)

{
    long                number = 0, count = 0;
    interfaceItemType   *item;
    short               buttonKey;
    KeyMap              keyMap;
    bool             caughtKey = false;
    char                whichChar;
    long                keyCode;

    keyCode = message & keyCodeMask;
    keyCode >>= 8;
    keyCode += 1;
    whichChar = message & charCodeMask;
    if ( keyCode > 0)
    {
        number = gInterfaceItemData.count();
        item = *gInterfaceItemData;

/*      while ( !(( item->kind == kPlainButton) &&
                ( item->item.plainButton.key == keyNum)) && ( count < number))
*/
        // check plain buttons
        do
        {
            buttonKey = 0;
            switch( item->kind)
            {
                case kPlainButton:
                    if ( item->item.plainButton.status != kDimmed)
                        buttonKey = item->item.plainButton.key;
                    break;

                case kTabBoxButton:
                    if ( item->item.radioButton.status != kDimmed)
                        buttonKey = item->item.radioButton.key;
                    break;

                default:
                    buttonKey = 0;
                    break;
            }
            if ( keyCode == buttonKey)
                caughtKey = true;
            else
            {
                count++;
                item++;
            }
        } while ((!caughtKey) && ( count < number));

/*      while ( !(( item->kind == kPlainButton)
                && ( keyCode == item->item.plainButton.key)) && ( count < number))

        {
            count++;
            item++;
        }
*/

        if ( caughtKey)
        {

//          item->item.plainButton.status = kIH_Hilite;
            SetStatusOfAnyInterfaceItem( count, kIH_Hilite, false);
            DrawAnyInterfaceItemOffToOn( item);
            mPlayButtonDown;
            do
            {
                GetKeys( keyMap);
            } while ( GetKeyNumFromKeyMap( keyMap) == buttonKey);
//              item->item.plainButton.key);
//          item->item.plainButton.status = kActive;
            SetStatusOfAnyInterfaceItem( count, kActive, false);

            switch ( item->kind)
            {
                case kTabBoxButton:
                    item->item.radioButton.on = true;
                    break;

                default:
                    break;
            }

            DrawAnyInterfaceItemOffToOn( item);
            return( count);
        }

    }

    return( -1);
}

bool InterfaceButtonHit( interfaceItemType *button)

{
    Rect    tRect;
    Point   where;

//  LongRectToRect( &(button->bounds), &tRect);
    GetAnyInterfaceItemGraphicBounds( button, &tRect);

    if ( button->item.plainButton.status == kDimmed) return( false);

    while ( Button())
    {
        GetMouse( &where);
        if (tRect.contains(where)) {
            if ( button->item.plainButton.status != kIH_Hilite)
            {
                mPlayButtonDown;
                button->item.plainButton.status = kIH_Hilite;
                DrawAnyInterfaceItemOffToOn( button);
            }
        } else
        {
            if ( button->item.plainButton.status != kActive)
            {
                mPlayButtonUp;
                button->item.plainButton.status = kActive;
                DrawAnyInterfaceItemOffToOn( button);
            }
        }
    }
    if ( button->item.plainButton.status == kIH_Hilite)
    {
        button->item.plainButton.status = kActive;
        DrawAnyInterfaceItemOffToOn( button);
    }
    return tRect.contains(where);
}

bool InterfaceCheckboxHit( interfaceItemType *button)

{
    Rect    tRect;
    Point   where;

//  LongRectToRect( &(button->bounds), &tRect);
    GetAnyInterfaceItemGraphicBounds( button, &tRect);

    if ( button->item.checkboxButton.status == kDimmed) return( false);

    do
    {
        GetMouse( &where);
        if (tRect.contains(where)) {
            if ( button->item.checkboxButton.status != kIH_Hilite)
            {
                mPlayButtonDown;
                button->item.checkboxButton.status = kIH_Hilite;
                DrawAnyInterfaceItemOffToOn( button);
            }
        } else
        {
            if ( button->item.checkboxButton.status != kActive)
            {
                button->item.checkboxButton.status = kActive;
                DrawAnyInterfaceItemOffToOn( button);
            }
        }
    } while ( Button());
    if ( button->item.checkboxButton.status == kIH_Hilite)
    {
        button->item.checkboxButton.status = kActive;
    }
    GetMouse( &where);
    if (tRect.contains(where)) {
        if ( button->item.checkboxButton.on)
            button->item.checkboxButton.on = false;
        else button->item.checkboxButton.on = true;
        DrawAnyInterfaceItemOffToOn( button);
        return( true);
    } else
    {
        DrawAnyInterfaceItemOffToOn( button);
        return( false);
    }
}

bool InterfaceRadioButtonHit( interfaceItemType *button)

{
    Rect    tRect;
    Point   where;

//  LongRectToRect( &(button->bounds), &tRect);
    GetAnyInterfaceItemGraphicBounds( button, &tRect);

    if ( button->item.radioButton.status == kDimmed) return( false);

    if ( button->item.radioButton.on == false)
        button->item.radioButton.on = true;

    do
    {
        GetMouse( &where);
        if (tRect.contains(where)) {
            if ( button->item.radioButton.status != kIH_Hilite)
            {
                mPlayButtonDown;
                button->item.radioButton.status = kIH_Hilite;
                DrawAnyInterfaceItemOffToOn( button);
            }
        } else
        {
            if ( button->item.radioButton.status != kActive)
            {
                button->item.radioButton.status = kActive;
                DrawAnyInterfaceItemOffToOn( button);
            }
        }
    } while ( Button());
    if ( button->item.radioButton.status == kIH_Hilite)
    {
        button->item.radioButton.status = kActive;
    }
    DrawAnyInterfaceItemOffToOn( button);
//  return( MacPtInRect( where, &tRect));
    return(true);
}

bool InterfaceTabBoxButtonHit( interfaceItemType *button)

{
    Rect    tRect;
    Point   where;

    GetAnyInterfaceItemGraphicBounds( button, &tRect);

    if ( button->item.radioButton.status == kDimmed) return( false);

    if ( button->item.radioButton.on != false) return( false);

    do
    {
        GetMouse( &where);
        if (tRect.contains(where)) {
            if ( button->item.radioButton.status != kIH_Hilite)
            {
                mPlayButtonDown;
                button->item.radioButton.status = kIH_Hilite;
                DrawAnyInterfaceItemOffToOn( button);
            }
        } else
        {
            if ( button->item.radioButton.status != kActive)
            {
                button->item.radioButton.status = kActive;
                DrawAnyInterfaceItemOffToOn( button);
            }
        }
    } while ( Button());
    if ( button->item.radioButton.status == kIH_Hilite)
    {
        button->item.radioButton.status = kActive;
    }
    button->item.radioButton.on = true;
    DrawAnyInterfaceItemOffToOn( button);

    return(true);
}

void InterfaceListRectHit( interfaceItemType *listRect, Point where)

{
    Rect    tRect;
    short   lineHeight, whichHit;

    if ( listRect->item.listRect.getListLength != nil)
    {
        tRect = listRect->bounds;
        lineHeight = GetInterfaceFontHeight(listRect->style) + kInterfaceTextVBuffer;
        where.v -= tRect.top;
        whichHit = where.v / lineHeight + listRect->item.listRect.topItem;
        if ( whichHit >= (*(listRect->item.listRect.getListLength))())
            whichHit = -1;
        (*(listRect->item.listRect.itemHilited))( whichHit, true);
    }
}

/*
void DrawStringInInterfaceContent( short whichItem, anyCharType *s)

{
    interfaceItemType   *item;
    Rect                tRect;

    item = *gInterfaceItemData + (long)whichItem;

    LongRectToRect( &(item->bounds), &tRect);
    DefaultColors();
    PaintRect( &tRect);
    MoveTo( tRect.left + kInterfaceTextHBuffer, tRect.top + GetInterfaceFontHeight( item->style) +
                kInterfaceTextVBuffer);
    SetTranslateColorShadeFore( item->color, VERY_LIGHT);
    DrawInterfaceString( s, item->style);
}
*/

interfaceItemType *GetAnyInterfaceItemPtr( long whichItem)

{
    return ( *gInterfaceItemData + whichItem);
}

void SetStatusOfAnyInterfaceItem( short whichItem, interfaceItemStatusType status, bool drawNow)

{
    interfaceItemType       *item;

    item = *gInterfaceItemData + whichItem;

    switch ( item->kind)
    {
        case kPlainButton:
            item->item.plainButton.status = status;
            break;
        case kRadioButton:
        case kTabBoxButton:
            item->item.radioButton.status = status;
            break;
        case kCheckboxButton:
            item->item.checkboxButton.status = status;
            break;
        case kTextRect:
            item->item.textRect.visibleBounds = ( status == kActive);
            break;
        case kPictureRect:
            item->item.pictureRect.visibleBounds = ( status == kActive);
            break;
    }
    if ( drawNow)
        RefreshInterfaceItem( whichItem);
}


void SwitchAnyRadioOrCheckbox( short whichItem, bool turnOn)

{
    interfaceItemType       *item;

    item = *gInterfaceItemData + whichItem;



    if ( item->kind == kCheckboxButton)
        item->item.checkboxButton.on = turnOn;
    else if (( item->kind == kRadioButton) || ( item->kind == kTabBoxButton))
        item->item.radioButton.on = turnOn;

}

bool GetAnyRadioOrCheckboxOn( short whichItem)

{
    interfaceItemType       *item;

    item = *gInterfaceItemData + whichItem;

    if ( item->kind == kCheckboxButton)
        return( item->item.checkboxButton.on);
    else if ( item->kind == kRadioButton)
        return (item->item.radioButton.on);


    return( false);
}


void RefreshInterfaceItem( short whichItem)

{
    interfaceItemType   *item;
    Rect                tRect;

    item = *gInterfaceItemData + whichItem;
    GetAnyInterfaceItemGraphicBounds( item, &tRect);
    DrawInOffWorld();
    DefaultColors();
    PaintRect(tRect);
    DrawAnyInterfaceItemOffToOn( item);
}

void SetInterfaceListCallback(  short       whichItem,
                                short       (*getListLength)( void),
                                void        (*getItemString)(short, unsigned char*),
                                bool     (*itemHilited)( short, bool))
{
    interfaceItemType   *item;

    item = *gInterfaceItemData + whichItem;

    if ( item->kind == kListRect)
    {
        item->item.listRect.getListLength = getListLength;
        item->item.listRect.getItemString = getItemString;
        item->item.listRect.itemHilited = itemHilited;
//      item->item.listRect.hiliteItem = hiliteItem;
        item->item.listRect.topItem = 0;
    }

}

void SetButtonKeyNum( short whichItem, short whichKey)

{
    interfaceItemType   *item;

    item = *gInterfaceItemData + whichItem;

    if ( item->kind == kPlainButton)
    {
        item->item.plainButton.key = whichKey;
    }
}

short GetButtonKeyNum( short whichItem)

{
    interfaceItemType   *item;

    item = *gInterfaceItemData + whichItem;

    if ( item->kind == kPlainButton)
        return ( item->item.plainButton.key);
    else return( 0);
}

void SetInterfaceTextBoxText( short resID)
{
    static_cast<void>(resID);
}

size_t interfaceItemType::load_data(const char* data, size_t len) {
    BufferBinaryReader bin(data, len);
    char section[22];

    bin.read(&bounds);
    bin.read(section, 22);
    bin.read(&color);
    bin.read(&kind);
    bin.read(&style);
    bin.discard(1);

    BufferBinaryReader sub(section, 22);
    switch (kind) {
      case kPlainRect:
      case kPictureRect:
        sub.read(&item.pictureRect);
        break;

      case kLabeledRect:
        sub.read(&item.labeledRect);
        break;

      case kListRect:
        sub.read(&item.listRect);
        break;

      case kTextRect:
        sub.read(&item.textRect);
        break;

      case kPlainButton:
        sub.read(&item.plainButton);
        break;

      case kRadioButton:
      case kTabBoxButton:
        sub.read(&item.radioButton);
        break;

      case kCheckboxButton:
        sub.read(&item.checkboxButton);
        break;

      case kTabBox:
        sub.read(&item.tabBox);
        break;

      case kTabBoxTop:
        break;
    }

    return bin.bytes_read();
}

void interfaceLabelType::read(BinaryReader* bin) {
    bin->read(&stringID);
    bin->read(&stringNumber);
}

void interfaceLabeledRectType::read(BinaryReader* bin) {
    bin->read(&label);
    bin->read(&color);
    bin->discard(5);
    bin->read(&editable);
}

void interfaceListType::read(BinaryReader* bin) {
    bin->read(&label);
    bin->discard(12);
    bin->read(&topItem);
    bin->read(&lineUpStatus);
    bin->read(&lineDownStatus);
    bin->read(&pageUpStatus);
    bin->read(&pageDownStatus);

    getListLength = NULL;
    getItemString = NULL;
    itemHilited = NULL;
}

void interfaceTextRectType::read(BinaryReader* bin) {
    bin->read(&textID);
    bin->read(&visibleBounds);
}

void interfaceButtonType::read(BinaryReader* bin) {
    bin->read(&label);
    bin->read(&key);
    bin->read(&defaultButton);
    bin->read(&status);
}

void interfaceRadioType::read(BinaryReader* bin) {
    bin->read(&label);
    bin->read(&key);
    bin->read(&on);
    bin->read(&status);
}

void interfaceCheckboxType::read(BinaryReader* bin) {
    bin->read(&label);
    bin->read(&key);
    bin->read(&on);
    bin->read(&status);
}

void interfacePictureRectType::read(BinaryReader* bin) {
    bin->read(&pictureID);
    bin->read(&visibleBounds);
}

void interfaceTabBoxType::read(BinaryReader* bin) {
    bin->read(&topRightBorderSize);
}

}  // namespace antares
