/*
 * Hello Digitalis — Minimal Vulkan triangle renderer.
 *
 * Renders a single colored triangle to verify that ARM64 Vulkan calls
 * are correctly translated to x86_64 host Vulkan calls via the Digitalis
 * NativeBridge binary translator.
 */

#include "vulkan_renderer.h"

#include <android/log.h>
#include <vulkan/vulkan_android.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#define LOG_TAG "HelloDigitalis"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define VK_CHECK(call)                                                  \
    do {                                                                \
        VkResult result = (call);                                       \
        if (result != VK_SUCCESS) {                                     \
            LOGE("%s failed: VkResult=%d at %s:%d", #call, result,      \
                 __FILE__, __LINE__);                                   \
            return false;                                               \
        }                                                               \
    } while (0)

// Inline SPIR-V for vertex shader:
//   #version 450
//   layout(location = 0) out vec3 fragColor;
//   vec2 positions[3] = vec2[](
//       vec2( 0.0, -0.5),
//       vec2( 0.5,  0.5),
//       vec2(-0.5,  0.5)
//   );
//   vec3 colors[3] = vec3[](
//       vec3(1.0, 0.0, 0.0),
//       vec3(0.0, 1.0, 0.0),
//       vec3(0.0, 0.0, 1.0)
//   );
//   void main() {
//       gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
//       fragColor = colors[gl_VertexIndex];
//   }
static const uint32_t kVertShaderSpv[] = {
    0x07230203, 0x00010000, 0x000d000a, 0x00000038,
    0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0008000f, 0x00000000, 0x00000004, 0x6e69616d,
    0x00000000, 0x0000000d, 0x00000012, 0x0000002c,
    0x00030003, 0x00000002, 0x000001c2, 0x000a0004,
    0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70,
    0x5f656c79, 0x656e696c, 0x7269645f, 0x69746365,
    0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f,
    0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572,
    0x00657669, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00060005, 0x0000000b, 0x505f6c67,
    0x65567265, 0x78657472, 0x00000000, 0x00060006,
    0x0000000b, 0x00000000, 0x505f6c67, 0x7469736f,
    0x006e6f69, 0x00070006, 0x0000000b, 0x00000001,
    0x505f6c67, 0x746e696f, 0x657a6953, 0x00000000,
    0x00070006, 0x0000000b, 0x00000002, 0x435f6c67,
    0x4470696c, 0x61747369, 0x0065636e, 0x00070006,
    0x0000000b, 0x00000003, 0x435f6c67, 0x446c6c75,
    0x61747369, 0x0065636e, 0x00030005, 0x0000000d,
    0x00000000, 0x00060005, 0x00000012, 0x565f6c67,
    0x65747265, 0x646e4978, 0x00007865, 0x00050005,
    0x00000017, 0x69736f70, 0x6e6f6974, 0x00000073,
    0x00050005, 0x0000002c, 0x67617266, 0x6f6c6f43,
    0x00000072, 0x00040005, 0x0000002e, 0x6f6c6f63,
    0x00007372, 0x00050048, 0x0000000b, 0x00000000,
    0x0000000b, 0x00000000, 0x00050048, 0x0000000b,
    0x00000001, 0x0000000b, 0x00000001, 0x00050048,
    0x0000000b, 0x00000002, 0x0000000b, 0x00000003,
    0x00050048, 0x0000000b, 0x00000003, 0x0000000b,
    0x00000004, 0x00030047, 0x0000000b, 0x00000002,
    0x00040047, 0x00000012, 0x0000000b, 0x0000002a,
    0x00040047, 0x0000002c, 0x0000001e, 0x00000000,
    0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004,
    0x00040015, 0x00000008, 0x00000020, 0x00000000,
    0x0004002b, 0x00000008, 0x00000009, 0x00000001,
    0x0004001c, 0x0000000a, 0x00000006, 0x00000009,
    0x0006001e, 0x0000000b, 0x00000007, 0x00000006,
    0x0000000a, 0x0000000a, 0x00040020, 0x0000000c,
    0x00000003, 0x0000000b, 0x0004003b, 0x0000000c,
    0x0000000d, 0x00000003, 0x00040015, 0x0000000e,
    0x00000020, 0x00000001, 0x0004002b, 0x0000000e,
    0x0000000f, 0x00000000, 0x00040020, 0x00000011,
    0x00000001, 0x0000000e, 0x0004003b, 0x00000011,
    0x00000012, 0x00000001, 0x00040017, 0x00000014,
    0x00000006, 0x00000002, 0x0004002b, 0x00000008,
    0x00000015, 0x00000003, 0x0004001c, 0x00000016,
    0x00000014, 0x00000015, 0x00040020, 0x00000018,
    0x00000007, 0x00000016, 0x0004002b, 0x00000006,
    0x00000019, 0x00000000, 0x0004002b, 0x00000006,
    0x0000001a, 0xbf000000, 0x0005002c, 0x00000014,
    0x0000001b, 0x00000019, 0x0000001a, 0x0004002b,
    0x00000006, 0x0000001c, 0x3f000000, 0x0005002c,
    0x00000014, 0x0000001d, 0x0000001c, 0x0000001c,
    0x0005002c, 0x00000014, 0x0000001e, 0x0000001a,
    0x0000001c, 0x0006002c, 0x00000016, 0x0000001f,
    0x0000001b, 0x0000001d, 0x0000001e, 0x00040020,
    0x00000021, 0x00000007, 0x00000014, 0x0004002b,
    0x00000006, 0x00000025, 0x3f800000, 0x00040020,
    0x00000028, 0x00000003, 0x00000007, 0x00040017,
    0x0000002a, 0x00000006, 0x00000003, 0x00040020,
    0x0000002b, 0x00000003, 0x0000002a, 0x0004003b,
    0x0000002b, 0x0000002c, 0x00000003, 0x0004001c,
    0x0000002d, 0x0000002a, 0x00000015, 0x00040020,
    0x0000002f, 0x00000007, 0x0000002d, 0x0004002b,
    0x00000006, 0x00000030, 0x3f800000, 0x0006002c,
    0x0000002a, 0x00000031, 0x00000030, 0x00000019,
    0x00000019, 0x0006002c, 0x0000002a, 0x00000032,
    0x00000019, 0x00000030, 0x00000019, 0x0006002c,
    0x0000002a, 0x00000033, 0x00000019, 0x00000019,
    0x00000030, 0x0006002c, 0x0000002d, 0x00000034,
    0x00000031, 0x00000032, 0x00000033, 0x00040020,
    0x00000035, 0x00000007, 0x0000002a, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200f8, 0x00000005, 0x0004003b, 0x00000018,
    0x00000017, 0x00000007, 0x0004003b, 0x0000002f,
    0x0000002e, 0x00000007, 0x0003003e, 0x00000017,
    0x0000001f, 0x0004003d, 0x0000000e, 0x00000013,
    0x00000012, 0x00050041, 0x00000021, 0x00000022,
    0x00000017, 0x00000013, 0x0004003d, 0x00000014,
    0x00000023, 0x00000022, 0x00050051, 0x00000006,
    0x00000024, 0x00000023, 0x00000000, 0x00050051,
    0x00000006, 0x00000026, 0x00000023, 0x00000001,
    0x00070050, 0x00000007, 0x00000027, 0x00000024,
    0x00000026, 0x00000019, 0x00000025, 0x00050041,
    0x00000028, 0x00000029, 0x0000000d, 0x0000000f,
    0x0003003e, 0x00000029, 0x00000027, 0x0003003e,
    0x0000002e, 0x00000034, 0x0004003d, 0x0000000e,
    0x00000020, 0x00000012, 0x00050041, 0x00000035,
    0x00000036, 0x0000002e, 0x00000020, 0x0004003d,
    0x0000002a, 0x00000037, 0x00000036, 0x0003003e,
    0x0000002c, 0x00000037, 0x000100fd, 0x00010038,
};

