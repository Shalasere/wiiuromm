#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    WHBProcInit();
    WHBLogConsoleInit();

    WHBLogPrintf("wiiuromm (Wii U target) skeleton");
    WHBLogPrintf("Press HOME on GamePad to exit.");

    while (WHBProcIsRunning()) {
        VPADStatus status;
        VPADReadError err;
        VPADRead(VPAD_CHAN_0, &status, 1, &err);
        if (err == VPAD_READ_SUCCESS && (status.trigger & VPAD_BUTTON_HOME)) {
            break;
        }
        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
