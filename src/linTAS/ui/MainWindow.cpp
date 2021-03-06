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

#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QApplication>

#include "MainWindow.h"
#include "../MovieFile.h"
#include "ErrorChecking.h"
#include "../../shared/version.h"

#include <iostream>
#include <future>
#include <sys/stat.h>

MainWindow::MainWindow(Context* c) : QMainWindow(), context(c)
{
    QString title = QString("libTAS v%1.%2.%3").arg(MAJORVERSION).arg(MINORVERSION).arg(PATCHVERSION);
    setWindowTitle(title);

    /* Create the object that will launch and communicate with the game,
     * and connect all the signals.
     */
    gameLoop = new GameLoop(context);
    connect(gameLoop, &GameLoop::statusChanged, this, &MainWindow::updateStatus);
    connect(gameLoop, &GameLoop::configChanged, this, &MainWindow::updateUIFromConfig);
    connect(gameLoop, &GameLoop::alertToShow, this, &MainWindow::alertDialog);
    connect(gameLoop, &GameLoop::startInnerLoop, this, &MainWindow::updateRam);
    connect(gameLoop, &GameLoop::rerecordChanged, this, &MainWindow::updateRerecordCount);
    connect(gameLoop, &GameLoop::frameCountChanged, this, &MainWindow::updateFrameCountTime);
    connect(gameLoop, &GameLoop::sharedConfigChanged, this, &MainWindow::updateSharedConfigChanged);
    connect(gameLoop, &GameLoop::fpsChanged, this, &MainWindow::updateFps);
    connect(gameLoop, &GameLoop::askMovieSaved, this, &MainWindow::alertSave);

    /* Create other windows */
#ifdef LIBTAS_ENABLE_AVDUMPING
    encodeWindow = new EncodeWindow(c, this);
#endif
    inputWindow = new InputWindow(c, this);
    executableWindow = new ExecutableWindow(c, this);
    controllerTabWindow = new ControllerTabWindow(c, this);
    gameInfoWindow = new GameInfoWindow(c, this);
    ramSearchWindow = new RamSearchWindow(c, this);
    ramWatchWindow = new RamWatchWindow(c, this);

    /* Menu */
    createActions();
    createMenus();

    /* Movie File */
    moviePath = new QLineEdit();
    moviePath->setReadOnly(true);

    browseMoviePath = new QPushButton("Browse...");
    connect(browseMoviePath, &QAbstractButton::clicked, this, &MainWindow::slotBrowseMoviePath);
    disabledWidgetsOnStart.append(browseMoviePath);

    movieRecording = new QRadioButton("Recording");
    connect(movieRecording, &QAbstractButton::clicked, this, &MainWindow::slotMovieRecording);
    moviePlayback = new QRadioButton("Playback");
    connect(moviePlayback, &QAbstractButton::clicked, this, &MainWindow::slotMovieRecording);

    /* Frame count */
    frameCount = new QSpinBox();
    frameCount->setReadOnly(true);
    frameCount->setMaximum(1000000000);

    movieFrameCount = new QSpinBox();
    movieFrameCount->setReadOnly(true);
    movieFrameCount->setMaximum(1000000000);

    /* Current/movie length */
    currentLength = new QLabel("Current time: -");
    movieLength = new QLabel("Movie length: -");

    /* Frames per second */
    logicalFps = new QSpinBox();
    logicalFps->setMaximum(100000);
    disabledWidgetsOnStart.append(logicalFps);

    fpsValues = new QLabel("Current FPS: - / -");

    /* Re-record count */
    rerecordCount = new QSpinBox();
    rerecordCount->setReadOnly(true);
    rerecordCount->setMaximum(1000000000);

    /* Initial time */
    initialTimeSec = new QSpinBox();
    initialTimeSec->setMaximum(1000000000);
    initialTimeSec->setMinimumWidth(50);
    initialTimeNsec = new QSpinBox();
    initialTimeNsec->setMaximum(1000000000);
    initialTimeNsec->setMinimumWidth(50);
    disabledWidgetsOnStart.append(initialTimeSec);
    disabledWidgetsOnStart.append(initialTimeNsec);

    /* Pause/FF */
    pauseCheck = new QCheckBox("Pause");
    connect(pauseCheck, &QAbstractButton::clicked, this, &MainWindow::slotPause);
    fastForwardCheck = new QCheckBox("Fast-forward");
    connect(fastForwardCheck, &QAbstractButton::clicked, this, &MainWindow::slotFastForward);

    /* Game Executable */
    gamePath = new QLineEdit();
    gamePath->setReadOnly(true);
    gamePath->setMinimumWidth(400);

    browseGamePath = new QPushButton("Browse...");
    connect(browseGamePath, &QAbstractButton::clicked, this, &MainWindow::slotBrowseGamePath);
    disabledWidgetsOnStart.append(browseGamePath);

    // gamepathchooser = new Fl_Native_File_Chooser();
    // gamepathchooser->title("Game path");

    /* Command-line options */
    cmdOptions = new QLineEdit();
    disabledWidgetsOnStart.append(cmdOptions);

    /* Buttons */
    QPushButton *launchButton = new QPushButton(tr("Start"));
    connect(launchButton, &QAbstractButton::clicked, this, &MainWindow::slotLaunch);
    disabledWidgetsOnStart.append(launchButton);

    launchGdbButton = new QPushButton(tr("Start and attach gdb"));
    connect(launchGdbButton, &QAbstractButton::clicked, this, &MainWindow::slotLaunch);
    disabledWidgetsOnStart.append(launchGdbButton);

    stopButton = new QPushButton(tr("Stop"));
    connect(stopButton, &QAbstractButton::clicked, this, &MainWindow::slotStop);

    QDialogButtonBox *buttonBox = new QDialogButtonBox();
    buttonBox->addButton(launchButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(launchGdbButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(stopButton, QDialogButtonBox::ActionRole);


    /* Layouts */


    /* Game parameters layout */
    QGroupBox *gameBox = new QGroupBox(tr("Game execution"));
    QGridLayout *gameLayout = new QGridLayout;
    gameLayout->addWidget(new QLabel(tr("Game executable")), 0, 0);
    gameLayout->addWidget(gamePath, 0, 1);
    gameLayout->addWidget(browseGamePath, 0, 2);
    gameLayout->addWidget(new QLabel(tr("Command-line options")), 1, 0);
    gameLayout->addWidget(cmdOptions, 1, 1);
    gameBox->setLayout(gameLayout);

    /* Movie layout */
    movieBox = new QGroupBox(tr("Movie recording"));
    movieBox->setCheckable(true);
    connect(movieBox, &QGroupBox::clicked, this, &MainWindow::slotMovieEnable);

    QVBoxLayout *movieLayout = new QVBoxLayout;

    QHBoxLayout *movieFileLayout = new QHBoxLayout;
    movieFileLayout->addWidget(new QLabel(tr("Movie file:")));
    movieFileLayout->addWidget(moviePath);
    movieFileLayout->addWidget(browseMoviePath);

    QGridLayout *movieCountLayout = new QGridLayout;
    movieCountLayout->addWidget(new QLabel(tr("Movie frame count:")), 0, 0);
    movieCountLayout->addWidget(movieFrameCount, 0, 1);
    movieCountLayout->addWidget(movieLength, 0, 3);
    movieCountLayout->addWidget(new QLabel(tr("Rerecord count:")), 1, 0);
    movieCountLayout->addWidget(rerecordCount, 1, 1);
    movieCountLayout->setColumnMinimumWidth(2, 50);

    QGroupBox *movieStatusBox = new QGroupBox(tr("Movie status"));
    QHBoxLayout *movieStatusLayout = new QHBoxLayout;
    movieStatusLayout->addWidget(movieRecording);
    movieStatusLayout->addWidget(moviePlayback);
    movieStatusLayout->addStretch(1);
    movieStatusBox->setLayout(movieStatusLayout);

    movieLayout->addLayout(movieFileLayout);
    movieLayout->addLayout(movieCountLayout);
    movieLayout->addWidget(movieStatusBox);
    movieBox->setLayout(movieLayout);

    /* General layout */
    QGroupBox *generalBox = new QGroupBox(tr("General options"));
    QVBoxLayout *generalLayout = new QVBoxLayout;

    QGridLayout *generalFrameLayout = new QGridLayout;
    generalFrameLayout->addWidget(new QLabel(tr("Frame:")), 0, 0);
    generalFrameLayout->addWidget(frameCount, 0, 1);
    generalFrameLayout->addWidget(currentLength, 0, 3);
    generalFrameLayout->addWidget(new QLabel(tr("Frames per second:")), 1, 0);
    generalFrameLayout->addWidget(logicalFps, 1, 1);
    generalFrameLayout->addWidget(fpsValues, 1, 3);
    generalFrameLayout->setColumnMinimumWidth(2, 50);

    QHBoxLayout *generalTimeLayout = new QHBoxLayout;
    generalTimeLayout->addWidget(new QLabel(tr("System time:")));
    generalTimeLayout->addStretch(1);
    generalTimeLayout->addWidget(initialTimeSec);
    generalTimeLayout->addWidget(new QLabel(tr("sec")));
    generalTimeLayout->addStretch(1);
    generalTimeLayout->addWidget(initialTimeNsec);
    generalTimeLayout->addWidget(new QLabel(tr("nsec")));
    generalTimeLayout->addStretch(1);

    QHBoxLayout *generalControlLayout = new QHBoxLayout;
    generalControlLayout->addWidget(pauseCheck);
    generalControlLayout->addWidget(fastForwardCheck);
    generalControlLayout->addStretch(1);

    generalLayout->addLayout(generalFrameLayout);
    generalLayout->addLayout(generalTimeLayout);
    generalLayout->addLayout(generalControlLayout);
    generalBox->setLayout(generalLayout);

    /* Create the main layout */
    QVBoxLayout *mainLayout = new QVBoxLayout;

    mainLayout->addWidget(gameBox);
    mainLayout->addStretch(1);
    mainLayout->addWidget(movieBox);
    mainLayout->addStretch(1);
    mainLayout->addWidget(generalBox);
    mainLayout->addStretch(1);
    mainLayout->addWidget(buttonBox);

    QWidget *centralWidget = new QWidget;
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    updateUIFromConfig();
}

MainWindow::~MainWindow()
{
    if (game_thread.joinable())
        game_thread.detach();
}

/* We are going to do this a lot, so this is a helper function to insert
 * checkable actions into an action group with data.
 */
void MainWindow::addActionCheckable(QActionGroup*& group, const QString& text, const QVariant &qdata)
{
    QAction *action = group->addAction(text);
    action->setCheckable(true);
    action->setData(qdata);
}

void MainWindow::createActions()
{
    movieEndGroup = new QActionGroup(this);
    connect(movieEndGroup, &QActionGroup::triggered, this, &MainWindow::slotMovieEnd);

    addActionCheckable(movieEndGroup, tr("Pause the Movie"), Config::MOVIEEND_PAUSE);
    addActionCheckable(movieEndGroup, tr("Switch to Writing"), Config::MOVIEEND_WRITE);

    renderPerfGroup = new QActionGroup(this);
    renderPerfGroup->setExclusive(false);

    addActionCheckable(renderPerfGroup, tr("minimize texture cache footprint"), "texmem");
    addActionCheckable(renderPerfGroup, tr("MIP_FILTER_NONE always"), "no_mipmap");
    addActionCheckable(renderPerfGroup, tr("FILTER_NEAREST always"), "no_linear");
    addActionCheckable(renderPerfGroup, tr("MIP_FILTER_LINEAR ==> _NEAREST"), "no_mip_linear");
    addActionCheckable(renderPerfGroup, tr("sample white always"), "no_tex");
    addActionCheckable(renderPerfGroup, tr("disable blending"), "no_blend");
    addActionCheckable(renderPerfGroup, tr("disable depth buffering entirely"), "no_depth");
    addActionCheckable(renderPerfGroup, tr("disable alpha testing"), "no_alphatest");

    osdGroup = new QActionGroup(this);
    osdGroup->setExclusive(false);
    connect(osdGroup, &QActionGroup::triggered, this, &MainWindow::slotOsd);

    addActionCheckable(osdGroup, tr("Frame Count"), SharedConfig::OSD_FRAMECOUNT);
    addActionCheckable(osdGroup, tr("Inputs"), SharedConfig::OSD_INPUTS);

    frequencyGroup = new QActionGroup(this);

    addActionCheckable(frequencyGroup, tr("8000 Hz"), 8000);
    addActionCheckable(frequencyGroup, tr("11025 Hz"), 11025);
    addActionCheckable(frequencyGroup, tr("12000 Hz"), 12000);
    addActionCheckable(frequencyGroup, tr("16000 Hz"), 16000);
    addActionCheckable(frequencyGroup, tr("22050 Hz"), 22050);
    addActionCheckable(frequencyGroup, tr("24000 Hz"), 24000);
    addActionCheckable(frequencyGroup, tr("32000 Hz"), 32000);
    addActionCheckable(frequencyGroup, tr("44100 Hz"), 44100);
    addActionCheckable(frequencyGroup, tr("48000 Hz"), 48000);

    bitDepthGroup = new QActionGroup(this);

    addActionCheckable(bitDepthGroup, tr("8 bit"), 8);
    addActionCheckable(bitDepthGroup, tr("16 bit"), 16);

    channelGroup = new QActionGroup(this);

    addActionCheckable(channelGroup, tr("Mono"), 1);
    addActionCheckable(channelGroup, tr("Stereo"), 2);

    timeMainGroup = new QActionGroup(this);
    timeMainGroup->setExclusive(false);

    addActionCheckable(timeMainGroup, tr("time()"), SharedConfig::TIMETYPE_TIME);
    addActionCheckable(timeMainGroup, tr("gettimeofday()"), SharedConfig::TIMETYPE_GETTIMEOFDAY);
    addActionCheckable(timeMainGroup, tr("clock()"), SharedConfig::TIMETYPE_CLOCK);
    addActionCheckable(timeMainGroup, tr("clock_gettime()"), SharedConfig::TIMETYPE_CLOCKGETTIME);
    addActionCheckable(timeMainGroup, tr("SDL_GetTicks()"), SharedConfig::TIMETYPE_SDLGETTICKS);
    addActionCheckable(timeMainGroup, tr("SDL_GetPerformanceCounter()"), SharedConfig::TIMETYPE_SDLGETPERFORMANCECOUNTER);

    timeSecGroup = new QActionGroup(this);
    timeSecGroup->setExclusive(false);

    addActionCheckable(timeSecGroup, tr("time()"), SharedConfig::TIMETYPE_TIME);
    addActionCheckable(timeSecGroup, tr("gettimeofday()"), SharedConfig::TIMETYPE_GETTIMEOFDAY);
    addActionCheckable(timeSecGroup, tr("clock()"), SharedConfig::TIMETYPE_CLOCK);
    addActionCheckable(timeSecGroup, tr("clock_gettime()"), SharedConfig::TIMETYPE_CLOCKGETTIME);
    addActionCheckable(timeSecGroup, tr("SDL_GetTicks()"), SharedConfig::TIMETYPE_SDLGETTICKS);
    addActionCheckable(timeSecGroup, tr("SDL_GetPerformanceCounter()"), SharedConfig::TIMETYPE_SDLGETPERFORMANCECOUNTER);

    savestateIgnoreGroup = new QActionGroup(this);
    savestateIgnoreGroup->setExclusive(false);
    connect(savestateIgnoreGroup, &QActionGroup::triggered, this, &MainWindow::slotSavestateIgnore);

    addActionCheckable(savestateIgnoreGroup, tr("Ignore non-writeable segments"), SharedConfig::IGNORE_NON_WRITEABLE);
    addActionCheckable(savestateIgnoreGroup, tr("Ignore non-writeable non-anonymous segments"), SharedConfig::IGNORE_NON_ANONYMOUS_NON_WRITEABLE);
    addActionCheckable(savestateIgnoreGroup, tr("Ignore exec segments"), SharedConfig::IGNORE_EXEC);
    addActionCheckable(savestateIgnoreGroup, tr("Ignore shared segments"), SharedConfig::IGNORE_SHARED);

    loggingOutputGroup = new QActionGroup(this);

    addActionCheckable(loggingOutputGroup, tr("Disabled"), SharedConfig::NO_LOGGING);
    addActionCheckable(loggingOutputGroup, tr("Log to console"), SharedConfig::LOGGING_TO_CONSOLE);
    addActionCheckable(loggingOutputGroup, tr("Log to file"), SharedConfig::LOGGING_TO_FILE);

    loggingPrintGroup = new QActionGroup(this);
    loggingPrintGroup->setExclusive(false);
    connect(loggingPrintGroup, &QActionGroup::triggered, this, &MainWindow::slotLoggingPrint);

    addActionCheckable(loggingPrintGroup, tr("Untested"), LCF_UNTESTED);
    addActionCheckable(loggingPrintGroup, tr("Desync"), LCF_DESYNC);
    addActionCheckable(loggingPrintGroup, tr("Frequent"), LCF_FREQUENT);
    addActionCheckable(loggingPrintGroup, tr("Error"), LCF_ERROR);
    addActionCheckable(loggingPrintGroup, tr("ToDo"), LCF_TODO);
    addActionCheckable(loggingPrintGroup, tr("Frame"), LCF_FRAME);
    addActionCheckable(loggingPrintGroup, tr("Hook"), LCF_HOOK);
    addActionCheckable(loggingPrintGroup, tr("Time Set"), LCF_TIMESET);
    addActionCheckable(loggingPrintGroup, tr("Time Get"), LCF_TIMEGET);
    addActionCheckable(loggingPrintGroup, tr("Checkpoint"), LCF_CHECKPOINT);
    addActionCheckable(loggingPrintGroup, tr("Wait"), LCF_WAIT);
    addActionCheckable(loggingPrintGroup, tr("Sleep"), LCF_SLEEP);
    addActionCheckable(loggingPrintGroup, tr("Socket"), LCF_SOCKET);
    addActionCheckable(loggingPrintGroup, tr("OpenGL"), LCF_OGL);
    addActionCheckable(loggingPrintGroup, tr("AV Dumping"), LCF_DUMP);
    addActionCheckable(loggingPrintGroup, tr("SDL"), LCF_SDL);
    addActionCheckable(loggingPrintGroup, tr("Memory"), LCF_MEMORY);
    addActionCheckable(loggingPrintGroup, tr("Keyboard"), LCF_KEYBOARD);
    addActionCheckable(loggingPrintGroup, tr("Mouse"), LCF_MOUSE);
    addActionCheckable(loggingPrintGroup, tr("Joystick"), LCF_JOYSTICK);
    addActionCheckable(loggingPrintGroup, tr("OpenAL"), LCF_OPENAL);
    addActionCheckable(loggingPrintGroup, tr("Sound"), LCF_SOUND);
    addActionCheckable(loggingPrintGroup, tr("Random"), LCF_RANDOM);
    addActionCheckable(loggingPrintGroup, tr("Signals"), LCF_SIGNAL);
    addActionCheckable(loggingPrintGroup, tr("Events"), LCF_EVENTS);
    addActionCheckable(loggingPrintGroup, tr("Windows"), LCF_WINDOW);
    addActionCheckable(loggingPrintGroup, tr("File IO"), LCF_FILEIO);
    addActionCheckable(loggingPrintGroup, tr("Steam"), LCF_STEAM);
    addActionCheckable(loggingPrintGroup, tr("Threads"), LCF_THREAD);
    addActionCheckable(loggingPrintGroup, tr("Timers"), LCF_TIMERS);

    loggingExcludeGroup = new QActionGroup(this);
    loggingExcludeGroup->setExclusive(false);
    connect(loggingExcludeGroup, &QActionGroup::triggered, this, &MainWindow::slotLoggingExclude);

    addActionCheckable(loggingExcludeGroup, tr("Untested"), LCF_UNTESTED);
    addActionCheckable(loggingExcludeGroup, tr("Desync"), LCF_DESYNC);
    addActionCheckable(loggingExcludeGroup, tr("Frequent"), LCF_FREQUENT);
    addActionCheckable(loggingExcludeGroup, tr("Error"), LCF_ERROR);
    addActionCheckable(loggingExcludeGroup, tr("ToDo"), LCF_TODO);
    addActionCheckable(loggingExcludeGroup, tr("Frame"), LCF_FRAME);
    addActionCheckable(loggingExcludeGroup, tr("Hook"), LCF_HOOK);
    addActionCheckable(loggingExcludeGroup, tr("Time Set"), LCF_TIMESET);
    addActionCheckable(loggingExcludeGroup, tr("Time Get"), LCF_TIMEGET);
    addActionCheckable(loggingExcludeGroup, tr("Checkpoint"), LCF_CHECKPOINT);
    addActionCheckable(loggingExcludeGroup, tr("Wait"), LCF_WAIT);
    addActionCheckable(loggingExcludeGroup, tr("Sleep"), LCF_SLEEP);
    addActionCheckable(loggingExcludeGroup, tr("Socket"), LCF_SOCKET);
    addActionCheckable(loggingExcludeGroup, tr("OpenGL"), LCF_OGL);
    addActionCheckable(loggingExcludeGroup, tr("AV Dumping"), LCF_DUMP);
    addActionCheckable(loggingExcludeGroup, tr("SDL"), LCF_SDL);
    addActionCheckable(loggingExcludeGroup, tr("Memory"), LCF_MEMORY);
    addActionCheckable(loggingExcludeGroup, tr("Keyboard"), LCF_KEYBOARD);
    addActionCheckable(loggingExcludeGroup, tr("Mouse"), LCF_MOUSE);
    addActionCheckable(loggingExcludeGroup, tr("Joystick"), LCF_JOYSTICK);
    addActionCheckable(loggingExcludeGroup, tr("OpenAL"), LCF_OPENAL);
    addActionCheckable(loggingExcludeGroup, tr("Sound"), LCF_SOUND);
    addActionCheckable(loggingExcludeGroup, tr("Random"), LCF_RANDOM);
    addActionCheckable(loggingExcludeGroup, tr("Signals"), LCF_SIGNAL);
    addActionCheckable(loggingExcludeGroup, tr("Events"), LCF_EVENTS);
    addActionCheckable(loggingExcludeGroup, tr("Windows"), LCF_WINDOW);
    addActionCheckable(loggingExcludeGroup, tr("File IO"), LCF_FILEIO);
    addActionCheckable(loggingExcludeGroup, tr("Steam"), LCF_STEAM);
    addActionCheckable(loggingExcludeGroup, tr("Threads"), LCF_THREAD);
    addActionCheckable(loggingExcludeGroup, tr("Timers"), LCF_TIMERS);

    slowdownGroup = new QActionGroup(this);
    connect(slowdownGroup, &QActionGroup::triggered, this, &MainWindow::slotSlowdown);

    addActionCheckable(slowdownGroup, tr("100% (normal speed)"), 1);
    addActionCheckable(slowdownGroup, tr("50%"), 2);
    addActionCheckable(slowdownGroup, tr("25%"), 4);
    addActionCheckable(slowdownGroup, tr("12%"), 8);

    joystickGroup = new QActionGroup(this);
    addActionCheckable(joystickGroup, tr("None"), 0);
    addActionCheckable(joystickGroup, tr("1"), 1);
    addActionCheckable(joystickGroup, tr("2"), 2);
    addActionCheckable(joystickGroup, tr("3"), 3);
    addActionCheckable(joystickGroup, tr("4"), 4);

    hotkeyFocusGroup = new QActionGroup(this);
    hotkeyFocusGroup->setExclusive(false);
    connect(hotkeyFocusGroup, &QActionGroup::triggered, this, &MainWindow::slotHotkeyFocus);

    addActionCheckable(hotkeyFocusGroup, tr("Game has focus"), Context::FOCUS_GAME);
    addActionCheckable(hotkeyFocusGroup, tr("UI has focus"), Context::FOCUS_UI);
    addActionCheckable(hotkeyFocusGroup, tr("Always (not working)"), Context::FOCUS_ALL);

    inputFocusGroup = new QActionGroup(this);
    inputFocusGroup->setExclusive(false);
    connect(inputFocusGroup, &QActionGroup::triggered, this, &MainWindow::slotInputFocus);

    addActionCheckable(inputFocusGroup, tr("Game has focus"), Context::FOCUS_GAME);
    addActionCheckable(inputFocusGroup, tr("UI has focus"), Context::FOCUS_UI);
    addActionCheckable(inputFocusGroup, tr("Always (not working)"), Context::FOCUS_ALL);
}

void MainWindow::createMenus()
{
    /* File Menu */
    QMenu *fileMenu = menuBar()->addMenu(tr("File"));

    fileMenu->addAction(tr("Open Executable..."), this, &MainWindow::slotBrowseGamePath);
    fileMenu->addAction(tr("Executable Options..."), executableWindow, &ExecutableWindow::exec);
    fileMenu->addAction(tr("Open Movie..."), this, &MainWindow::slotBrowseMoviePath);
    fileMenu->addAction(tr("Save Movie"), this, &MainWindow::slotSaveMovie);
    fileMenu->addAction(tr("Export Movie..."), this, &MainWindow::slotExportMovie);

    QMenu *movieEndMenu = fileMenu->addMenu(tr("On Movie End"));
    movieEndMenu->addActions(movieEndGroup->actions());

    /* Video Menu */
    QMenu *videoMenu = menuBar()->addMenu(tr("Video"));

    renderSoftAction = videoMenu->addAction(tr("Force software rendering"));
    renderSoftAction->setCheckable(true);
    disabledActionsOnStart.append(renderSoftAction);

    QMenu *renderPerfMenu = videoMenu->addMenu(tr("Add performance flags to software rendering"));
    renderPerfMenu->addActions(renderPerfGroup->actions());
    disabledWidgetsOnStart.append(renderPerfMenu);

    QMenu *osdMenu = videoMenu->addMenu(tr("OSD"));
    osdMenu->addActions(osdGroup->actions());
    osdMenu->addSeparator();
    osdEncodeAction = osdMenu->addAction(tr("OSD on video encode"), this, &MainWindow::slotOsdEncode);
    osdEncodeAction->setCheckable(true);

    /* Sound Menu */
    QMenu *soundMenu = menuBar()->addMenu(tr("Sound"));

    QMenu *formatMenu = soundMenu->addMenu(tr("Format"));
    formatMenu->addActions(frequencyGroup->actions());
    formatMenu->addSeparator();
    formatMenu->addActions(bitDepthGroup->actions());
    formatMenu->addSeparator();
    formatMenu->addActions(channelGroup->actions());
    disabledWidgetsOnStart.append(formatMenu);

    muteAction = soundMenu->addAction(tr("Mute"), this, &MainWindow::slotMuteSound);
    muteAction->setCheckable(true);

    /* Runtime Menu */
    QMenu *runtimeMenu = menuBar()->addMenu(tr("Runtime"));

    QMenu *timeMenu = runtimeMenu->addMenu(tr("Time tracking"));
    disabledWidgetsOnStart.append(timeMenu);
    QMenu *timeMainMenu = timeMenu->addMenu(tr("Main thread"));
    timeMainMenu->addActions(timeMainGroup->actions());
    QMenu *timeSecMenu = timeMenu->addMenu(tr("Secondary thread"));
    timeSecMenu->addActions(timeSecGroup->actions());

    QMenu *savestateMenu = runtimeMenu->addMenu(tr("Savestates"));
    QMenu *savestateSegmentMenu = savestateMenu->addMenu(tr("Ignore memory segments"));
    savestateSegmentMenu->addActions(savestateIgnoreGroup->actions());

    saveScreenAction = runtimeMenu->addAction(tr("Save screen"), this, &MainWindow::slotSaveScreen);
    saveScreenAction->setCheckable(true);
    preventSavefileAction = runtimeMenu->addAction(tr("Backup savefiles in memory"), this, &MainWindow::slotPreventSavefile);
    preventSavefileAction->setCheckable(true);

    QMenu *debugMenu = runtimeMenu->addMenu(tr("Debug Logging"));
    debugMenu->addActions(loggingOutputGroup->actions());
    disabledActionsOnStart.append(loggingOutputGroup->actions());

    debugMenu->addSeparator();

    QMenu *debugPrintMenu = debugMenu->addMenu(tr("Print Categories"));
    debugPrintMenu->addActions(loggingPrintGroup->actions());
    QMenu *debugExcludeMenu = debugMenu->addMenu(tr("Exclude Categories"));
    debugExcludeMenu->addActions(loggingExcludeGroup->actions());

    /* Tools Menu */
    QMenu *toolsMenu = menuBar()->addMenu(tr("Tools"));
    configEncodeAction = toolsMenu->addAction(tr("Configure encode..."), encodeWindow, &EncodeWindow::exec);
    toggleEncodeAction = toolsMenu->addAction(tr("Start encode"), this, &MainWindow::slotToggleEncode);

    toolsMenu->addSeparator();

    QMenu *slowdownMenu = toolsMenu->addMenu(tr("Slow Motion"));
    slowdownMenu->addActions(slowdownGroup->actions());

    toolsMenu->addSeparator();

    toolsMenu->addAction(tr("Game information..."), gameInfoWindow, &GameInfoWindow::exec);

    toolsMenu->addSeparator();

    toolsMenu->addAction(tr("Ram Search..."), ramSearchWindow, &RamSearchWindow::show);
    toolsMenu->addAction(tr("Ram Watch..."), ramWatchWindow, &RamWatchWindow::show);

    /* Input Menu */
    QMenu *inputMenu = menuBar()->addMenu(tr("Input"));
    inputMenu->addAction(tr("Configure mapping..."), inputWindow, &InputWindow::exec);

    keyboardAction = inputMenu->addAction(tr("Keyboard support"));
    keyboardAction->setCheckable(true);
    disabledActionsOnStart.append(keyboardAction);
    mouseAction = inputMenu->addAction(tr("Mouse support"));
    mouseAction->setCheckable(true);
    disabledActionsOnStart.append(mouseAction);

    QMenu *joystickMenu = inputMenu->addMenu(tr("Joystick support"));
    joystickMenu->addActions(joystickGroup->actions());
    disabledWidgetsOnStart.append(joystickMenu);

    inputMenu->addAction(tr("Joystick inputs..."), controllerTabWindow, &ControllerTabWindow::show);

    inputMenu->addSeparator();

    QMenu *hotkeyFocusMenu = inputMenu->addMenu(tr("Enable hotkeys when"));
    hotkeyFocusMenu->addActions(hotkeyFocusGroup->actions());

    QMenu *inputFocusMenu = inputMenu->addMenu(tr("Enable inputs when"));
    inputFocusMenu->addActions(inputFocusGroup->actions());

}

void MainWindow::updateStatus()
{
    /* Update game status (active/inactive) */

    switch (context->status) {

        case Context::INACTIVE:
            for (QWidget* w : disabledWidgetsOnStart)
                w->setEnabled(true);
            for (QAction* a : disabledActionsOnStart)
                a->setEnabled(true);

            if (context->config.sc.recording == SharedConfig::NO_RECORDING) {
                movieBox->setEnabled(true);
            }
            movieBox->setCheckable(true);
            movieBox->setChecked(context->config.sc.recording != SharedConfig::NO_RECORDING);

            // item = const_cast<Fl_Menu_Item*>(menu_bar->find_item(save_movie_cb));
            // if (item) item->deactivate();
            // item = const_cast<Fl_Menu_Item*>(menu_bar->find_item(export_movie_cb));
            // if (item) item->deactivate();

            initialTimeSec->setValue(context->config.sc.initial_time.tv_sec);
            initialTimeNsec->setValue(context->config.sc.initial_time.tv_nsec);

#ifdef LIBTAS_ENABLE_AVDUMPING
            if (context->config.sc.av_dumping) {
                context->config.sc.av_dumping = false;
                configEncodeAction->setEnabled(true);
                toggleEncodeAction->setText("Start encode");
            }
#endif

            frameCount->setValue(0);
            currentLength->setText("Current Time: -");
            fpsValues->setText("Current FPS: - / -");
            {
                MovieFile tempmovie(context);
                /* Update the movie frame count and rerecord count
                 * if the movie file is valid.
                 */
                if (tempmovie.extractMovie() == 0) {
                    movieFrameCount->setValue(tempmovie.nbFramesConfig());
                    rerecordCount->setValue(tempmovie.nbRerecords());
                    int sec, nsec;
                    tempmovie.lengthConfig(sec, nsec);
                    movieLength->setText(QString("Movie length: %1m %2s").arg(sec/60).arg((sec%60) + (nsec/1000000000.0), 0, 'f', 2));

                    if (context->config.sc.recording != SharedConfig::NO_RECORDING) {
                        context->config.sc.recording = SharedConfig::RECORDING_READ;
                        moviePlayback->setChecked(true);
                    }
                }
                else {
                    if (context->config.sc.recording != SharedConfig::NO_RECORDING) {
                        context->config.sc.recording = SharedConfig::RECORDING_WRITE;
                        movieRecording->setChecked(true);
                    }
                }
            }
            break;

        case Context::STARTING:
            for (QWidget* w : disabledWidgetsOnStart)
                w->setEnabled(false);
            for (QAction* a : disabledActionsOnStart)
                a->setEnabled(false);

            movieBox->setCheckable(false);
            if (context->config.sc.recording == SharedConfig::NO_RECORDING) {
                movieBox->setEnabled(false);
            }

            break;

        case Context::ACTIVE:
            stopButton->setEnabled(true);
            // if (context->config.sc.recording != SharedConfig::NO_RECORDING) {
            //     item = const_cast<Fl_Menu_Item*>(menu_bar->find_item(save_movie_cb));
            //     if (item) item->activate();
            //     item = const_cast<Fl_Menu_Item*>(menu_bar->find_item(export_movie_cb));
            //     if (item) item->activate();
            // }
            break;
        case Context::QUITTING:
            stopButton->setEnabled(false);
            break;
        default:
            break;
    }
}

void MainWindow::updateSharedConfigChanged()
{
    /* Update pause status */
    pauseCheck->setChecked(!context->config.sc.running);

    /* Update fastforward status */
    fastForwardCheck->setChecked(context->config.sc.fastforward);

    /* Update recording state */
    std::string movieframestr;
    switch (context->config.sc.recording) {
        case SharedConfig::RECORDING_WRITE:
            movieRecording->setChecked(true);
            movieFrameCount->setValue(context->config.sc.movie_framecount);
            break;
        case SharedConfig::RECORDING_READ:
            moviePlayback->setChecked(true);
            movieFrameCount->setValue(context->config.sc.movie_framecount);
            break;
        default:
            break;
    }

    /* Update encode menus */
#ifdef LIBTAS_ENABLE_AVDUMPING
    if (context->config.sc.av_dumping) {
        configEncodeAction->setEnabled(false);
        toggleEncodeAction->setText("Stop encode");
    }
    else {
        configEncodeAction->setEnabled(true);
        toggleEncodeAction->setText("Start encode");
    }
#endif
}

void MainWindow::updateFrameCountTime()
{
    /* Update frame count */
    frameCount->setValue(context->framecount);
    movieFrameCount->setValue(context->config.sc.movie_framecount);

    /* Update time */
    initialTimeSec->setValue(context->current_time.tv_sec);
    initialTimeNsec->setValue(context->current_time.tv_nsec);

    /* Update movie time */
    if (context->config.sc.framerate > 0) {
        double sec = (double)(context->framecount % (context->config.sc.framerate * 60)) / context->config.sc.framerate;
        int min = context->framecount / (context->config.sc.framerate * 60);

        currentLength->setText(QString("Current Time: %1m %2s").arg(min).arg(sec, 0, 'f', 2));

        /* Format movie length */
        double msec = (double)(context->config.sc.movie_framecount % (context->config.sc.framerate * 60)) / context->config.sc.framerate;
        int mmin = context->config.sc.movie_framecount / (context->config.sc.framerate * 60);

        movieLength->setText(QString("Movie length: %1m %2s").arg(mmin).arg(msec, 0, 'f', 2));
    }
}

void MainWindow::updateRerecordCount()
{
    /* Update frame count */
    rerecordCount->setValue(context->rerecord_count);
}

void MainWindow::updateFps(float fps, float lfps)
{
    /* Update fps values */
    if ((fps > 0) || (lfps > 0)) {
        fpsValues->setText(QString("Current FPS: %1 / %2").arg(fps, 0, 'f', 1).arg(lfps, 0, 'f', 1));
    }
    else {
        fpsValues->setText("Current FPS: - / -");
    }
}

void MainWindow::updateRam()
{
    if (ramSearchWindow->isVisible()) {
        ramSearchWindow->update();
    }
    if (ramWatchWindow->isVisible()) {
        ramWatchWindow->update();
    }
}

void MainWindow::setCheckboxesFromMask(const QActionGroup *actionGroup, int value)
{
    for (auto& action : actionGroup->actions()) {
        action->setChecked(value & action->data().toInt());
    }
}

void MainWindow::setMaskFromCheckboxes(const QActionGroup *actionGroup, int &value)
{
    value = 0;
    for (const auto& action : actionGroup->actions()) {
        if (action->isChecked()) {
            value |= action->data().toInt();
        }
    }
}

void MainWindow::setRadioFromList(const QActionGroup *actionGroup, int value)
{
    for (auto& action : actionGroup->actions()) {
        if (value == action->data().toInt()) {
            action->setChecked(true);
            return;
        }
    }
}

void MainWindow::setListFromRadio(const QActionGroup *actionGroup, int &value)
{
    for (const auto& action : actionGroup->actions()) {
        if (action->isChecked()) {
            value = action->data().toInt();
            return;
        }
    }
}

void MainWindow::updateUIFromConfig()
{
    gamePath->setText(context->gamepath.c_str());
    cmdOptions->setText(context->config.gameargs.c_str());
    moviePath->setText(context->config.moviefile.c_str());
    logicalFps->setValue(context->config.sc.framerate);

    initialTimeSec->setValue(context->config.sc.initial_time.tv_sec);
    initialTimeNsec->setValue(context->config.sc.initial_time.tv_nsec);

    movieBox->setChecked(!(context->config.sc.recording == SharedConfig::NO_RECORDING));

    MovieFile tempmovie(context);
    if (tempmovie.extractMovie() == 0) {
        movieFrameCount->setValue(tempmovie.nbFramesConfig());
        rerecordCount->setValue(tempmovie.nbRerecords());
        int sec, nsec;
        tempmovie.lengthConfig(sec, nsec);
        movieLength->setText(QString("Movie length: %1m %2s").arg(sec/60).arg((sec%60) + (nsec/1000000000.0), 0, 'f', 2));

        /* Also, by default, set to playback mode */
        moviePlayback->setChecked(true);
    }
    else {
        movieRecording->setChecked(true);
    }

    pauseCheck->setChecked(!context->config.sc.running);
    fastForwardCheck->setChecked(context->config.sc.fastforward);

    setRadioFromList(frequencyGroup, context->config.sc.audio_frequency);
    setRadioFromList(bitDepthGroup, context->config.sc.audio_bitdepth);
    setRadioFromList(channelGroup, context->config.sc.audio_channels);

    muteAction->setChecked(context->config.sc.audio_mute);

    setRadioFromList(loggingOutputGroup, context->config.sc.logging_status);

    setCheckboxesFromMask(loggingPrintGroup, context->config.sc.includeFlags);
    setCheckboxesFromMask(loggingExcludeGroup, context->config.sc.excludeFlags);

    setRadioFromList(slowdownGroup, context->config.sc.speed_divisor);

    keyboardAction->setChecked(context->config.sc.keyboard_support);
    mouseAction->setChecked(context->config.sc.mouse_support);

    setRadioFromList(joystickGroup, context->config.sc.nb_controllers);

#ifdef LIBTAS_ENABLE_HUD
    setCheckboxesFromMask(osdGroup, context->config.sc.osd);
    osdEncodeAction->setChecked(context->config.sc.osd_encode);
#endif

    for (auto& action : timeMainGroup->actions()) {
        action->setChecked(context->config.sc.main_gettimes_threshold[action->data().toInt()] != -1);
    }

    for (auto& action : timeSecGroup->actions()) {
        action->setChecked(context->config.sc.sec_gettimes_threshold[action->data().toInt()] != -1);
    }

    setCheckboxesFromMask(hotkeyFocusGroup, context->hotkeys_focus);
    setCheckboxesFromMask(inputFocusGroup, context->inputs_focus);

    renderSoftAction->setChecked(context->config.opengl_soft);
    saveScreenAction->setChecked(context->config.sc.save_screenpixels);
    preventSavefileAction->setChecked(context->config.sc.prevent_savefiles);

    setCheckboxesFromMask(savestateIgnoreGroup, context->config.sc.ignore_sections);

    setRadioFromList(movieEndGroup, context->config.on_movie_end);
}

void MainWindow::slotLaunch()
{
    /* Do we attach gdb ? */
    QPushButton* button = static_cast<QPushButton*>(sender());
    context->attach_gdb = (button == launchGdbButton);

    if (context->status != Context::INACTIVE)
        return;

    /* Perform all checks */
    if (!ErrorChecking::allChecks(context))
        return;

    /* Set a few parameters */
    context->config.sc.framerate = logicalFps->value();
    context->config.sc.initial_time.tv_sec = initialTimeSec->value();
    context->config.sc.initial_time.tv_nsec = initialTimeNsec->value();

    setListFromRadio(frequencyGroup, context->config.sc.audio_frequency);
    setListFromRadio(bitDepthGroup, context->config.sc.audio_bitdepth);
    setListFromRadio(channelGroup, context->config.sc.audio_channels);

    setListFromRadio(loggingOutputGroup, context->config.sc.logging_status);

    context->config.sc.keyboard_support = keyboardAction->isChecked();
    context->config.sc.mouse_support = mouseAction->isChecked();
    setListFromRadio(joystickGroup, context->config.sc.nb_controllers);

    for (const auto& action : timeMainGroup->actions()) {
        int index = action->data().toInt();
        if (action->isChecked()) {
            context->config.sc.main_gettimes_threshold[index] = 100;
        }
        else {
            context->config.sc.main_gettimes_threshold[index] = -1;
        }
    }
    for (const auto& action : timeSecGroup->actions()) {
        int index = action->data().toInt();
        if (action->isChecked()) {
            context->config.sc.sec_gettimes_threshold[index] = 100;
        }
        else {
            context->config.sc.sec_gettimes_threshold[index] = -1;
        }
    }

    context->config.opengl_soft = renderSoftAction->isChecked();
    context->config.gameargs = cmdOptions->text().toStdString();

    QString llvmStr;
    for (const auto& action : renderPerfGroup->actions()) {
        if (action->isChecked()) {
            llvmStr.append(action->data().toString()).append(",");
        }
    }
    /* Remove the trailing comma */
    llvmStr.chop(1);
    context->config.llvm_perf = llvmStr.toStdString();

    /* Check that there might be a thread from a previous game execution */
    if (game_thread.joinable())
        game_thread.join();

    /* Start game */
    context->status = Context::STARTING;
    updateStatus();
    game_thread = std::thread{&GameLoop::start, gameLoop};
}

void MainWindow::slotStop()
{
    if (context->status != Context::ACTIVE)
        return;

    context->status = Context::QUITTING;
    context->config.sc.running = true;
    context->config.sc_modified = true;
    updateSharedConfigChanged();
    updateStatus();
    game_thread.detach();
}

void MainWindow::slotBrowseGamePath()
{
    QString filename = QFileDialog::getOpenFileName(this, tr("Game path"), context->gamepath.c_str());
    if (filename.isNull())
        return;

    /* Save the previous config */
    context->config.save(context->gamepath);

    gamePath->setText(filename);
    context->gamepath = filename.toStdString();

    /* Try to load the game-specific pref file */
    context->config.load(context->gamepath);

    /* Update the UI accordingly */
    updateUIFromConfig();
#ifdef LIBTAS_ENABLE_AVDUMPING
    encodeWindow->update_config();
#endif
    executableWindow->update_config();
    inputWindow->update();
}

void MainWindow::slotBrowseMoviePath()
{
    QString filename = QFileDialog::getSaveFileName(this, tr("Choose a movie file"), context->config.moviefile.c_str(), tr("libTAS movie files (*.ltm)"));
    if (filename.isNull())
        return;

    moviePath->setText(filename);
    context->config.moviefile = filename.toStdString();

    MovieFile tempmovie(context);
    if (tempmovie.extractMovie() == 0) {
        movieFrameCount->setValue(tempmovie.nbFramesConfig());
        rerecordCount->setValue(tempmovie.nbRerecords());
        int sec, nsec;
        tempmovie.lengthConfig(sec, nsec);
        movieLength->setText(QString("Movie length: %1m %2s").arg(sec/60).arg((sec%60) + (nsec/1000000000.0), 0, 'f', 2));

        moviePlayback->setChecked(true);
        context->config.sc.recording = SharedConfig::RECORDING_READ;
        context->config.sc_modified = true;
    }
    else {
        movieFrameCount->setValue(0);
        rerecordCount->setValue(0);
        movieLength->setText("Movie length: -");

        movieRecording->setChecked(true);
        context->config.sc.recording = SharedConfig::RECORDING_WRITE;
        context->config.sc_modified = true;
    }
}

void MainWindow::slotSaveMovie()
{
    if (context->config.sc.recording != SharedConfig::NO_RECORDING)
        gameLoop->movie.saveMovie(); // TODO: game.h exports the movie object, bad...
}

void MainWindow::slotExportMovie()
{
    if (context->config.sc.recording != SharedConfig::NO_RECORDING) {
        QString filename = QFileDialog::getSaveFileName(this, tr("Choose a movie file"), context->config.moviefile.c_str(), tr("libTAS movie files (*.ltm)"));
        gameLoop->movie.saveMovie(filename.toStdString()); // TODO: game.h exports the movie object, bad...
    }
}

void MainWindow::slotPause(bool checked)
{
    if (context->status == Context::INACTIVE) {
        /* If the game is inactive, set the value directly */
        context->config.sc.running = !checked;
    }
    else {
        /* Else, let the game thread set the value */
        context->hotkey_queue.push(HOTKEY_PLAYPAUSE);
    }
}

void MainWindow::slotFastForward(bool checked)
{
    context->config.sc.fastforward = checked;
    context->config.sc_modified = true;
}

void MainWindow::slotMovieEnable(bool checked)
{
    if (checked) {
        if (movieRecording->isChecked()) {
            context->config.sc.recording = SharedConfig::RECORDING_WRITE;
        }
        else {
            context->config.sc.recording = SharedConfig::RECORDING_READ;
        }
    }
    else {
        context->config.sc.recording = SharedConfig::NO_RECORDING;
    }
    context->config.sc_modified = true;
}

void MainWindow::slotMovieRecording()
{
    /* If the game is running, we let the main thread deal with movie toggling.
     * Else, we set the recording mode.
     */
    if (context->status == Context::INACTIVE) {
        if (movieRecording->isChecked()) {
            context->config.sc.recording = SharedConfig::RECORDING_WRITE;
        }
        else {
            context->config.sc.recording = SharedConfig::RECORDING_READ;
        }
    }
    else {
        context->hotkey_queue.push(HOTKEY_READWRITE);
    }
    context->config.sc_modified = true;
}

#ifdef LIBTAS_ENABLE_AVDUMPING
void MainWindow::slotToggleEncode()
{
    /* Prompt a confirmation message for overwriting an encode file */
    if (!context->config.sc.av_dumping) {
        struct stat sb;
        if (stat(context->config.dumpfile.c_str(), &sb) == 0) {
            /* Pause the game during the choice */
            context->config.sc.running = false;
            context->config.sc_modified = true;

            QMessageBox::StandardButton btn = QMessageBox::question(this, "File overwrite", QString("The encode file %s does exist. Do you want to overwrite it?").arg(context->config.dumpfile.c_str()), QMessageBox::Ok | QMessageBox::Cancel);
            if (btn != QMessageBox::Ok)
                return;
        }
    }

    /* TODO: Using directly the hotkey does not check for existing file */
    context->hotkey_queue.push(HOTKEY_TOGGLE_ENCODE);

}
#endif

void MainWindow::slotMuteSound(bool checked)
{
    context->config.sc.audio_mute = checked;
    context->config.sc_modified = true;
}

void MainWindow::slotLoggingPrint()
{
    setMaskFromCheckboxes(loggingPrintGroup, context->config.sc.includeFlags);
    context->config.sc_modified = true;
}

void MainWindow::slotLoggingExclude()
{
    setMaskFromCheckboxes(loggingExcludeGroup, context->config.sc.excludeFlags);
    context->config.sc_modified = true;
}

void MainWindow::slotHotkeyFocus()
{
    setMaskFromCheckboxes(hotkeyFocusGroup, context->hotkeys_focus);

    /* If the game was not launched, don't do anything */
    if (context->game_window ) {
        if (context->hotkeys_focus & Context::FOCUS_GAME) {
            const static uint32_t values[] = { XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_EXPOSURE};
            xcb_change_window_attributes (context->conn, context->game_window, XCB_CW_EVENT_MASK, values);
        }
        else {
            const static uint32_t values[] = { XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_EXPOSURE};
            xcb_change_window_attributes (context->conn, context->game_window, XCB_CW_EVENT_MASK, values);
        }
    }
    // if (context->hotkeys_focus & Context::FOCUS_UI) {
    //     const static uint32_t values[] = { XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_FOCUS_CHANGE };
    //     xcb_change_window_attributes (context->conn, context->ui_window, XCB_CW_EVENT_MASK, values);
    // }
    // else {
    //     const static uint32_t values[] = { XCB_EVENT_MASK_FOCUS_CHANGE };
    //     xcb_change_window_attributes (context->conn, context->ui_window, XCB_CW_EVENT_MASK, values);
    // }
}

void MainWindow::slotInputFocus()
{
    setMaskFromCheckboxes(inputFocusGroup, context->inputs_focus);
}

void MainWindow::slotSlowdown()
{
    setListFromRadio(slowdownGroup, context->config.sc.speed_divisor);
    context->config.sc_modified = true;
}

#ifdef LIBTAS_ENABLE_HUD

void MainWindow::slotOsd()
{
    setListFromRadio(osdGroup, context->config.sc.osd);
    context->config.sc_modified = true;
}

void MainWindow::slotOsdEncode(bool checked)
{
    context->config.sc.osd_encode = checked;
    context->config.sc_modified = true;
}
#endif

void MainWindow::slotSavestateIgnore()
{
    setMaskFromCheckboxes(savestateIgnoreGroup, context->config.sc.ignore_sections);
    context->config.sc_modified = true;
}

void MainWindow::slotSaveScreen(bool checked)
{
    context->config.sc.save_screenpixels = checked;
    context->config.sc_modified = true;
}

void MainWindow::slotPreventSavefile(bool checked)
{
    context->config.sc.save_screenpixels = checked;
    context->config.sc_modified = true;
}

void MainWindow::slotMovieEnd()
{
    setListFromRadio(movieEndGroup, context->config.on_movie_end);
}

void MainWindow::alertSave(void* promise)
{
    std::promise<bool>* saveAnswer = static_cast<std::promise<bool>*>(promise);
    QMessageBox::StandardButton btn = QMessageBox::question(this, "Save movie", QString("Do you want to save the movie file?"), QMessageBox::Yes | QMessageBox::No);
    saveAnswer->set_value(btn == QMessageBox::Yes);
}

void MainWindow::alertDialog(QString alert_msg)
{
    /* Pause the game */
    context->config.sc.running = false;
    context->config.sc_modified = true;

    /* Bring FLTK to foreground
     * taken from https://stackoverflow.com/a/28404920
     */
    // xcb_client_message_event_t event;
    // event.response_type = XCB_CLIENT_MESSAGE;
    // event.window = mw.context->ui_window;
    // event.format = 32;
    // event.type = XInternAtom(mw.context->display, "_NET_ACTIVE_WINDOW", False);
    //
    // xcb_intern_atom_cookie_t cookie = xcb_intern_atom (mw.context->conn, 0, 0, strlen(names[i]), names[i] );
    //     /* get response */
    // xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply (connection,
    //                                                             cookie,
    //                                                             NULL ); // normally a pointer to receive error, but we'll just ignore error handling

    // XSendEvent(mw.context->display, DefaultRootWindow(mw.context->display), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    // XMapRaised(mw.context->display, mw.context->ui_window);

    /* Show alert window */
    QMessageBox::warning(this, "Warning", alert_msg);
}