// Inline SPIR-V for fragment shader:
//   #version 450
//   layout(location = 0) in vec3 fragColor;
//   layout(location = 0) out vec4 outColor;
//   void main() {
//       outColor = vec4(fragColor, 1.0);
//   }
static const uint32_t kFragShaderSpv[] = {
    0x07230203, 0x00010000, 0x000d000a, 0x00000013,
    0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d,
    0x00000000, 0x00000009, 0x0000000b, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002,
    0x000001c2, 0x000a0004, 0x475f4c47, 0x4c474f4f,
    0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c,
    0x7269645f, 0x69746365, 0x00006576, 0x00080004,
    0x475f4c47, 0x4c474f4f, 0x6e695f45, 0x64756c63,
    0x69645f65, 0x74636572, 0x00657669, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00050005,
    0x00000009, 0x4374756f, 0x726f6c6f, 0x00000000,
    0x00050005, 0x0000000b, 0x67617266, 0x6f6c6f43,
    0x00000072, 0x00040047, 0x00000009, 0x0000001e,
    0x00000000, 0x00040047, 0x0000000b, 0x0000001e,
    0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003,
    0x00000007, 0x0004003b, 0x00000008, 0x00000009,
    0x00000003, 0x00040017, 0x0000000a, 0x00000006,
    0x00000003, 0x00040020, 0x0000000c, 0x00000001,
    0x0000000a, 0x0004003b, 0x0000000c, 0x0000000b,
    0x00000001, 0x0004002b, 0x00000006, 0x0000000e,
    0x3f800000, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x0000000a, 0x0000000d, 0x0000000b,
    0x00050051, 0x00000006, 0x0000000f, 0x0000000d,
    0x00000000, 0x00050051, 0x00000006, 0x00000010,
    0x0000000d, 0x00000001, 0x00050051, 0x00000006,
    0x00000011, 0x0000000d, 0x00000002, 0x00070050,
    0x00000007, 0x00000012, 0x0000000f, 0x00000010,
    0x00000011, 0x0000000e, 0x0003003e, 0x00000009,
    0x00000012, 0x000100fd, 0x00010038,
};

