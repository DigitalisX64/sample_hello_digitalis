// Minimal Vulkan single-channel (R8) tiled-texture integrity probe.
//
// Chromium GPU-rasterizes glyphs into an R8 (alpha/coverage) atlas stored as a
// VK_IMAGE_TILING_OPTIMAL image, then samples it. Under Digitalis the guest's
// Vulkan calls are proxied to the host gfxstream driver, which performs the
// linear<->tiled memory conversion. If that conversion mishandles an R8 tiled
// image (wrong tile geometry / row pitch), glyph pixels land in the wrong place
// — the "block-displaced glyph" corruption seen in Helium.
//
// This probe isolates that path with no shaders or swapchain: it fills a host
// buffer with a deterministic pattern, uploads it into an OPTIMAL-tiled R8 image
// (vkCmdCopyBufferToImage), copies it back out (vkCmdCopyImageToBuffer), and
// compares the round-trip bytes against the original. A clean translator + GPU
// stack returns the exact pattern; a tiling/pitch bug returns displaced bytes.
// Several sizes (including tile-unaligned dims) are tested. The result is shown
// on screen and logged; the StatusTest just verifies the process doesn't crash.

#include <android/log.h>
#include <jni.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define LOG_TAG "hellovktexture"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// Fullscreen-triangle vertex shader (emits uv 0..1) and a fragment shader that
// samples a sampler2D at uv and writes the .r value into the RGBA output's red
// channel. Precompiled from GLSL with glslc -O.
static const uint32_t kVertSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x0000002c, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0008000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000c, 0x0000001d,
    0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000c, 0x0000000b, 0x0000002a,
    0x00030047, 0x0000001b, 0x00000002, 0x00050048, 0x0000001b, 0x00000000, 0x0000000b, 0x00000000,
    0x00050048, 0x0000001b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000001b, 0x00000002,
    0x0000000b, 0x00000003, 0x00050048, 0x0000001b, 0x00000003, 0x0000000b, 0x00000004, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000007, 0x00000006, 0x00000002, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
    0x00000008, 0x00000009, 0x00000003, 0x00040015, 0x0000000a, 0x00000020, 0x00000001, 0x00040020,
    0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b, 0x0000000c, 0x00000001, 0x0004002b,
    0x0000000a, 0x0000000e, 0x00000001, 0x0004002b, 0x0000000a, 0x00000010, 0x00000002, 0x00040017,
    0x00000017, 0x00000006, 0x00000004, 0x00040015, 0x00000018, 0x00000020, 0x00000000, 0x0004002b,
    0x00000018, 0x00000019, 0x00000001, 0x0004001c, 0x0000001a, 0x00000006, 0x00000019, 0x0006001e,
    0x0000001b, 0x00000017, 0x00000006, 0x0000001a, 0x0000001a, 0x00040020, 0x0000001c, 0x00000003,
    0x0000001b, 0x0004003b, 0x0000001c, 0x0000001d, 0x00000003, 0x0004002b, 0x0000000a, 0x0000001e,
    0x00000000, 0x0004002b, 0x00000006, 0x00000020, 0x40000000, 0x0004002b, 0x00000006, 0x00000022,
    0x3f800000, 0x0004002b, 0x00000006, 0x00000025, 0x00000000, 0x00040020, 0x00000029, 0x00000003,
    0x00000017, 0x0005002c, 0x00000007, 0x0000002b, 0x00000022, 0x00000022, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d,
    0x0000000c, 0x000500c4, 0x0000000a, 0x0000000f, 0x0000000d, 0x0000000e, 0x000500c7, 0x0000000a,
    0x00000011, 0x0000000f, 0x00000010, 0x0004006f, 0x00000006, 0x00000012, 0x00000011, 0x000500c7,
    0x0000000a, 0x00000014, 0x0000000d, 0x00000010, 0x0004006f, 0x00000006, 0x00000015, 0x00000014,
    0x00050050, 0x00000007, 0x00000016, 0x00000012, 0x00000015, 0x0003003e, 0x00000009, 0x00000016,
    0x0004003d, 0x00000007, 0x0000001f, 0x00000009, 0x0005008e, 0x00000007, 0x00000021, 0x0000001f,
    0x00000020, 0x00050083, 0x00000007, 0x00000024, 0x00000021, 0x0000002b, 0x00050051, 0x00000006,
    0x00000026, 0x00000024, 0x00000000, 0x00050051, 0x00000006, 0x00000027, 0x00000024, 0x00000001,
    0x00070050, 0x00000017, 0x00000028, 0x00000026, 0x00000027, 0x00000025, 0x00000022, 0x00050041,
    0x00000029, 0x0000002a, 0x0000001d, 0x0000001e, 0x0003003e, 0x0000002a, 0x00000028, 0x000100fd,
    0x00010038,
};
static const uint32_t kFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x0000001d, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000010, 0x00000018, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x0000000c, 0x00000021, 0x00000000, 0x00040047, 0x0000000c,
    0x00000022, 0x00000000, 0x00040047, 0x00000010, 0x0000001e, 0x00000000, 0x00040047, 0x00000018,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00090019, 0x00000009, 0x00000006, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000001, 0x00000000, 0x0003001b, 0x0000000a, 0x00000009, 0x00040020, 0x0000000b,
    0x00000000, 0x0000000a, 0x0004003b, 0x0000000b, 0x0000000c, 0x00000000, 0x00040017, 0x0000000e,
    0x00000006, 0x00000002, 0x00040020, 0x0000000f, 0x00000001, 0x0000000e, 0x0004003b, 0x0000000f,
    0x00000010, 0x00000001, 0x00040017, 0x00000012, 0x00000006, 0x00000004, 0x00040020, 0x00000017,
    0x00000003, 0x00000012, 0x0004003b, 0x00000017, 0x00000018, 0x00000003, 0x0004002b, 0x00000006,
    0x0000001a, 0x00000000, 0x0004002b, 0x00000006, 0x0000001b, 0x3f800000, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d,
    0x0000000c, 0x0004003d, 0x0000000e, 0x00000011, 0x00000010, 0x00050057, 0x00000012, 0x00000013,
    0x0000000d, 0x00000011, 0x00050051, 0x00000006, 0x00000016, 0x00000013, 0x00000000, 0x00070050,
    0x00000012, 0x0000001c, 0x00000016, 0x0000001a, 0x0000001a, 0x0000001b, 0x0003003e, 0x00000018,
    0x0000001c, 0x000100fd, 0x00010038,
};

