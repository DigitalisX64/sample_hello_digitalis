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

}  // namespace

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
  snprintf(buf, sizeof(buf), "Summary: %d/%d sizes clean%s\n", total - total_fail, total,
           total_fail ? "  <-- TILED R8 ROUND-TRIP CORRUPTED" : "");
  report += buf;
  if (total_fail) {
    LOGE("%s", report.c_str());
  } else {
    LOGI("%s", report.c_str());
  }

  vkDeviceWaitIdle(vk.device);
  vkDestroyDevice(vk.device, nullptr);
  vkDestroyInstance(vk.instance, nullptr);
  return env->NewStringUTF(report.c_str());
}