static bool create_shader_module(VkDevice device, const uint32_t* code, size_t size,
                                  VkShaderModule* module) {
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = code;
    return vkCreateShaderModule(device, &create_info, nullptr, module) == VK_SUCCESS;
}

bool vulkan_init(VulkanState* state, ANativeWindow* window) {
    memset(state, 0, sizeof(*state));

    // Create instance
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "HelloDigitalis";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Digitalis";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &state->instance));
    LOGI("Vulkan instance created");

    // Pick physical device
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(state->instance, &device_count, nullptr);
    if (device_count == 0) {
        LOGE("No Vulkan devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(state->instance, &device_count, devices.data());
    state->physical_device = devices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(state->physical_device, &props);
    LOGI("Using GPU: %s", props.deviceName);

    // Find graphics queue family
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(state->physical_device, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(state->physical_device, &queue_count,
                                              queue_families.data());
    state->graphics_queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < queue_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            state->graphics_queue_family = i;
            break;
        }
    }
    if (state->graphics_queue_family == UINT32_MAX) {
        LOGE("No graphics queue family found");
        return false;
    }

    // Create logical device
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = state->graphics_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;

    VK_CHECK(vkCreateDevice(state->physical_device, &device_info, nullptr, &state->device));
    vkGetDeviceQueue(state->device, state->graphics_queue_family, 0, &state->graphics_queue);
    LOGI("Vulkan device created");

    // Create surface
    VkAndroidSurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surface_info.window = window;
    VK_CHECK(vkCreateAndroidSurfaceKHR(state->instance, &surface_info, nullptr, &state->surface));

    // Create swapchain
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state->physical_device, state->surface, &caps);

    state->swapchain_extent = caps.currentExtent;
    if (state->swapchain_extent.width == UINT32_MAX) {
        state->swapchain_extent = {
            static_cast<uint32_t>(ANativeWindow_getWidth(window)),
            static_cast<uint32_t>(ANativeWindow_getHeight(window)),
        };
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(state->physical_device, state->surface, &format_count,
                                          nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(state->physical_device, state->surface, &format_count,
                                          formats.data());
    state->swapchain_format = formats[0].format;

    state->swapchain_image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && state->swapchain_image_count > caps.maxImageCount) {
        state->swapchain_image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = state->surface;
    swapchain_info.minImageCount = state->swapchain_image_count;
    swapchain_info.imageFormat = state->swapchain_format;
    swapchain_info.imageColorSpace = formats[0].colorSpace;
    swapchain_info.imageExtent = state->swapchain_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = caps.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(state->device, &swapchain_info, nullptr, &state->swapchain));

    vkGetSwapchainImagesKHR(state->device, state->swapchain, &state->swapchain_image_count,
                             nullptr);
    state->swapchain_images = new VkImage[state->swapchain_image_count];
    vkGetSwapchainImagesKHR(state->device, state->swapchain, &state->swapchain_image_count,
                             state->swapchain_images);

    // Image views
    state->swapchain_image_views = new VkImageView[state->swapchain_image_count];
    for (uint32_t i = 0; i < state->swapchain_image_count; i++) {
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = state->swapchain_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = state->swapchain_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(state->device, &view_info, nullptr,
                                    &state->swapchain_image_views[i]));
    }

    // Render pass
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = state->swapchain_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    VK_CHECK(vkCreateRenderPass(state->device, &render_pass_info, nullptr, &state->render_pass));

    // Shader modules
    VkShaderModule vert_module, frag_module;
    if (!create_shader_module(state->device, kVertShaderSpv, sizeof(kVertShaderSpv),
                               &vert_module) ||
        !create_shader_module(state->device, kFragShaderSpv, sizeof(kFragShaderSpv),
                               &frag_module)) {
        LOGE("Failed to create shader modules");
        return false;
    }

    // Pipeline
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_module;
    shader_stages[0].pName = "main";
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_module;
    shader_stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {};
    viewport.width = (float)state->swapchain_extent.width;
    viewport.height = (float)state->swapchain_extent.height;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.extent = state->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment = {};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VK_CHECK(vkCreatePipelineLayout(state->device, &layout_info, nullptr,
                                     &state->pipeline_layout));

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.layout = state->pipeline_layout;
    pipeline_info.renderPass = state->render_pass;

    VK_CHECK(vkCreateGraphicsPipelines(state->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                        &state->pipeline));

    vkDestroyShaderModule(state->device, vert_module, nullptr);
    vkDestroyShaderModule(state->device, frag_module, nullptr);

    // Framebuffers
    state->framebuffers = new VkFramebuffer[state->swapchain_image_count];
    for (uint32_t i = 0; i < state->swapchain_image_count; i++) {
        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = state->render_pass;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &state->swapchain_image_views[i];
        fb_info.width = state->swapchain_extent.width;
        fb_info.height = state->swapchain_extent.height;
        fb_info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(state->device, &fb_info, nullptr,
                                      &state->framebuffers[i]));
    }

    // Command pool and buffer
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = state->graphics_queue_family;
    VK_CHECK(vkCreateCommandPool(state->device, &pool_info, nullptr, &state->command_pool));

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = state->command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(state->device, &alloc_info, &state->command_buffer));

    // Sync objects
    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(state->device, &sem_info, nullptr,
                                &state->image_available_semaphore));
    VK_CHECK(vkCreateSemaphore(state->device, &sem_info, nullptr,
                                &state->render_finished_semaphore));

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(state->device, &fence_info, nullptr, &state->in_flight_fence));

    state->initialized = true;
    LOGI("Vulkan initialization complete (%ux%u)", state->swapchain_extent.width,
         state->swapchain_extent.height);
    return true;
}