// Fragment shader that computes the test pattern (x*131 + y*17) & 0xFF from
// gl_FragCoord and writes it to the red channel — used to RASTERIZE a pattern
// directly into an R8 color attachment (the glyph-atlas fill path).
static const uint32_t kGenR8Spv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x0000002b, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000c, 0x00000022, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x0000000c, 0x0000000b, 0x0000000f, 0x00040047, 0x00000022,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015,
    0x00000006, 0x00000020, 0x00000000, 0x00030016, 0x00000009, 0x00000020, 0x00040017, 0x0000000a,
    0x00000009, 0x00000004, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,
    0x0000000c, 0x00000001, 0x0004002b, 0x00000006, 0x0000000d, 0x00000000, 0x00040020, 0x0000000e,
    0x00000001, 0x00000009, 0x0004002b, 0x00000006, 0x00000013, 0x00000001, 0x0004002b, 0x00000006,
    0x00000019, 0x00000083, 0x0004002b, 0x00000006, 0x0000001c, 0x00000011, 0x0004002b, 0x00000006,
    0x0000001f, 0x000000ff, 0x00040020, 0x00000021, 0x00000003, 0x0000000a, 0x0004003b, 0x00000021,
    0x00000022, 0x00000003, 0x0004002b, 0x00000009, 0x00000027, 0x00000000, 0x0004002b, 0x00000009,
    0x00000028, 0x3f800000, 0x0004002b, 0x00000009, 0x0000002a, 0x3b808081, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x0000000e, 0x0000000f,
    0x0000000c, 0x0000000d, 0x0004003d, 0x00000009, 0x00000010, 0x0000000f, 0x0004006d, 0x00000006,
    0x00000011, 0x00000010, 0x00050041, 0x0000000e, 0x00000014, 0x0000000c, 0x00000013, 0x0004003d,
    0x00000009, 0x00000015, 0x00000014, 0x0004006d, 0x00000006, 0x00000016, 0x00000015, 0x00050084,
    0x00000006, 0x0000001a, 0x00000011, 0x00000019, 0x00050084, 0x00000006, 0x0000001d, 0x00000016,
    0x0000001c, 0x00050080, 0x00000006, 0x0000001e, 0x0000001a, 0x0000001d, 0x000500c7, 0x00000006,
    0x00000020, 0x0000001e, 0x0000001f, 0x00040070, 0x00000009, 0x00000024, 0x00000020, 0x00050085,
    0x00000009, 0x00000026, 0x00000024, 0x0000002a, 0x00070050, 0x0000000a, 0x00000029, 0x00000026,
    0x00000027, 0x00000027, 0x00000028, 0x0003003e, 0x00000022, 0x00000029, 0x000100fd, 0x00010038,
};

struct Vk {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkPhysicalDeviceMemoryProperties mem_props{};
};

bool init_vk(Vk* vk, std::string* err) {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "hellovktexture";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  if (vkCreateInstance(&ici, nullptr, &vk->instance) != VK_SUCCESS) {
    *err = "vkCreateInstance failed";
    return false;
  }
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(vk->instance, &n, nullptr);
  if (n == 0) {
    *err = "no Vulkan physical devices";
    return false;
  }
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(vk->instance, &n, devs.data());
  vk->phys = devs[0];
  VkPhysicalDeviceProperties pdp{};
  vkGetPhysicalDeviceProperties(vk->phys, &pdp);
  LOGI("GPU: %s", pdp.deviceName);

  uint32_t qn = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &qn, nullptr);
  std::vector<VkQueueFamilyProperties> qfs(qn);
  vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &qn, qfs.data());
  bool found = false;
  for (uint32_t i = 0; i < qn; i++) {
    if (qfs[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT)) {
      vk->queue_family = i;
      found = true;
      break;
    }
  }
  if (!found) {
    *err = "no transfer-capable queue family";
    return false;
  }
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = vk->queue_family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  if (vkCreateDevice(vk->phys, &dci, nullptr, &vk->device) != VK_SUCCESS) {
    *err = "vkCreateDevice failed";
    return false;
  }
  vkGetDeviceQueue(vk->device, vk->queue_family, 0, &vk->queue);
  vkGetPhysicalDeviceMemoryProperties(vk->phys, &vk->mem_props);
  return true;
}

