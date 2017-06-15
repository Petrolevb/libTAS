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

    Most of the code taken from DMTCP <http://dmtcp.sourceforge.net/>
 */

#ifndef LIBTAS_THREAD_INFO_H
#define LIBTAS_THREAD_INFO_H
#include <atomic>
#include <ucontext.h>
#include <pthread.h> // pthread_t

namespace libtas {
struct ThreadInfo {

    enum ThreadState {
        ST_RUNNING,
        ST_SIGNALED,
        ST_SUSPINPROG,
        ST_SUSPENDED,
        ST_ZOMBIE,
        ST_CKPNTHREAD
    };
    ThreadState state;
    pthread_t tid;
    void *(*start)(void *);
    void *arg;
    std::atomic<bool> go;
    bool detached;
    std::ptrdiff_t routine_id;
    ucontext_t savctx;

    ThreadInfo *next;
    ThreadInfo *prev;
};
}

#endif
