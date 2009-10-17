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

#include "Picture.hpp"

#include <assert.h>
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include "BinaryStream.hpp"
#include "FakeDrawing.hpp"

namespace antares {

extern PixMap* gActiveWorld;

Picture::Picture(int32_t id)
        : ArrayPixMap(0, 0) {
    char fileglob[64];
    glob_t g;
    g.gl_offs = 0;

    sprintf(fileglob, "pictures/%d.bin", id);
    glob(fileglob, 0, NULL, &g);
    sprintf(fileglob, "pictures/%d *.bin", id);
    glob(fileglob, GLOB_APPEND, NULL, &g);

    if (g.gl_pathc == 0) {
        throw PictureNotFoundException();
    } else if (g.gl_pathc == 1) {
        MappedFile file(g.gl_pathv[0]);
        BufferBinaryReader bin(file.data(), file.size());
        read(&bin);
        assert(bin.bytes_read() == file.size());
    } else {
        fprintf(stderr, "Found %lu matches for %d\n", g.gl_pathc, id);
        exit(1);
    }
    globfree(&g);
}

const Rect& Picture::frame() {
    return bounds();
}

void Picture::draw(const Rect& dst) {
    CopyBits(this, gActiveWorld, bounds(), dst);
}

void Picture::draw_to(PixMap* pix, const Rect& from, const Rect& to) {
    CopyBits(this, pix, from, to);
}

}  // namespace antares
