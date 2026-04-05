/*
 * Hello Digitalis — Minimal Vulkan triangle renderer for testing ARM64→x86_64 translation.
 *
 * This app renders a colored triangle using the Vulkan API. It is built for ARM64 only
 * (arm64-v8a ABI) and requires a NativeBridge binary translator to run on x86_64 devices.
 */

#ifndef HELLO_DIGITALIS_VULKAN_RENDERER_H
#define HELLO_DIGITALIS_VULKAN_RENDERER_H

#include <vulkan/vulkan.h>
#include <android/native_window.h>

struct VulkanState {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;

    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;

    uint32_t swapchain_image_count;
    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;
    VkFramebuffer* framebuffers;

    bool initialized;
};

bool vulkan_init(VulkanState* state, ANativeWindow* window);
void vulkan_render_frame(VulkanState* state);
void vulkan_cleanup(VulkanState* state);

#endif  // HELLO_DIGITALIS_VULKAN_RENDERER_H
