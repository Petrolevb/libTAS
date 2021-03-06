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
#include "dlhook.h"
#include "logging.h"
#include <string>

namespace libtas {

bool link_function(void** function, const char* source, const char* library, const char *version /*= nullptr*/)
{
    /* Test if function is already linked */
    if (*function != nullptr)
        return true;

    /* Initialize the pointers to use real dl functions */
    dlhook_init();

    dlenter();
    /* From this function dl* call will refer to real dl functions */

    /* First try to link it from the global namespace */
    if (version)
        *function = dlvsym(RTLD_NEXT, source, version);
    else
        *function = dlsym(RTLD_NEXT, source);

    if (*function != nullptr) {

        /* Now checking that this function does not come from our library.
         * Otherwise this would usually lead to a call loop.
         */
        Dl_info info;
        int res = dladdr(*function, &info);
        if (res != 0) {
            std::string libpath = info.dli_fname;
            std::string libtasstr = "libTAS.so"; // bad!
            if (libpath.length() >= libtasstr.length() &&
                libpath.compare(libpath.length()-libtasstr.length(), libtasstr.length(), libtasstr) == 0) {

                debuglog(LCF_ERROR | LCF_HOOK, "   we could not find the real function ", source, " using RTLD_NEXT. Trying RTLD_DEFAULT");

                if (version)
                    *function = dlvsym(RTLD_NEXT, source, version);
                else
                    *function = dlsym(RTLD_NEXT, source);
            }
        }

        dlleave();
        debuglog(LCF_HOOK, "Imported symbol ", source, " function : ", *function);
        return true;
    }

    if (library != nullptr) {

        /* If it did not succeed, try to link using a matching library
         * loaded by the game.
         */
        std::string libpath = find_lib(library);

        if (! libpath.empty()) {

            /* Try to link again using a matching library */
            void* handle = dlopen(libpath.c_str(), RTLD_LAZY);

            if (handle != NULL) {
                *function = dlsym(handle, source);

                if (*function != nullptr) {
                    dlleave();
                    debuglog(LCF_HOOK, "Imported from lib ", libpath, " symbol ", source, " function : ", *function);
                    return true;
                }
            }
        }

        /* If it did not succeed, try to link using the given library */
        void* handle = dlopen(library, RTLD_LAZY);

        if (handle != NULL) {
            *function = dlsym(handle, source);

            if (*function != nullptr) {
                dlleave();
                debuglog(LCF_HOOK, "Imported from lib ", library, " symbol ", source, " function : ", *function);
                return true;
            }
        }

    }
    debuglogstdio(LCF_ERROR | LCF_HOOK, "Could not import symbol %s", source);

    *function = nullptr;
    dlleave();
    return false;
}

}