int find_mem(const Vk& vk, uint32_t type_bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < vk.mem_props.memoryTypeCount; i++) {
    if ((type_bits & (1u << i)) &&
        (vk.mem_props.memoryTypes[i].propertyFlags & want) == want) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool make_buffer(const Vk& vk, VkDeviceSize size, VkBufferUsageFlags usage,
                 VkMemoryPropertyFlags mem_want, VkBuffer* buf, VkDeviceMemory* mem) {
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(vk.device, &bci, nullptr, buf) != VK_SUCCESS) return false;
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(vk.device, *buf, &req);
  int mt = find_mem(vk, req.memoryTypeBits, mem_want);
  if (mt < 0) return false;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = static_cast<uint32_t>(mt);
  if (vkAllocateMemory(vk.device, &mai, nullptr, mem) != VK_SUCCESS) return false;
  vkBindBufferMemory(vk.device, *buf, *mem, 0);
  return true;
}

// One round-trip: host pattern -> staging -> OPTIMAL R8 image -> readback -> compare.
// Returns mismatch count (0 == clean); sets first-mismatch details.
int roundtrip(const Vk& vk, uint32_t W, uint32_t H, int seed, int* fx, int* fy,
              int* fexp, int* fgot) {
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(W) * H;
  std::vector<uint8_t> pattern(bytes);
  for (uint32_t y = 0; y < H; y++) {
    for (uint32_t x = 0; x < W; x++) {
      pattern[y * W + x] = static_cast<uint8_t>((x * 131u + y * 17u + seed * 7u) & 0xFFu);
    }
  }

  VkBuffer staging = VK_NULL_HANDLE, readback = VK_NULL_HANDLE;
  VkDeviceMemory staging_mem = VK_NULL_HANDLE, readback_mem = VK_NULL_HANDLE;
  const VkMemoryPropertyFlags host =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  if (!make_buffer(vk, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, &staging, &staging_mem))
    return -1;
  if (!make_buffer(vk, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, &readback, &readback_mem))
    return -1;

  void* p = nullptr;
  vkMapMemory(vk.device, staging_mem, 0, bytes, 0, &p);
  memcpy(p, pattern.data(), bytes);
  vkUnmapMemory(vk.device, staging_mem);

  // OPTIMAL-tiled R8 image.
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory image_mem = VK_NULL_HANDLE;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8_UNORM;
  ici.extent = {W, H, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(vk.device, &ici, nullptr, &image) != VK_SUCCESS) return -1;
  VkMemoryRequirements ireq{};
  vkGetImageMemoryRequirements(vk.device, image, &ireq);
  int imt = find_mem(vk, ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (imt < 0) imt = find_mem(vk, ireq.memoryTypeBits, 0);
  VkMemoryAllocateInfo imai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  imai.allocationSize = ireq.size;
  imai.memoryTypeIndex = static_cast<uint32_t>(imt);
  if (vkAllocateMemory(vk.device, &imai, nullptr, &image_mem) != VK_SUCCESS) return -1;
  vkBindImageMemory(vk.device, image, image_mem, 0);

  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = vk.queue_family;
  vkCreateCommandPool(vk.device, &pci, nullptr, &pool);
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  vkAllocateCommandBuffers(vk.device, &cbai, &cmd);

  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);

  auto barrier = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags src,
                     VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  };

  barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;    // tightly packed
  region.bufferImageHeight = 0;  // tightly packed
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {W, H, 1};
  vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
  vkEndCommandBuffer(cmd);

  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkCreateFence(vk.device, &fci, nullptr, &fence);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(vk.queue, 1, &si, fence);
  vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);

  void* rb = nullptr;
  vkMapMemory(vk.device, readback_mem, 0, bytes, 0, &rb);
  const uint8_t* got = static_cast<const uint8_t*>(rb);
  int mismatches = 0;
  for (uint32_t i = 0; i < bytes; i++) {
    if (got[i] != pattern[i]) {
      if (mismatches == 0) {
        *fx = static_cast<int>(i % W);
        *fy = static_cast<int>(i / W);
        *fexp = pattern[i];
        *fgot = got[i];
      }
      mismatches++;
    }
  }
  vkUnmapMemory(vk.device, readback_mem);

  vkDestroyFence(vk.device, fence, nullptr);
  vkDestroyCommandPool(vk.device, pool, nullptr);
  vkDestroyImage(vk.device, image, nullptr);
  vkFreeMemory(vk.device, image_mem, nullptr);
  vkDestroyBuffer(vk.device, staging, nullptr);
  vkFreeMemory(vk.device, staging_mem, nullptr);
  vkDestroyBuffer(vk.device, readback, nullptr);
  vkFreeMemory(vk.device, readback_mem, nullptr);
  return mismatches;
}

