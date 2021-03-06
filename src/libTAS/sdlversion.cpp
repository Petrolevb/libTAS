/*
    Copyright 2015-2016 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hook.h"
#include "logging.h"

namespace libtas {

namespace orig {
    void (*SDL_GetVersion)(SDL_version* ver);
    /* SDL 1.2 specific function */
    static SDL_version * (*SDL_Linked_Version)(void);
}

int get_sdlversion(void)
{
    static int SDLver = -1;

    if (SDLver != -1)
        return SDLver;

    /* Determine SDL version */
    SDL_version ver = {0, 0, 0};

    LINK_NAMESPACE_SDL2(SDL_GetVersion);

    if (orig::SDL_GetVersion) {
        orig::SDL_GetVersion(&ver);
    }
    else {
        LINK_NAMESPACE_SDL1(SDL_Linked_Version);

        if (orig::SDL_Linked_Version) {
            SDL_version *verp;
            verp = orig::SDL_Linked_Version();
            ver = *verp;
        }
    }

    if (ver.major > 0) {
        debuglog(LCF_SDL | LCF_HOOK, "Detected SDL ", static_cast<int>(ver.major), ".", static_cast<int>(ver.minor), ".", static_cast<int>(ver.patch));
    }

    /* We save the version major so that we can return it in a future calls */
    SDLver = ver.major;
    return SDLver;
}

}
