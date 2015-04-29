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

#include <fcntl.h>
#include <getopt.h>
#include <sfz/sfz.hpp>

#include "config/preferences.hpp"
#include "drawing/build-pix.hpp"
#include "drawing/color.hpp"
#include "drawing/pix-map.hpp"
#include "drawing/text.hpp"
#include "video/offscreen-driver.hpp"

using sfz::Optional;
using sfz::ScopedFd;
using sfz::String;
using sfz::StringSlice;
using sfz::args::help;
using sfz::args::store;
using sfz::dec;
using sfz::format;
using sfz::write;
using std::unique_ptr;

namespace io = sfz::io;
namespace utf8 = sfz::utf8;
namespace args = sfz::args;

namespace antares {
namespace {

class SizePix : public Card {
  public:
    SizePix(int id, int32_t width, int32_t* height): _id(id), _width(width), _height(height) { }
    virtual void become_front() { InitDirectText(); }
    virtual void draw() const { *_height = BuildPix(_id, _width).size().height; }

  private:
    const int _id;
    const int32_t _width;
    int32_t* const _height;
};

class DrawPix : public Card {
  public:
    DrawPix(int id, int32_t width): _id(id), _width(width) { }
    virtual void become_front() { InitDirectText(); }
    virtual void draw() const { BuildPix(_id, _width).draw({0, 0}); }

  private:
    const int _id;
    const int32_t _width;
};

class PixBuilder {
  public:
    PixBuilder(const Optional<String>& output_dir)
            : _output_dir(output_dir) { }

    void save(int id, int32_t width) {
        Size size = {width, 480};

        // One time to figure out the height of the output.
        // (we need an active, looping offscreen driver for this)
        {
            Preferences::preferences()->set_screen_size(size);
            OffscreenVideoDriver off(Preferences::preferences()->screen_size(), {});
            off.capture(new SizePix(id, width, &size.height), format("{0}.png", dec(id, 5)));
        }

        // One time for real, with a driver sized appropriately.
        {
            Preferences::preferences()->set_screen_size(size);
            OffscreenVideoDriver off(Preferences::preferences()->screen_size(), _output_dir);
            off.capture(new DrawPix(id, width), format("{0}.png", dec(id, 5)));
        }
    }

  private:
    const Optional<String> _output_dir;

    DISALLOW_COPY_AND_ASSIGN(PixBuilder);
};

int main(int argc, char* const* argv) {
    args::Parser parser(argv[0], "Builds all of the scrolling text images in the game");

    Optional<String> output_dir;
    parser.add_argument("-o", "--output", store(output_dir))
        .help("place output in this directory");
    parser.add_argument("-h", "--help", help(parser, 0))
        .help("display this help screen");

    String error;
    if (!parser.parse_args(argc - 1, argv + 1, error)) {
        print(io::err, format("{0}: {1}\n", parser.name(), error));
        exit(1);
    }

    if (output_dir.has()) {
        makedirs(*output_dir, 0755);
    }

    NullPrefsDriver prefs;

    PixBuilder builder(output_dir);
    builder.save(3020, 450);  // Gaitori prologue
    builder.save(3025, 450);  // Tutorial prologue
    builder.save(3080, 450);  // Cantharan prologue
    builder.save(3081, 450);  // Cantharan epilogue
    builder.save(3120, 450);  // Salrilian prologue
    builder.save(3211, 450);  // Game epilogue
    builder.save(4063, 450);  // Bazidanese prologue
    builder.save(4509, 450);  // Elejeetian prologue
    builder.save(4606, 450);  // Audemedon prologue
    builder.save(5600, 450);  // Story introduction
    builder.save(6500, 540);  // Credits text
    builder.save(6501, 450);  // Please register
    builder.save(10199, 450);  // Unused Gaitori prologue

    return 0;
}

}  // namespace
}  // namespace antares

int main(int argc, char** argv) {
    return antares::main(argc, argv);
}
