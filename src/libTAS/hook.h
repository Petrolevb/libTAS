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

#ifndef LIBTAS_HOOK_H_INCLUDED
#define LIBTAS_HOOK_H_INCLUDED

#include "sdlversion.h"

namespace libtas {

/* Get access to a function from a substring of the library name
 * For example, if we want to access to the SDL_Init() function:
 *   void (*SDL_Init_real)(void);
 *   link_function((void**)&SDL_Init_real, "SDL_Init", "libSDL2-2");
 *
 * @param[out] function   pointer the function pointer we want to access
 * @param[in]  source     name of the function we want to access
 * @param[in]  library    substring of the name of the library which contains the function
 * @return                whether we successfully accessed to the function
 */
bool link_function(void** function, const char* source, const char* library, const char *version = nullptr);

/* Some macros to make the above function easier to use */

/* Declare the function pointer using decltype to deduce the
 * type of the function pointer from the function signature.
 */
#define DEFINE_ORIG_POINTER(FUNC) \
namespace orig { \
    static decltype(&FUNC) FUNC; \
}

#define LINK_NAMESPACE(FUNC,LIB) link_function((void**)&orig::FUNC, #FUNC, LIB)
#define LINK_NAMESPACE_GLOBAL(FUNC) LINK_NAMESPACE(FUNC, nullptr)
#define LINK_NAMESPACE_VERSION(FUNC,LIB,V) link_function((void**)&orig::FUNC, #FUNC, LIB, V)
#define LINK_NAMESPACE_SDL1(FUNC) LINK_NAMESPACE(FUNC,"libSDL-1.2")
#define LINK_NAMESPACE_SDL2(FUNC) LINK_NAMESPACE(FUNC,"libSDL2-2")
#define LINK_NAMESPACE_SDLX(FUNC) (get_sdlversion()==1)?LINK_NAMESPACE_SDL1(FUNC):LINK_NAMESPACE_SDL2(FUNC)


}

#endif