// Sampled round-trip: fill an OPTIMAL R8 image (the glyph atlas), SAMPLE it in a
// fragment shader onto a WxH RGBA8 render target with NEAREST 1:1 mapping, read
// the target back, and check each output pixel's red channel equals the source
// texel. This exercises the image-view / sampler / descriptor-set / graphics-
// pipeline proxy paths the plain transfer test never touches — the path Chromium
// uses to draw its glyph atlas. Returns mismatch count (0 == clean).
int sampled_roundtrip(const Vk& vk, uint32_t W, uint32_t H, int seed, int* fx, int* fy,
                      int* fexp, int* fgot) {
  const VkDeviceSize tex_bytes = static_cast<VkDeviceSize>(W) * H;
  const VkDeviceSize rgba_bytes = tex_bytes * 4;
  std::vector<uint8_t> pattern(tex_bytes);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
      pattern[y * W + x] = static_cast<uint8_t>((x * 131u + y * 17u + seed * 7u) & 0xFFu);

  const VkMemoryPropertyFlags host =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VkBuffer staging = VK_NULL_HANDLE, readback = VK_NULL_HANDLE;
  VkDeviceMemory staging_mem = VK_NULL_HANDLE, readback_mem = VK_NULL_HANDLE;
  if (!make_buffer(vk, tex_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, &staging, &staging_mem))
    return -1;
  if (!make_buffer(vk, rgba_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, &readback, &readback_mem))
    return -1;
  void* p = nullptr;
  vkMapMemory(vk.device, staging_mem, 0, tex_bytes, 0, &p);
  memcpy(p, pattern.data(), tex_bytes);
  vkUnmapMemory(vk.device, staging_mem);

  auto make_image = [&](VkFormat fmt, VkImageUsageFlags usage, VkImage* img,
                        VkDeviceMemory* mem) -> bool {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {W, H, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk.device, &ici, nullptr, img) != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(vk.device, *img, &req);
    int mt = find_mem(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt < 0) mt = find_mem(vk, req.memoryTypeBits, 0);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = static_cast<uint32_t>(mt);
    if (vkAllocateMemory(vk.device, &mai, nullptr, mem) != VK_SUCCESS) return false;
    vkBindImageMemory(vk.device, *img, *mem, 0);
    return true;
  };

  VkImage tex = VK_NULL_HANDLE, color = VK_NULL_HANDLE;
  VkDeviceMemory tex_mem = VK_NULL_HANDLE, color_mem = VK_NULL_HANDLE;
  if (!make_image(VK_FORMAT_R8_UNORM,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &tex, &tex_mem))
    return -1;
  if (!make_image(VK_FORMAT_R8G8B8A8_UNORM,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &color,
                  &color_mem))
    return -1;

  auto make_view = [&](VkImage img, VkFormat fmt) -> VkImageView {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView v = VK_NULL_HANDLE;
    vkCreateImageView(vk.device, &vi, nullptr, &v);
    return v;
  };
  VkImageView tex_view = make_view(tex, VK_FORMAT_R8_UNORM);
  VkImageView color_view = make_view(color, VK_FORMAT_R8G8B8A8_UNORM);

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VkSampler sampler = VK_NULL_HANDLE;
  vkCreateSampler(vk.device, &sci, nullptr, &sampler);

  // Render pass: one RGBA8 color attachment, final layout TRANSFER_SRC.
  VkAttachmentDescription att{};
  att.format = VK_FORMAT_R8G8B8A8_UNORM;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ar;
  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &att;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &sub;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  vkCreateRenderPass(vk.device, &rpci, nullptr, &render_pass);

  VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbci.renderPass = render_pass;
  fbci.attachmentCount = 1;
  fbci.pAttachments = &color_view;
  fbci.width = W;
  fbci.height = H;
  fbci.layers = 1;
  VkFramebuffer fb = VK_NULL_HANDLE;
  vkCreateFramebuffer(vk.device, &fbci, nullptr, &fb);

  // Descriptor set: binding 0 = combined image sampler.
  VkDescriptorSetLayoutBinding dslb{};
  dslb.binding = 0;
  dslb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  dslb.descriptorCount = 1;
  dslb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dslci.bindingCount = 1;
  dslci.pBindings = &dslb;
  VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
  vkCreateDescriptorSetLayout(vk.device, &dslci, nullptr, &dsl);
  VkDescriptorPoolSize dps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &dps;
  VkDescriptorPool dpool = VK_NULL_HANDLE;
  vkCreateDescriptorPool(vk.device, &dpci, nullptr, &dpool);
  VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = dpool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &dsl;
  VkDescriptorSet dset = VK_NULL_HANDLE;
  vkAllocateDescriptorSets(vk.device, &dsai, &dset);
  VkDescriptorImageInfo dii{sampler, tex_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  wds.dstSet = dset;
  wds.dstBinding = 0;
  wds.descriptorCount = 1;
  wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  wds.pImageInfo = &dii;
  vkUpdateDescriptorSets(vk.device, 1, &wds, 0, nullptr);

  auto make_shader = [&](const uint32_t* code, size_t bytes) -> VkShaderModule {
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = bytes;
    smci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(vk.device, &smci, nullptr, &m);
    return m;
  };
  VkShaderModule vs = make_shader(kVertSpv, sizeof(kVertSpv));
  VkShaderModule fs = make_shader(kFragSpv, sizeof(kFragSpv));

  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &dsl;
  VkPipelineLayout pl = VK_NULL_HANDLE;
  vkCreatePipelineLayout(vk.device, &plci, nullptr, &pl);

  VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vis{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ias{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport vp{0, 0, (float)W, (float)H, 0, 1};
  VkRect2D sc{{0, 0}, {W, H}};
  VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vps.viewportCount = 1;
  vps.pViewports = &vp;
  vps.scissorCount = 1;
  vps.pScissors = &sc;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cbs.attachmentCount = 1;
  cbs.pAttachments = &cba;
  VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vis;
  gpci.pInputAssemblyState = &ias;
  gpci.pViewportState = &vps;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &cbs;
  gpci.layout = pl;
  gpci.renderPass = render_pass;
  VkPipeline pipe = VK_NULL_HANDLE;
  vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe);

  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = vk.queue_family;
  vkCreateCommandPool(vk.device, &pci, nullptr, &pool);
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  vkAllocateCommandBuffers(vk.device, &cbai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);

  // Upload pattern into the R8 texture, transition to SHADER_READ_ONLY.
  auto img_barrier = [&](VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
                         VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  };
  img_barrier(tex, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
              VK_ACCESS_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {W, H, 1};
  vkCmdCopyBufferToImage(cmd, staging, tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  img_barrier(tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

  VkClearValue clear{};
  VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpbi.renderPass = render_pass;
  rpbi.framebuffer = fb;
  rpbi.renderArea = {{0, 0}, {W, H}};
  rpbi.clearValueCount = 1;
  rpbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &dset, 0, nullptr);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(cmd);

  VkBufferImageCopy creg{};
  creg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  creg.imageExtent = {W, H, 1};
  vkCmdCopyImageToBuffer(cmd, color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &creg);
  vkEndCommandBuffer(cmd);

  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkCreateFence(vk.device, &fci, nullptr, &fence);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(vk.queue, 1, &si, fence);
  vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);

  void* rb = nullptr;
  vkMapMemory(vk.device, readback_mem, 0, rgba_bytes, 0, &rb);
  const uint8_t* got = static_cast<const uint8_t*>(rb);
  int mismatches = 0;
  for (uint32_t i = 0; i < tex_bytes; i++) {
    uint8_t r = got[i * 4];  // red channel of pixel i
    if (r != pattern[i]) {
      if (mismatches == 0) {
        *fx = static_cast<int>(i % W);
        *fy = static_cast<int>(i / W);
        *fexp = pattern[i];
        *fgot = r;
      }
      mismatches++;
    }
  }
  vkUnmapMemory(vk.device, readback_mem);

  vkDestroyFence(vk.device, fence, nullptr);
  vkDestroyCommandPool(vk.device, pool, nullptr);
  vkDestroyPipeline(vk.device, pipe, nullptr);
  vkDestroyPipelineLayout(vk.device, pl, nullptr);
  vkDestroyShaderModule(vk.device, vs, nullptr);
  vkDestroyShaderModule(vk.device, fs, nullptr);
  vkDestroyDescriptorPool(vk.device, dpool, nullptr);
  vkDestroyDescriptorSetLayout(vk.device, dsl, nullptr);
  vkDestroyFramebuffer(vk.device, fb, nullptr);
  vkDestroyRenderPass(vk.device, render_pass, nullptr);
  vkDestroySampler(vk.device, sampler, nullptr);
  vkDestroyImageView(vk.device, tex_view, nullptr);
  vkDestroyImageView(vk.device, color_view, nullptr);
  vkDestroyImage(vk.device, tex, nullptr);
  vkFreeMemory(vk.device, tex_mem, nullptr);
  vkDestroyImage(vk.device, color, nullptr);
  vkFreeMemory(vk.device, color_mem, nullptr);
  vkDestroyBuffer(vk.device, staging, nullptr);
  vkFreeMemory(vk.device, staging_mem, nullptr);
  vkDestroyBuffer(vk.device, readback, nullptr);
  vkFreeMemory(vk.device, readback_mem, nullptr);
  return mismatches;
}

// Rasterize the pattern directly INTO an R8 color attachment (the glyph-atlas
// FILL path), then read it back and compare. R8 as a render target has tighter
// format-feature and layout/tiling requirements than R8-as-sampled-texture; this
// is the closest match to how Chromium GPU-rasterizes glyph coverage into its
// atlas. Returns mismatch count (0 == clean), -1 setup fail, -2 R8 unsupported.
int render_to_r8(const Vk& vk, uint32_t W, uint32_t H, int* fx, int* fy, int* fexp, int* fgot) {
  VkFormatProperties fp{};
  vkGetPhysicalDeviceFormatProperties(vk.phys, VK_FORMAT_R8_UNORM, &fp);
  if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) return -2;

  const VkDeviceSize bytes = static_cast<VkDeviceSize>(W) * H;
  std::vector<uint8_t> pattern(bytes);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
      pattern[y * W + x] = static_cast<uint8_t>((x * 131u + y * 17u) & 0xFFu);

  VkImage img = VK_NULL_HANDLE;
  VkDeviceMemory img_mem = VK_NULL_HANDLE;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8_UNORM;
  ici.extent = {W, H, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(vk.device, &ici, nullptr, &img) != VK_SUCCESS) return -1;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(vk.device, img, &req);
  int mt = find_mem(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mt < 0) mt = find_mem(vk, req.memoryTypeBits, 0);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = static_cast<uint32_t>(mt);
  if (vkAllocateMemory(vk.device, &mai, nullptr, &img_mem) != VK_SUCCESS) return -1;
  vkBindImageMemory(vk.device, img, img_mem, 0);

  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = img;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = VK_FORMAT_R8_UNORM;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView view = VK_NULL_HANDLE;
  vkCreateImageView(vk.device, &vi, nullptr, &view);

  VkBuffer readback = VK_NULL_HANDLE;
  VkDeviceMemory readback_mem = VK_NULL_HANDLE;
  if (!make_buffer(vk, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &readback, &readback_mem))
    return -1;

  VkAttachmentDescription att{};
  att.format = VK_FORMAT_R8_UNORM;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ar;
  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &att;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &sub;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  vkCreateRenderPass(vk.device, &rpci, nullptr, &render_pass);
  VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbci.renderPass = render_pass;
  fbci.attachmentCount = 1;
  fbci.pAttachments = &view;
  fbci.width = W;
  fbci.height = H;
  fbci.layers = 1;
  VkFramebuffer fb = VK_NULL_HANDLE;
  vkCreateFramebuffer(vk.device, &fbci, nullptr, &fb);

  auto make_shader = [&](const uint32_t* code, size_t sz) -> VkShaderModule {
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = sz;
    smci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(vk.device, &smci, nullptr, &m);
    return m;
  };
  VkShaderModule vs = make_shader(kVertSpv, sizeof(kVertSpv));
  VkShaderModule fs = make_shader(kGenR8Spv, sizeof(kGenR8Spv));
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  VkPipelineLayout pl = VK_NULL_HANDLE;
  vkCreatePipelineLayout(vk.device, &plci, nullptr, &pl);

  VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vis{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ias{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport vp{0, 0, (float)W, (float)H, 0, 1};
  VkRect2D sc{{0, 0}, {W, H}};
  VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vps.viewportCount = 1;
  vps.pViewports = &vp;
  vps.scissorCount = 1;
  vps.pScissors = &sc;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
  VkPipelineColorBlendStateCreateInfo cbs{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cbs.attachmentCount = 1;
  cbs.pAttachments = &cba;
  VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vis;
  gpci.pInputAssemblyState = &ias;
  gpci.pViewportState = &vps;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &cbs;
  gpci.layout = pl;
  gpci.renderPass = render_pass;
  VkPipeline pipe = VK_NULL_HANDLE;
  vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe);

  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = vk.queue_family;
  vkCreateCommandPool(vk.device, &pci, nullptr, &pool);
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  vkAllocateCommandBuffers(vk.device, &cbai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  VkClearValue clear{};
  VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpbi.renderPass = render_pass;
  rpbi.framebuffer = fb;
  rpbi.renderArea = {{0, 0}, {W, H}};
  rpbi.clearValueCount = 1;
  rpbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(cmd);
  VkBufferImageCopy creg{};
  creg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  creg.imageExtent = {W, H, 1};
  vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &creg);
  vkEndCommandBuffer(cmd);

  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkCreateFence(vk.device, &fci, nullptr, &fence);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(vk.queue, 1, &si, fence);
  vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);

  void* rb = nullptr;
  vkMapMemory(vk.device, readback_mem, 0, bytes, 0, &rb);
  const uint8_t* got = static_cast<const uint8_t*>(rb);
  int mismatches = 0;
  for (uint32_t i = 0; i < bytes; i++) {
    if (got[i] != pattern[i]) {
      if (mismatches == 0) {
        *fx = static_cast<int>(i % W);
        *fy = static_cast<int>(i / W);
        *fexp = pattern[i];
        *fgot = got[i];
      }
      mismatches++;
    }
  }
  vkUnmapMemory(vk.device, readback_mem);

  vkDestroyFence(vk.device, fence, nullptr);
  vkDestroyCommandPool(vk.device, pool, nullptr);
  vkDestroyPipeline(vk.device, pipe, nullptr);
  vkDestroyPipelineLayout(vk.device, pl, nullptr);
  vkDestroyShaderModule(vk.device, vs, nullptr);
  vkDestroyShaderModule(vk.device, fs, nullptr);
  vkDestroyFramebuffer(vk.device, fb, nullptr);
  vkDestroyRenderPass(vk.device, render_pass, nullptr);
  vkDestroyImageView(vk.device, view, nullptr);
  vkDestroyImage(vk.device, img, nullptr);
  vkFreeMemory(vk.device, img_mem, nullptr);
  vkDestroyBuffer(vk.device, readback, nullptr);
  vkFreeMemory(vk.device, readback_mem, nullptr);
  return mismatches;
}

// Incremental sub-rectangle updates into a tiled R8 atlas: fill a base pattern,
// then overwrite several sub-rects at arbitrary (tile-unaligned) offsets via
// vkCmdCopyBufferToImage with non-zero imageOffset / small imageExtent — exactly
// how Chromium places individual glyphs into its atlas. The tiled-address math
// for a sub-rect differs from a full-image copy; a bug there displaces glyph
// blocks (the reported symptom). Read the whole atlas back and verify every
// pixel matches base-or-overlay. Returns mismatch count (0 == clean).
int subregion_roundtrip(const Vk& vk, uint32_t W, uint32_t H, int* fx, int* fy, int* fexp,
                        int* fgot) {
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(W) * H;
  // Expected image: base pattern, then overlay sub-rects.
  std::vector<uint8_t> expected(bytes);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
      expected[y * W + x] = static_cast<uint8_t>((x * 131u + y * 17u) & 0xFFu);

  struct Rect {
    uint32_t ox, oy, w, h;
  };
  // Tile-unaligned offsets and odd sizes to stress sub-rect tiled addressing.
  const Rect rects[] = {{0, 0, 16, 16},   {37, 91, 24, 11},  {130, 7, 40, 33},
                        {201, 150, 50, 60}, {7, 200, 13, 19},  {W / 2, H / 2, 31, 7}};
  const int n_rects = static_cast<int>(sizeof(rects) / sizeof(rects[0]));

  const VkMemoryPropertyFlags host =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  // Image.
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory image_mem = VK_NULL_HANDLE;
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8_UNORM;
  ici.extent = {W, H, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(vk.device, &ici, nullptr, &image) != VK_SUCCESS) return -1;
  VkMemoryRequirements ireq{};
  vkGetImageMemoryRequirements(vk.device, image, &ireq);
  int imt = find_mem(vk, ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (imt < 0) imt = find_mem(vk, ireq.memoryTypeBits, 0);
  VkMemoryAllocateInfo imai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  imai.allocationSize = ireq.size;
  imai.memoryTypeIndex = static_cast<uint32_t>(imt);
  if (vkAllocateMemory(vk.device, &imai, nullptr, &image_mem) != VK_SUCCESS) return -1;
  vkBindImageMemory(vk.device, image, image_mem, 0);

  // One staging buffer big enough for the base (full image) and each sub-rect;
  // pack base at offset 0, then each rect's local pattern after it.
  std::vector<VkDeviceSize> rect_off(n_rects);
  VkDeviceSize total = bytes;
  for (int r = 0; r < n_rects; r++) {
    rect_off[r] = total;
    total += static_cast<VkDeviceSize>(rects[r].w) * rects[r].h;
  }
  VkBuffer staging = VK_NULL_HANDLE, readback = VK_NULL_HANDLE;
  VkDeviceMemory staging_mem = VK_NULL_HANDLE, readback_mem = VK_NULL_HANDLE;
  if (!make_buffer(vk, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, &staging, &staging_mem))
    return -1;
  if (!make_buffer(vk, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, &readback, &readback_mem))
    return -1;

  void* sp = nullptr;
  vkMapMemory(vk.device, staging_mem, 0, total, 0, &sp);
  uint8_t* s = static_cast<uint8_t*>(sp);
  memcpy(s, expected.data(), bytes);  // base
  for (int r = 0; r < n_rects; r++) {
    const Rect& rc = rects[r];
    uint8_t* dst = s + rect_off[r];
    for (uint32_t ly = 0; ly < rc.h; ly++) {
      for (uint32_t lx = 0; lx < rc.w; lx++) {
        uint8_t v = static_cast<uint8_t>((lx * 53u + ly * 29u + (r + 1) * 97u) & 0xFFu);
        dst[ly * rc.w + lx] = v;
        expected[(rc.oy + ly) * W + (rc.ox + lx)] = v;  // overlay onto expected
      }
    }
  }
  vkUnmapMemory(vk.device, staging_mem);

  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = vk.queue_family;
  vkCreateCommandPool(vk.device, &pci, nullptr, &pool);
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  vkAllocateCommandBuffers(vk.device, &cbai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);

  auto barrier = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  };

  barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT);
  // Base full-image copy.
  VkBufferImageCopy base{};
  base.bufferOffset = 0;
  base.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  base.imageExtent = {W, H, 1};
  vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &base);
  // Ensure base completes before the overlapping sub-rect writes.
  barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  // Sub-rect updates at arbitrary offsets.
  for (int r = 0; r < n_rects; r++) {
    const Rect& rc = rects[r];
    VkBufferImageCopy sub{};
    sub.bufferOffset = rect_off[r];
    sub.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    sub.imageOffset = {static_cast<int32_t>(rc.ox), static_cast<int32_t>(rc.oy), 0};
    sub.imageExtent = {rc.w, rc.h, 1};
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &sub);
  }
  barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferImageCopy rd{};
  rd.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  rd.imageExtent = {W, H, 1};
  vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &rd);
  vkEndCommandBuffer(cmd);

  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkCreateFence(vk.device, &fci, nullptr, &fence);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(vk.queue, 1, &si, fence);
  vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);

  void* rb = nullptr;
  vkMapMemory(vk.device, readback_mem, 0, bytes, 0, &rb);
  const uint8_t* got = static_cast<const uint8_t*>(rb);
  int mismatches = 0;
  for (uint32_t i = 0; i < bytes; i++) {
    if (got[i] != expected[i]) {
      if (mismatches == 0) {
        *fx = static_cast<int>(i % W);
        *fy = static_cast<int>(i / W);
        *fexp = expected[i];
        *fgot = got[i];
      }
      mismatches++;
    }
  }
  vkUnmapMemory(vk.device, readback_mem);

  vkDestroyFence(vk.device, fence, nullptr);
  vkDestroyCommandPool(vk.device, pool, nullptr);
  vkDestroyImage(vk.device, image, nullptr);
  vkFreeMemory(vk.device, image_mem, nullptr);
  vkDestroyBuffer(vk.device, staging, nullptr);
  vkFreeMemory(vk.device, staging_mem, nullptr);
  vkDestroyBuffer(vk.device, readback, nullptr);
  vkFreeMemory(vk.device, readback_mem, nullptr);
  return mismatches;
}

}  // namespace

