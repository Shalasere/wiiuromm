#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <stdbool.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

typedef enum AppAction {
    ACTION_NONE = 0,
    ACTION_UP,
    ACTION_DOWN,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_SELECT,
    ACTION_OPEN_SEARCH,
    ACTION_OPEN_DIAGNOSTICS,
    ACTION_OPEN_UPDATER,
    ACTION_OPEN_QUEUE,
    ACTION_BACK,
    ACTION_START_DOWNLOAD,
    ACTION_QUIT
} AppAction;

typedef enum AppView {
    VIEW_PLATFORMS = 0,
    VIEW_ROMS,
    VIEW_DETAIL,
    VIEW_QUEUE,
    VIEW_DOWNLOADING,
    VIEW_DIAGNOSTICS,
    VIEW_UPDATER,
    VIEW_ERROR
} AppView;

typedef struct AppState {
    AppView current;
    AppView prevQueueView;
    AppView prevDiagnosticsView;
    AppView prevUpdaterView;
    int cursor;
    int queuedItems;
} AppState;

static const char *view_name(AppView v) {
    switch (v) {
        case VIEW_PLATFORMS: return "PLATFORMS";
        case VIEW_ROMS: return "ROMS";
        case VIEW_DETAIL: return "DETAIL";
        case VIEW_QUEUE: return "QUEUE";
        case VIEW_DOWNLOADING: return "DOWNLOADING";
        case VIEW_DIAGNOSTICS: return "DIAGNOSTICS";
        case VIEW_UPDATER: return "UPDATER";
        case VIEW_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static const char *action_name(AppAction a) {
    switch (a) {
        case ACTION_UP: return "Up";
        case ACTION_DOWN: return "Down";
        case ACTION_LEFT: return "Left";
        case ACTION_RIGHT: return "Right";
        case ACTION_SELECT: return "Select";
        case ACTION_OPEN_SEARCH: return "OpenSearch";
        case ACTION_OPEN_DIAGNOSTICS: return "OpenDiagnostics";
        case ACTION_OPEN_UPDATER: return "OpenUpdater";
        case ACTION_OPEN_QUEUE: return "OpenQueue";
        case ACTION_BACK: return "Back";
        case ACTION_START_DOWNLOAD: return "StartDownload";
        case ACTION_QUIT: return "Quit";
        case ACTION_NONE:
        default: return "None";
    }
}

static AppAction map_vpad_action(uint32_t trigger) {
    if (trigger & VPAD_BUTTON_HOME) return ACTION_QUIT;
    if (trigger & VPAD_BUTTON_PLUS) return ACTION_QUIT;
    if (trigger & VPAD_BUTTON_UP) return ACTION_UP;
    if (trigger & VPAD_BUTTON_DOWN) return ACTION_DOWN;
    if (trigger & VPAD_BUTTON_LEFT) return ACTION_LEFT;
    if (trigger & VPAD_BUTTON_RIGHT) return ACTION_RIGHT;
    if (trigger & VPAD_BUTTON_A) return ACTION_SELECT;
    if (trigger & VPAD_BUTTON_B) return ACTION_BACK;
    if (trigger & VPAD_BUTTON_Y) return ACTION_OPEN_QUEUE;
    if (trigger & VPAD_BUTTON_X) return ACTION_START_DOWNLOAD;
    if (trigger & VPAD_BUTTON_MINUS) return ACTION_OPEN_SEARCH;
    if (trigger & VPAD_BUTTON_R) return ACTION_OPEN_DIAGNOSTICS;
    if (trigger & VPAD_BUTTON_L) return ACTION_OPEN_UPDATER;
    return ACTION_NONE;
}

static bool apply_action(AppState *state, AppAction action) {
    AppView oldView = state->current;

    switch (action) {
        case ACTION_NONE:
            return true;
        case ACTION_QUIT:
            return false;
        case ACTION_UP:
            if (state->cursor > 0) state->cursor--;
            break;
        case ACTION_DOWN:
            state->cursor++;
            break;
        case ACTION_LEFT:
        case ACTION_RIGHT:
            break;
        case ACTION_OPEN_SEARCH:
            if (state->current == VIEW_ROMS) {
                WHBLogPrintf("[search] Placeholder keyboard flow");
            }
            break;
        case ACTION_OPEN_DIAGNOSTICS:
            if (state->current == VIEW_PLATFORMS) {
                state->prevDiagnosticsView = state->current;
                state->current = VIEW_DIAGNOSTICS;
            } else if (state->current == VIEW_DIAGNOSTICS) {
                WHBLogPrintf("[diag] Refresh placeholder");
            }
            break;
        case ACTION_OPEN_UPDATER:
            if (state->current == VIEW_PLATFORMS) {
                state->prevUpdaterView = state->current;
                state->current = VIEW_UPDATER;
            }
            break;
        case ACTION_OPEN_QUEUE:
            if (state->current != VIEW_QUEUE && state->current != VIEW_DOWNLOADING) {
                state->prevQueueView = state->current;
            }
            state->current = VIEW_QUEUE;
            break;
        case ACTION_BACK:
            if (state->current == VIEW_ROMS) {
                state->current = VIEW_PLATFORMS;
            } else if (state->current == VIEW_DETAIL) {
                state->current = VIEW_ROMS;
            } else if (state->current == VIEW_QUEUE) {
                state->current = state->prevQueueView;
            } else if (state->current == VIEW_DOWNLOADING) {
                state->current = VIEW_QUEUE;
            } else if (state->current == VIEW_DIAGNOSTICS) {
                state->current = state->prevDiagnosticsView;
            } else if (state->current == VIEW_UPDATER) {
                state->current = state->prevUpdaterView;
            } else if (state->current == VIEW_ERROR) {
                state->current = VIEW_PLATFORMS;
            }
            break;
        case ACTION_SELECT:
            if (state->current == VIEW_PLATFORMS) {
                state->current = VIEW_ROMS;
            } else if (state->current == VIEW_ROMS) {
                state->current = VIEW_DETAIL;
            } else if (state->current == VIEW_DETAIL) {
                state->queuedItems++;
                state->prevQueueView = VIEW_DETAIL;
                state->current = VIEW_QUEUE;
            }
            break;
        case ACTION_START_DOWNLOAD:
            if (state->current == VIEW_QUEUE && state->queuedItems > 0) {
                state->current = VIEW_DOWNLOADING;
            }
            break;
    }

    WHBLogPrintf("[input] action=%s cursor=%d queue=%d",
                 action_name(action), state->cursor, state->queuedItems);
    if (state->current != oldView) {
        WHBLogPrintf("[view] %s -> %s", view_name(oldView), view_name(state->current));
    }
    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    WHBProcInit();
    WHBLogConsoleInit();

    WHBLogPrintf("wiiuromm (Wii U target) - Switch-style state skeleton");
    WHBLogPrintf("A=Select B=Back Y=Queue X=Start - =Search R=Diag L=Updater +=Quit");
    WHBLogPrintf("DPad navigates; diagnostics/updater are platform-view gated.");

    AppState state = {
        .current = VIEW_PLATFORMS,
        .prevQueueView = VIEW_PLATFORMS,
        .prevDiagnosticsView = VIEW_PLATFORMS,
        .prevUpdaterView = VIEW_PLATFORMS,
        .cursor = 0,
        .queuedItems = 0
    };
    WHBLogPrintf("[view] %s", view_name(state.current));

    while (WHBProcIsRunning()) {
        VPADStatus status;
        VPADReadError err;
        VPADRead(VPAD_CHAN_0, &status, 1, &err);
        if (err == VPAD_READ_SUCCESS) {
            AppAction action = map_vpad_action(status.trigger);
            if (!apply_action(&state, action)) {
                break;
            }
        }
        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