void vulkan_render_frame(VulkanState* state) {
    if (!state->initialized) return;

    vkWaitForFences(state->device, 1, &state->in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(state->device, 1, &state->in_flight_fence);

    uint32_t image_index;
    vkAcquireNextImageKHR(state->device, state->swapchain, UINT64_MAX,
                           state->image_available_semaphore, VK_NULL_HANDLE, &image_index);

    vkResetCommandBuffer(state->command_buffer, 0);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(state->command_buffer, &begin_info);

    VkClearValue clear_color = {{{0.1f, 0.1f, 0.2f, 1.0f}}};

    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = state->render_pass;
    rp_begin.framebuffer = state->framebuffers[image_index];
    rp_begin.renderArea.extent = state->swapchain_extent;
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_color;

    vkCmdBeginRenderPass(state->command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(state->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state->pipeline);
    vkCmdDraw(state->command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(state->command_buffer);

    vkEndCommandBuffer(state->command_buffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &state->image_available_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &state->command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &state->render_finished_semaphore;

    vkQueueSubmit(state->graphics_queue, 1, &submit_info, state->in_flight_fence);

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &state->render_finished_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &state->swapchain;
    present_info.pImageIndices = &image_index;

    vkQueuePresentKHR(state->graphics_queue, &present_info);
}

void vulkan_cleanup(VulkanState* state) {
    if (!state->initialized) return;

    vkDeviceWaitIdle(state->device);

    vkDestroyFence(state->device, state->in_flight_fence, nullptr);
    vkDestroySemaphore(state->device, state->render_finished_semaphore, nullptr);
    vkDestroySemaphore(state->device, state->image_available_semaphore, nullptr);
    vkDestroyCommandPool(state->device, state->command_pool, nullptr);

    for (uint32_t i = 0; i < state->swapchain_image_count; i++) {
        vkDestroyFramebuffer(state->device, state->framebuffers[i], nullptr);
        vkDestroyImageView(state->device, state->swapchain_image_views[i], nullptr);
    }
    delete[] state->framebuffers;
    delete[] state->swapchain_image_views;
    delete[] state->swapchain_images;

    vkDestroyPipeline(state->device, state->pipeline, nullptr);
    vkDestroyPipelineLayout(state->device, state->pipeline_layout, nullptr);
    vkDestroyRenderPass(state->device, state->render_pass, nullptr);
    vkDestroySwapchainKHR(state->device, state->swapchain, nullptr);
    vkDestroySurfaceKHR(state->instance, state->surface, nullptr);
    vkDestroyDevice(state->device, nullptr);
    vkDestroyInstance(state->instance, nullptr);

    state->initialized = false;
    LOGI("Vulkan cleanup complete");
}
