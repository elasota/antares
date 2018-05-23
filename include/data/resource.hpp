// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2017 The Antares Authors
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

#ifndef ANTARES_DATA_RESOURCE_HPP_
#define ANTARES_DATA_RESOURCE_HPP_

#include <stdint.h>
#include <pn/string>
#include <vector>

namespace antares {

class ArrayPixMap;
class InterfaceItem;
class NatePixTable;
class Texture;
struct FontData;
struct ReplayData;
struct SoundData;
struct SpriteData;

class Resource {
  public:
    static bool      exists(pn::string_view path);
    static pn::value procyon(pn::string_view path);

    static FontData                                    font(pn::string_view name);
    static Texture                                     font_image(pn::string_view name);
    static std::vector<std::unique_ptr<InterfaceItem>> interface(pn::string_view name);
    static SoundData                                   music(pn::string_view name);
    static ReplayData                                  replay(int id);
    static std::vector<int32_t>                        rotation_table();
    static SoundData                                   sound(pn::string_view name);
    static SpriteData                                  sprite_data(pn::string_view name);
    static ArrayPixMap                                 sprite_image(pn::string_view name);
    static ArrayPixMap                                 sprite_overlay(pn::string_view name);
    static std::vector<pn::string>                     strings(int id);
    static pn::string                                  text(int id);
    static Texture                                     texture(pn::string_view name);
    static Texture                                     texture(int16_t id);

    Resource() = delete;
};

}  // namespace antares

#endif  // ANTARES_DATA_RESOURCE_HPP_
