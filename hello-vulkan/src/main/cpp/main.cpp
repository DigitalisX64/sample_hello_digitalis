/*
 * Hello Digitalis — NativeActivity entry point.
 *
 * This is an ARM64-only Vulkan app used to test the Digitalis
 * binary translator (ARM64→x86_64 NativeBridge).
 */

#include <android/log.h>
#include <android_native_app_glue.h>

#include "vulkan_renderer.h"

#define LOG_TAG "HelloDigitalis"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static VulkanState g_vulkan_state = {};

static void handle_cmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                LOGI("Window initialized, starting Vulkan");
                if (!vulkan_init(&g_vulkan_state, app->window)) {
                    LOGI("Vulkan init failed, will retry");
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            vulkan_cleanup(&g_vulkan_state);
            break;
        default:
            break;
    }
}

void android_main(struct android_app* app) {
    LOGI("Hello Digitalis starting (ARM64 binary on x86_64 via NativeBridge)");

    app->onAppCmd = handle_cmd;

    while (true) {
        int events;
        struct android_poll_source* source;

        while (ALooper_pollOnce(g_vulkan_state.initialized ? 0 : -1,
                                nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                vulkan_cleanup(&g_vulkan_state);
                return;
            }
        }

        vulkan_render_frame(&g_vulkan_state);
    }
}