// GLES (OpenGL ES 3.0) R8 probe — the API path Chromium's renderer actually uses
// (Skia Ganesh GL -> GLES proxy -> host ANGLE -> Vulkan). Defined in gles-probe.cpp.
bool RunGlesProbe(std::string* out);

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellovktexture_MainActivity_probeVkTexture(JNIEnv* env, jobject /*this*/) {
  std::string report = "Vulkan R8 OPTIMAL-tiled round-trip probe:\n";
  char buf[256];

  Vk vk;
  std::string err;
  if (!init_vk(&vk, &err)) {
    report += "  Vulkan init: " + err + "\n";
    LOGE("%s", report.c_str());
    return env->NewStringUTF(report.c_str());
  }

  // Sizes chosen to stress tile/row-pitch handling: small, glyph-atlas-like, and
  // tile-unaligned (non-multiple-of-common-tile-width / odd) dimensions.
  struct Size {
    uint32_t w, h;
  };
  const Size sizes[] = {{64, 64}, {256, 256}, {300, 200}, {137, 59}, {1024, 64}};
  int total_fail = 0, total = 0;
  for (int s = 0; s < static_cast<int>(sizeof(sizes) / sizeof(sizes[0])); s++) {
    int fx = -1, fy = -1, fexp = -1, fgot = -1;
    int mm = roundtrip(vk, sizes[s].w, sizes[s].h, s, &fx, &fy, &fexp, &fgot);
    total++;
    if (mm < 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : SETUP-FAIL\n", sizes[s].w, sizes[s].h);
    } else if (mm == 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : OK\n", sizes[s].w, sizes[s].h);
    } else {
      total_fail++;
      snprintf(buf, sizeof(buf),
               "  %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n", sizes[s].w,
               sizes[s].h, mm, fx, fy, fexp, fgot);
    }
    report += buf;
  }
  snprintf(buf, sizeof(buf), "Transfer: %d/%d sizes clean%s\n", total - total_fail, total,
           total_fail ? "  <-- TILED R8 TRANSFER CORRUPTED" : "");
  report += buf;

  // Sampled-render path (image view / sampler / descriptors / graphics pipeline)
  // — the path Chromium draws its glyph atlas through.
  report += "Vulkan R8 sampled-render probe:\n";
  int s_fail = 0, s_total = 0;
  for (int s = 0; s < static_cast<int>(sizeof(sizes) / sizeof(sizes[0])); s++) {
    int fx = -1, fy = -1, fexp = -1, fgot = -1;
    int mm = sampled_roundtrip(vk, sizes[s].w, sizes[s].h, s, &fx, &fy, &fexp, &fgot);
    s_total++;
    if (mm < 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : SETUP-FAIL\n", sizes[s].w, sizes[s].h);
    } else if (mm == 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : OK\n", sizes[s].w, sizes[s].h);
    } else {
      s_fail++;
      snprintf(buf, sizeof(buf),
               "  %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n", sizes[s].w,
               sizes[s].h, mm, fx, fy, fexp, fgot);
    }
    report += buf;
  }
  snprintf(buf, sizeof(buf), "Sampled: %d/%d sizes clean%s\n", s_total - s_fail, s_total,
           s_fail ? "  <-- SAMPLED R8 RENDER CORRUPTED" : "");
  report += buf;

  // Render-INTO-R8 path (R8 as a color attachment) — Chromium rasterizes glyph
  // coverage into its R8 atlas this way.
  report += "Vulkan rasterize-into-R8 probe:\n";
  int r_fail = 0, r_total = 0;
  for (int s = 0; s < static_cast<int>(sizeof(sizes) / sizeof(sizes[0])); s++) {
    int fx = -1, fy = -1, fexp = -1, fgot = -1;
    int mm = render_to_r8(vk, sizes[s].w, sizes[s].h, &fx, &fy, &fexp, &fgot);
    r_total++;
    if (mm == -2) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : R8-attachment-UNSUPPORTED\n", sizes[s].w,
               sizes[s].h);
    } else if (mm < 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : SETUP-FAIL\n", sizes[s].w, sizes[s].h);
    } else if (mm == 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : OK\n", sizes[s].w, sizes[s].h);
    } else {
      r_fail++;
      snprintf(buf, sizeof(buf),
               "  %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n", sizes[s].w,
               sizes[s].h, mm, fx, fy, fexp, fgot);
    }
    report += buf;
  }
  snprintf(buf, sizeof(buf), "RenderR8: %d/%d sizes clean%s\n", r_total - r_fail, r_total,
           r_fail ? "  <-- RASTERIZE-INTO-R8 CORRUPTED" : "");
  report += buf;

  // Incremental sub-rectangle atlas updates (Chromium's per-glyph placement).
  report += "Vulkan R8 sub-rect atlas-update probe:\n";
  const Size atlas[] = {{256, 256}, {512, 512}};
  int g_fail = 0, g_total = 0;
  for (int s = 0; s < static_cast<int>(sizeof(atlas) / sizeof(atlas[0])); s++) {
    int fx = -1, fy = -1, fexp = -1, fgot = -1;
    int mm = subregion_roundtrip(vk, atlas[s].w, atlas[s].h, &fx, &fy, &fexp, &fgot);
    g_total++;
    if (mm < 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : SETUP-FAIL\n", atlas[s].w, atlas[s].h);
    } else if (mm == 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : OK\n", atlas[s].w, atlas[s].h);
    } else {
      g_fail++;
      snprintf(buf, sizeof(buf),
               "  %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n", atlas[s].w,
               atlas[s].h, mm, fx, fy, fexp, fgot);
    }
    report += buf;
  }
  snprintf(buf, sizeof(buf), "SubRect: %d/%d clean%s\n", g_total - g_fail, g_total,
           g_fail ? "  <-- SUB-RECT ATLAS UPDATE CORRUPTED" : "");
  report += buf;

  vkDeviceWaitIdle(vk.device);
  vkDestroyDevice(vk.device, nullptr);
  vkDestroyInstance(vk.instance, nullptr);

  // GLES path — what Chromium's renderer actually uses (Skia GL -> ANGLE).
  report += "GLES (ANGLE) R8 probe [Chromium's actual path]:\n";
  bool gles_ok = RunGlesProbe(&report);

  // Log per line — a single multi-line __android_log_print is truncated at the
  // ~4 KB logcat message limit, which would hide the later GLES draw-side lines.
  {
    bool any_fail = (total_fail || s_fail || r_fail || g_fail || !gles_ok);
    size_t start = 0;
    while (start < report.size()) {
      size_t nl = report.find('\n', start);
      std::string line = report.substr(start, nl == std::string::npos ? std::string::npos
                                                                       : nl - start);
      if (any_fail) {
        LOGE("%s", line.c_str());
      } else {
        LOGI("%s", line.c_str());
      }
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
  }
  return env->NewStringUTF(report.c_str());
}
