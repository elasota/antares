// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2015-2017 The Antares Authors
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

#include <GLFW/glfw3.h>

#include <time.h>
#include <pn/file>
#include <sfz/sfz.hpp>

#include "config/dirs.hpp"
#include "config/file-prefs-driver.hpp"
#include "config/ledger.hpp"
#include "config/preferences.hpp"
#include "data/pn.hpp"
#include "data/scenario-list.hpp"
#include "game/sys.hpp"
#include "glfw/video-driver.hpp"
#include "sound/openal-driver.hpp"
#include "ui/flows/master.hpp"

using sfz::args::store;
using sfz::args::store_const;
using sfz::range;

namespace args = sfz::args;

namespace antares {

void main(int argc, const char* argv[]) {
    args::Parser parser(argv[0], "Runs Antares");

    sfz::String sfz_app_data(pn2sfz(default_application_path()));
    sfz::String sfz_scenario(kFactoryScenarioIdentifier);
    parser.add_argument("scenario", store(sfz_scenario)).help("select scenario");
    parser.add_argument("--help", help(parser, 0)).help("display this help screen");
    parser.add_argument("--app-data", store(sfz_app_data))
            .help(format("set path to application data (default: {0})", sfz_app_data));

    sfz::String error;
    if (!parser.parse_args(argc - 1, argv + 1, error)) {
        pn::format(stderr, "{0}: {1}\n", sfz2pn(parser.name()), sfz2pn(error));
        exit(1);
    }

    if (!sfz::path::isdir(pn2sfz(application_path()))) {
        if (sfz_app_data.empty()) {
            pn::format(stderr, "{0}: application data not installed\n\n", sfz2pn(parser.name()));
            pn::format(stderr, "Please install it, or specify a path with --app-data\n\n");
        } else {
            pn::format(
                    stderr, "{0}: application data not found at {1}\n\n", sfz2pn(parser.name()),
                    application_path());
        }
        exit(1);
    } else {
        set_application_path(sfz2pn(sfz_app_data));
    }

    FilePrefsDriver prefs;

    pn::string scenario = sfz2pn(sfz_scenario);
    sys.prefs->set_scenario_identifier(scenario);
    bool         have_scenario = false;
    ScenarioList l;
    for (auto i : range(l.size())) {
        const auto& entry = l.at(i);
        if (entry.identifier == scenario) {
            if (entry.installed) {
                have_scenario = true;
                break;
            } else {
                pn::format(
                        stderr, "{0}: factory scenario not installed\n\n", sfz2pn(parser.name()));
                pn::format(stderr, "Please run antares-install-data\n", sfz2pn(parser.name()));
                exit(1);
            }
        }
    }
    if (!have_scenario) {
        pn::format(stderr, "{0}: {1}: scenario not installed\n", sfz2pn(parser.name()), scenario);
        exit(1);
    }

    DirectoryLedger   ledger;
    OpenAlSoundDriver sound;
    GLFWVideoDriver   video;
    video.loop(new Master(time(NULL)));
}

}  // namespace antares

int main(int argc, const char* argv[]) {
    antares::main(argc, argv);
    return 0;
}
