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

#include "frame.h"
#include "../shared/AllInputs.h"
#include "../shared/messages.h"
#include "global.h" // shared_config
#include "inputs/inputs.h" // AllInputs ai object
#include "inputs/inputevents.h"
#include "../shared/sockethelpers.h"
#include "logging.h"
#include "DeterministicTimer.h"
#include "AVEncoder.h"
#include "sdlwindows.h"
#include "sdlevents.h"
#include <iomanip>
#include "timewrappers.h" // clock_gettime
#include "threadwrappers.h" // isMainThread()
#include "checkpoint/ThreadManager.h"
#include "ScreenCapture.h"
#include "WindowTitle.h"
#include "EventQueue.h"

namespace libtas {

#define SAVESTATE_FILESIZE 1024
static char savestatepath[SAVESTATE_FILESIZE];

/* Store the number of nondraw frames */
static unsigned int nondraw_frame_counter = 0;

#ifdef LIBTAS_ENABLE_HUD
static void receive_messages(std::function<void()> draw, RenderHUD& hud);
#else
static void receive_messages(std::function<void()> draw);
#endif

/* Compute real and logical fps */
static void computeFPS(float& fps, float& lfps)
{
    /* Do We have enough values to compute fps? */
    static bool can_output = false;

    /* Frequency of FPS computing (every n frames) */
    static const int fps_refresh_freq = 10;

    /* Computations include values from past n calls */
    static const int history_length = 10;

    static std::array<TimeHolder, history_length> lastTimes;
    static std::array<TimeHolder, history_length> lastTicks;

    static int refresh_counter = 0;
    static int compute_counter = 0;

    if (++refresh_counter >= fps_refresh_freq) {
        refresh_counter = 0;

        /* Update current time */
        TimeHolder lastTime = lastTimes[compute_counter];
        NATIVECALL(clock_gettime(CLOCK_MONOTONIC, &lastTimes[compute_counter]));

        /* Update current ticks */
        TimeHolder lastTick = lastTicks[compute_counter];
        lastTicks[compute_counter] = detTimer.getTicks();

        /* Compute real fps (number of drawn screens per second) */
        TimeHolder deltaTime = lastTimes[compute_counter] - lastTime;

        /* Compute logical fps (number of drawn screens per timer second) */
        TimeHolder deltaTicks = lastTicks[compute_counter] - lastTick;

        if (++compute_counter >= history_length) {
            compute_counter = 0;
            can_output = true;
        }

        if (can_output) {
            fps = static_cast<float>(fps_refresh_freq*history_length) * 1000000000.0f / (deltaTime.tv_sec * 1000000000.0f + deltaTime.tv_nsec);
            lfps = static_cast<float>(fps_refresh_freq*history_length) * 1000000000.0f / (deltaTicks.tv_sec * 1000000000.0f + deltaTicks.tv_nsec);
        }
    }
}

/* Deciding if we actually draw the frame */
static bool skipDraw(float fps)
{
    static unsigned int skip_counter = 0;
    if (shared_config.fastforward) {
        unsigned int skip_freq = 1;

        /* I want to display about 16 effective frames per second, so I divide
         * the fps value by 16 to get the skip frequency.
         * Moreover, it is better to have bands of the same skip frequency, so
         * I take the next highest power of 2.
         * Because the value is already in a float, I can use this neat trick from
         * http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
         */

        if (fps > 1) {
            fps--;
            memcpy(&skip_freq, &fps, sizeof(int));
            skip_freq = 1U << ((skip_freq >> 23) - 126 - 4);
        }

        if (++skip_counter >= skip_freq) {
            skip_counter = 0;
            return false;
        }

        return true;
    }

    return false;
}

#ifdef LIBTAS_ENABLE_HUD
void frameBoundary(bool drawFB, std::function<void()> draw, RenderHUD& hud)
#else
void frameBoundary(bool drawFB, std::function<void()> draw)
#endif
{
    static float fps, lfps = 0;

    debuglog(LCF_FRAME, "Enter frame boundary");
    ThreadManager::setMainThread();

    if (is_exiting) {
        debuglog(LCF_FRAME, "Game is exiting: skipping frame boundary");
        detTimer.flushDelay();
        NATIVECALL(draw());
        return;
    }

    if (!drawFB)
        nondraw_frame_counter++;

    // if (!ThreadManager::isMainThread())
    //     debuglog(LCF_ERROR | LCF_FRAME, "Warning! Entering a frame boudary from a secondary thread!");

    detTimer.enterFrameBoundary();

    /* Decide if we skip drawing to the screen because of fastforward. We must
     * call it once per frame boundary, because it raises an internal counter.
     */
    bool skipping_draw = skipDraw(fps);

    /* Saving the screen pixels before drawing. This is done before rendering
     * the HUD, so that we can redraw with another HUD.
     *
     * TODO: What should we do for nondraw frames ???
     */
    if (!skipping_draw) {
        if (drawFB && shared_config.save_screenpixels) {
            ScreenCapture::getPixels(nullptr, nullptr);
        }
    }

#ifdef LIBTAS_ENABLE_HUD
    if (shared_config.osd_encode) {
        if (shared_config.osd & SharedConfig::OSD_FRAMECOUNT) {
            hud.renderFrame(frame_counter);
            // hud.renderNonDrawFrame(nondraw_frame_counter);
        }
        if (shared_config.osd & SharedConfig::OSD_INPUTS)
            hud.renderInputs(ai);
    }
#endif

    /* Audio mixing is done above, so encode must be called after */
#ifdef LIBTAS_ENABLE_AVDUMPING
    /* Dumping audio and video */
    if (shared_config.av_dumping) {

        /* First, create the AVEncoder is needed */
        if (!avencoder) {
            debuglog(LCF_DUMP, "Start AV dumping on file ", AVEncoder::dumpfile);
            avencoder.reset(new AVEncoder(gameWindow, frame_counter));
        }

        /* Write the current frame */
        int enc = avencoder->encodeOneFrame(frame_counter, drawFB);
        if (enc < 0) {
            /* Encode failed, disable AV dumping */
            avencoder.reset(nullptr);
            debuglog(LCF_ALERT, "Encoding to ", AVEncoder::dumpfile, " failed because:\n", avencoder->getErrorMsg());
            shared_config.av_dumping = false;
            sendMessage(MSGB_ENCODE_FAILED);
        }
    }
    else {
        /* If there is still an encoder object, it means we just stopped
         * encoding, so we must delete the encoder object.
         */
        if (avencoder) {
            debuglog(LCF_DUMP, "Stop AV dumping");
            avencoder.reset(nullptr);
        }
    }
#endif

#ifdef LIBTAS_ENABLE_HUD
    if (!shared_config.osd_encode) {
        if (shared_config.osd & SharedConfig::OSD_FRAMECOUNT) {
            hud.renderFrame(frame_counter);
            // hud.renderNonDrawFrame(nondraw_frame_counter);
        }
        if (shared_config.osd & SharedConfig::OSD_INPUTS)
            hud.renderInputs(ai);
    }
#endif

    if (!skipping_draw) {
        NATIVECALL(draw());
    }

    /* Send error messages */
    std::string alert;
    while (getAlertMsg(alert)) {
        sendMessage(MSGB_ALERT_MSG);
        sendString(alert);
    }

    /* Send framecount and internal time */
    sendMessage(MSGB_FRAMECOUNT_TIME);
    sendData(&frame_counter, sizeof(unsigned long));
    struct timespec ticks = detTimer.getTicks();
    sendData(&ticks, sizeof(struct timespec));

    /* Send GameInfo struct if needed */
    if (game_info.tosend) {
        sendMessage(MSGB_GAMEINFO);
        sendData(&game_info, sizeof(game_info));
        game_info.tosend = false;
    }

    /* Send fps and lfps values */
    sendMessage(MSGB_FPS);
    sendData(&fps, sizeof(float));
    sendData(&lfps, sizeof(float));

    /* Last message to send */
    sendMessage(MSGB_START_FRAMEBOUNDARY);

#ifdef LIBTAS_ENABLE_HUD
    receive_messages(draw, hud);
#else
    receive_messages(draw);
#endif

    if ((game_info.video & GameInfo::SDL1) || (game_info.video & GameInfo::SDL2)) {
        /* Push native SDL events into our emulated event queue */
        pushNativeEvents();
    }

    /* Push generated events.
     * This must be done after getting the new inputs. */
    generateKeyUpEvents();
    generateKeyDownEvents();
    if (frame_counter == 0)
        generateControllerAdded();
    generateControllerEvents();
    generateMouseMotionEvents();
    generateMouseButtonEvents();

    ++frame_counter;

    /* Print FPS */
    if (drawFB) {
        computeFPS(fps, lfps);
        debuglog(LCF_FRAME, "fps: ", std::fixed, std::setprecision(1), fps, " lfps: ", lfps);
    }
    WindowTitle::update(fps, lfps);

    detTimer.exitFrameBoundary();
    debuglog(LCF_FRAME, "Leave frame boundary");
}

static void pushQuitEvent(void)
{
    if (game_info.video & GameInfo::SDL1) {
        SDL1::SDL_Event ev;
        ev.type = SDL1::SDL_QUIT;
        sdlEventQueue.insert(&ev);
    }

    else if (game_info.video & GameInfo::SDL2) {
        SDL_Event ev;
        ev.type = SDL_QUIT;
        sdlEventQueue.insert(&ev);
    }

    else {
        GlobalNoLog gnl;
        XEvent xev;
        xev.xclient.type = ClientMessage;
        xev.xclient.window = gameXWindow;
        xev.xclient.message_type = XInternAtom(gameDisplay, "WM_PROTOCOLS", true);
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = XInternAtom(gameDisplay, "WM_DELETE_WINDOW", False);
        xev.xclient.data.l[1] = CurrentTime;
        OWNCALL(XSendEvent(gameDisplay, gameXWindow, False, NoEventMask, &xev));
    }
}

#ifdef LIBTAS_ENABLE_HUD
static void receive_messages(std::function<void()> draw, RenderHUD& hud)
#else
static void receive_messages(std::function<void()> draw)
#endif
{
    while (1)
    {
        int message = receiveMessage();
        AllInputs preview_ai;

        switch (message)
        {
            case MSGN_USERQUIT:
                pushQuitEvent();
                is_exiting = true;
                break;

            case MSGN_CONFIG:
                receiveData(&shared_config, sizeof(SharedConfig));
                break;

#ifdef LIBTAS_ENABLE_AVDUMPING
            case MSGN_DUMP_FILE:
                debuglog(LCF_SOCKET | LCF_FRAME, "Receiving dump filename");
                receiveCString(AVEncoder::dumpfile);
                debuglog(LCF_SOCKET | LCF_FRAME, "File ", AVEncoder::dumpfile);
                break;
#endif
            case MSGN_ALL_INPUTS:
                receiveData(&ai, sizeof(AllInputs));
                break;

            case MSGN_EXPOSE:
                if (shared_config.save_screenpixels) {
                    ScreenCapture::setPixels(false);
                    NATIVECALL(draw());
                }
                break;

            case MSGN_PREVIEW_INPUTS:
                receiveData(&preview_ai, sizeof(AllInputs));

#ifdef LIBTAS_ENABLE_HUD
                if ((shared_config.osd & SharedConfig::OSD_INPUTS) && shared_config.save_screenpixels) {
                    ScreenCapture::setPixels(false);

                    if (shared_config.osd & SharedConfig::OSD_FRAMECOUNT) {
                        hud.renderFrame(frame_counter);
                        // hud.renderNonDrawFrame(nondraw_frame_counter);
                    }
                    hud.renderInputs(ai);
                    hud.renderPreviewInputs(preview_ai);

                    NATIVECALL(draw());
                }
#endif

                break;

            case MSGN_SAVESTATE:
                /* Get the savestate path */
                receiveCString(savestatepath);

                ThreadManager::checkpoint(savestatepath);

                /* Don't forget that when we load a savestate, the game continues
                 * from here and not from ThreadManager::restore() under.
                 * To check if we did restored or returned from a checkpoint,
                 * we look at variable ThreadManager::restoreInProgress.
                 */
                if (ThreadManager::restoreInProgress) {
                    /* Tell the program that the loading succeeded */
                    sendMessage(MSGB_LOADING_SUCCEEDED);

                    /* After loading, the game and the program no longer store
                     * the same information, so they must communicate to be
                     * synced again.
                     */

                    /* We receive the shared config struct */
                    message = receiveMessage();
                    MYASSERT(message == MSGN_CONFIG)
                    receiveData(&shared_config, sizeof(SharedConfig));

                    /* We must send again the frame count and time because it
                     * probably has changed.
                     */
                    sendMessage(MSGB_FRAMECOUNT_TIME);
                    sendData(&frame_counter, sizeof(unsigned long));
                    struct timespec ticks = detTimer.getTicks();
                    sendData(&ticks, sizeof(struct timespec));

                    if (shared_config.save_screenpixels) {
                        /* If we restored from a savestate, refresh the screen */

                        ScreenCapture::setPixels(true);

#ifdef LIBTAS_ENABLE_HUD
                        if (shared_config.osd & SharedConfig::OSD_FRAMECOUNT) {
                            hud.renderFrame(frame_counter);
                            // hud.renderNonDrawFrame(nondraw_frame_counter);
                        }
                        if (shared_config.osd & SharedConfig::OSD_INPUTS)
                            hud.renderInputs(ai);
#endif

                        NATIVECALL(draw());
                    }

                }

                break;

            case MSGN_LOADSTATE:
                /* Get the savestate path */
                receiveCString(savestatepath);

                ThreadManager::restore(savestatepath);

                /* If restoring failed, we return here. We still send the
                 * frame count and time because the program will pull a
                 * message in either case.
                 */
                sendMessage(MSGB_FRAMECOUNT_TIME);
                sendData(&frame_counter, sizeof(unsigned long));
                {
                    struct timespec ticks = detTimer.getTicks();
                    sendData(&ticks, sizeof(struct timespec));
                }

                break;

            case MSGN_STOP_ENCODE:
#ifdef LIBTAS_ENABLE_AVDUMPING
                if (avencoder) {
                    debuglog(LCF_DUMP, "Stop AV dumping");
                    avencoder.reset(nullptr);
                    shared_config.av_dumping = false;

                    /* Update title without changing fps */
                    WindowTitle::update(-1, -1);
                }
#endif
                break;
            case MSGN_END_FRAMEBOUNDARY:
                return;

            default:
                debuglog(LCF_ERROR | LCF_SOCKET | LCF_FRAME, "Unknown message received");
                return;
        }
    }
}

}
