#include <gccore.h>
#include <ogc/pad.h>
#include <stdbool.h>
#include <stdio.h>
#include <wiiuse/wpad.h>

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

static AppAction map_wpad_action(u32 pressed) {
    if (pressed & WPAD_BUTTON_HOME) return ACTION_QUIT;
    if (pressed & WPAD_BUTTON_PLUS) return ACTION_QUIT;
    if (pressed & WPAD_BUTTON_UP) return ACTION_UP;
    if (pressed & WPAD_BUTTON_DOWN) return ACTION_DOWN;
    if (pressed & WPAD_BUTTON_LEFT) return ACTION_LEFT;
    if (pressed & WPAD_BUTTON_RIGHT) return ACTION_RIGHT;
    if (pressed & WPAD_BUTTON_A) return ACTION_SELECT;
    if (pressed & WPAD_BUTTON_B) return ACTION_BACK;
    if (pressed & WPAD_BUTTON_1) return ACTION_OPEN_QUEUE;
    if (pressed & WPAD_BUTTON_2) return ACTION_START_DOWNLOAD;
    if (pressed & WPAD_BUTTON_MINUS) return ACTION_OPEN_SEARCH;
    return ACTION_NONE;
}

static AppAction map_gc_action(u16 pressed) {
    if (pressed & PAD_BUTTON_START) return ACTION_QUIT;
    if (pressed & PAD_BUTTON_UP) return ACTION_UP;
    if (pressed & PAD_BUTTON_DOWN) return ACTION_DOWN;
    if (pressed & PAD_BUTTON_LEFT) return ACTION_LEFT;
    if (pressed & PAD_BUTTON_RIGHT) return ACTION_RIGHT;
    if (pressed & PAD_BUTTON_A) return ACTION_SELECT;
    if (pressed & PAD_BUTTON_B) return ACTION_BACK;
    if (pressed & PAD_BUTTON_Y) return ACTION_OPEN_QUEUE;
    if (pressed & PAD_BUTTON_X) return ACTION_START_DOWNLOAD;
    if (pressed & PAD_TRIGGER_R) return ACTION_OPEN_SEARCH;
    if (pressed & PAD_TRIGGER_Z) return ACTION_OPEN_DIAGNOSTICS;
    if (pressed & PAD_TRIGGER_L) return ACTION_OPEN_UPDATER;
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
            break;
        case ACTION_OPEN_DIAGNOSTICS:
            if (state->current == VIEW_PLATFORMS) {
                state->prevDiagnosticsView = state->current;
                state->current = VIEW_DIAGNOSTICS;
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

    if (state->current != oldView) {
        printf("View: %s -> %s\n", view_name(oldView), view_name(state->current));
    }
    return true;
}

static void draw_ui(const AppState *state) {
    printf("\x1b[2J\x1b[H");
    printf("wiiuromm (Wii/vWii) - Switch-style state skeleton\n");
    printf("Current view : %s\n", view_name(state->current));
    printf("Cursor       : %d\n", state->cursor);
    printf("Queued items : %d\n", state->queuedItems);
    printf("\nWii Remote: A=Select B=Back 1=Queue 2=Start -=Search +=Quit HOME=Quit\n");
    printf("GameCube  : A=Select B=Back Y=Queue X=Start L=Updater Z=Diag R=Search START=Quit\n");
}

static void init_video_console(void) {
    VIDEO_Init();

    GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
}

int main(void) {
    init_video_console();
    WPAD_Init();
    PAD_Init();

    AppState state = {
        .current = VIEW_PLATFORMS,
        .prevQueueView = VIEW_PLATFORMS,
        .prevDiagnosticsView = VIEW_PLATFORMS,
        .prevUpdaterView = VIEW_PLATFORMS,
        .cursor = 0,
        .queuedItems = 0
    };
    draw_ui(&state);

    while (1) {
        WPAD_ScanPads();
        PAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        u16 gcPressed = PAD_ButtonsDown(0);

        AppAction action = map_wpad_action(pressed);
        if (action == ACTION_NONE) {
            action = map_gc_action(gcPressed);
        }
        if (!apply_action(&state, action)) {
            break;
        }
        if (action != ACTION_NONE) {
            draw_ui(&state);
        }
        VIDEO_WaitVSync();
    }

    return 0;
}
