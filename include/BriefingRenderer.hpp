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

#ifndef ANTARES_BRIEFING_RENDERER_HPP_
#define ANTARES_BRIEFING_RENDERER_HPP_

/******************************************\
|**| Briefing_Renderer.h
\******************************************/

#pragma mark **DEFINITIONS**
/******************************************\
|**| #defines
\******************************************/

/* - definitions
*******************************************/

#pragma mark _macros_
/* - macros
*******************************************/

#pragma mark **TYPEDEFS**
/******************************************\
|**| typedefs
\******************************************/

#pragma mark **PUBLIC PROTOTYPES**
/******************************************\
|**| public function prototypes
\******************************************/

void Briefing_Objects_Render( long whichScenario, PixMapHandle destmap,
            long maxSize, Rect *bounds, long portleft, long portright,
            coordPointType *corner, long scale);

void BriefPoint_Data_Get( long whichPoint, long whichScenario, long *headerID,
                    long *headerNumber, long *contentID, Rect *hiliteBounds,
                    coordPointType *corner, long scale, long minSectorSize, long maxSize,
                    Rect *bounds);

#endif // ANTARES_BRIEFING_RENDERER_HPP_
