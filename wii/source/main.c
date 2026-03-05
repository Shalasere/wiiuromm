#include <gccore.h>
#include <stdio.h>
#include <wiiuse/wpad.h>

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

    printf("wiiuromm (Wii/vWii target) skeleton\n");
    printf("Press HOME on Wii Remote to exit.\n");

    while (1) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        if (pressed & WPAD_BUTTON_HOME) {
            break;
        }
        VIDEO_WaitVSync();
    }

    return 0;
}
