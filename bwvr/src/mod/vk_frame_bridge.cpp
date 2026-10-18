#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include "vk_core.h"
#include "vk_frame_bridge.h"
#include "../vr/xr_screen.h"
#include <cstring>

void ModLog(const char*, ...);
namespace {
ID3D11Device* device = nullptr;
struct Relay {
  ID3D11Texture2D* texture = nullptr;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint32_t width = 0, height = 0;
  bool initialized = false;
};
Relay relays[3];
bool failed = false;
bool batch = false;
void Free(Relay& r) {
  if (r.image) vk::f.vkDestroyImage(vk::Get().device, r.image, nullptr);
  if (r.memory) vk::f.vkFreeMemory(vk::Get().device, r.memory, nullptr);
  if (r.texture) r.texture->Release();
  r = {};
}
bool Make(Relay& r, uint32_t width, uint32_t height) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, &r.texture);
  if (FAILED(hr)) { ModLog("vk frame     : CreateTexture2D failed, 0x%08lX", hr); return false; }
  IDXGIResource* resource = nullptr;
  hr = r.texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&resource));
  if (FAILED(hr)) return false;
  HANDLE handle = nullptr;
  hr = resource->GetSharedHandle(&handle); resource->Release();
  if (FAILED(hr) || !handle) return false;
  // The legacy handle stays valid while its D3D11 texture lives. Do not close it.
  const auto type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
  auto properties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
      vk::f.vkGetDeviceProcAddr(vk::Get().device, "vkGetMemoryWin32HandlePropertiesKHR"));
  if (!properties) return false;
  auto format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
      vk::f.vkGetInstanceProcAddr(vk::Get().instance, "vkGetPhysicalDeviceImageFormatProperties2"));
  if (!format_properties) return false;
  VkPhysicalDeviceExternalImageFormatInfo external_info = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
  external_info.handleType = type;
  VkPhysicalDeviceImageFormatInfo2 format_info = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
  format_info.pNext = &external_info; format_info.format = VK_FORMAT_B8G8R8A8_UNORM;
  format_info.type = VK_IMAGE_TYPE_2D; format_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  format_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkExternalImageFormatProperties external_properties = {VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 image_properties = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  image_properties.pNext = &external_properties;
  const VkResult supported = format_properties(vk::Get().physical, &format_info, &image_properties);
  if (supported != VK_SUCCESS ||
      !(external_properties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
    ModLog("vk frame     : BGRA KMT image import is not supported, %d", supported); return false;
  }
  VkExternalMemoryImageCreateInfo external = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  external.handleTypes = type;
  VkImageCreateInfo image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image.pNext = &external; image.imageType = VK_IMAGE_TYPE_2D;
  image.format = VK_FORMAT_B8G8R8A8_UNORM; image.extent = {width, height, 1};
  image.mipLevels = image.arrayLayers = 1; image.samples = VK_SAMPLE_COUNT_1_BIT;
  image.tiling = VK_IMAGE_TILING_OPTIMAL;
  image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult result = vk::f.vkCreateImage(vk::Get().device, &image, nullptr, &r.image);
  if (result != VK_SUCCESS) { ModLog("vk frame     : vkCreateImage failed, %d", result); return false; }
  VkMemoryRequirements requirements;
  vk::f.vkGetImageMemoryRequirements(vk::Get().device, r.image, &requirements);
  VkMemoryWin32HandlePropertiesKHR hp = {VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
  result = properties(vk::Get().device, type, handle, &hp);
  if (result != VK_SUCCESS) { ModLog("vk frame     : KMT memory properties failed, %d", result); return false; }
  const uint32_t bits = requirements.memoryTypeBits & hp.memoryTypeBits;
  uint32_t index = 0;
  while (index < vk::Get().memory.memoryTypeCount && !(bits & (1u << index))) ++index;
  if (index == vk::Get().memory.memoryTypeCount) return false;
  VkMemoryDedicatedAllocateInfo dedicated = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicated.image = r.image;
  VkImportMemoryWin32HandleInfoKHR import = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
  import.pNext = &dedicated; import.handleType = type; import.handle = handle;
  VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.pNext = &import; allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = index;
  result = vk::f.vkAllocateMemory(vk::Get().device, &allocate, nullptr, &r.memory);
  if (result == VK_SUCCESS) result = vk::f.vkBindImageMemory(vk::Get().device, r.image, r.memory, 0);
  if (result != VK_SUCCESS) { ModLog("vk frame     : import or bind failed, %d", result); return false; }
  r.width = width; r.height = height;
  ModLog("vk frame     : shared GPU relay ready, %ux%u BGRA", width, height);
  return true;
}
void Ownership(VkCommandBuffer cb, Relay& r, bool acquire) {
  VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = acquire ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
  b.srcAccessMask = acquire ? 0 : VK_ACCESS_2_TRANSFER_WRITE_BIT;
  b.dstStageMask = acquire ? VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT : VK_PIPELINE_STAGE_2_NONE;
  b.dstAccessMask = acquire ? VK_ACCESS_2_TRANSFER_WRITE_BIT : 0;
  b.oldLayout = acquire ? (r.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED) : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.newLayout = acquire ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
  b.srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_EXTERNAL : vk::Get().queue_family;
  b.dstQueueFamilyIndex = acquire ? vk::Get().queue_family : VK_QUEUE_FAMILY_EXTERNAL;
  b.image = r.image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &b;
  vk::f.vkCmdPipelineBarrier2(cb, &dependency);
}
}
bool VkFrameBridgeStart(void* source) {
  if (!source || !vk::Start()) return false;
  auto* d3d = static_cast<ID3D11Device*>(source);
  IDXGIDevice* dxgi = nullptr; IDXGIAdapter* adapter = nullptr; DXGI_ADAPTER_DESC desc = {};
  HRESULT hr = d3d->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi));
  if (SUCCEEDED(hr)) { hr = dxgi->GetAdapter(&adapter); dxgi->Release(); }
  if (SUCCEEDED(hr)) { hr = adapter->GetDesc(&desc); adapter->Release(); }
  VkPhysicalDeviceIDProperties id = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  props.pNext = &id;
  vk::f.vkGetPhysicalDeviceProperties2(vk::Get().physical, &props);
  if (FAILED(hr) || !id.deviceLUIDValid || std::memcmp(id.deviceLUID, &desc.AdapterLuid, sizeof(LUID))) {
    ModLog("vk frame     : Vulkan and the XR host must use the same adapter LUID"); return false;
  }
  VkFormatProperties src, dst;
  vk::f.vkGetPhysicalDeviceFormatProperties(vk::Get().physical, vk::Get().color_format, &src);
  vk::f.vkGetPhysicalDeviceFormatProperties(vk::Get().physical, VK_FORMAT_B8G8R8A8_UNORM, &dst);
  if (!(src.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
      !(dst.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) return false;
  device = d3d; device->AddRef(); failed = false;
  ModLog("vk frame     : Vulkan renderer to x64 D3D11 host. No CPU image transfer");
  return true;
}
namespace {
void RecordCopy(VkCommandBuffer cb, uint64_t image, uint32_t layer,
                Relay& r, uint32_t width, uint32_t height) {
#if VK_USE_64_BIT_PTR_DEFINES
  VkImage source = reinterpret_cast<VkImage>(image);
#else
  VkImage source = image;
#endif
  vk::Barrier(cb, source, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1, layer);
  Ownership(cb, r, true);
  VkImageBlit blit = {};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = blit.dstOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
  vk::f.vkCmdBlitImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      r.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
  Ownership(cb, r, false);
  vk::Barrier(cb, source, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1, layer);
}
}
void* VkFrameBridgeCopy(uint64_t image, uint32_t layer, int slot, uint32_t width, uint32_t height) {
  if (!device || failed || !image || slot < 0 || slot > 2) return nullptr;
  auto& r = relays[slot];
  if (r.width != width || r.height != height) {
    Free(r);
    if (!Make(r, width, height)) { Free(r); failed = true; ModLog("vk frame     : relay creation failed. Restart with xr_bridge_vulkan=0 and backend=dxwrapper"); return nullptr; }
  }
  auto cb = vk::BeginOnce();
  if (!cb) { failed = true; return nullptr; }
  RecordCopy(cb, image, layer, r, width, height);
  if (!vk::EndOnce(cb)) { failed = true; ModLog("vk frame     : relay GPU submission failed"); return nullptr; }
  r.initialized = true;
  return r.texture;
}
bool VkFrameBridgePair(const uint64_t images[2], const uint32_t layers[2],
                       uint32_t width, uint32_t height, void* textures[2]) {
  textures[0] = textures[1] = nullptr;
  if (!device || failed || !images[0] || !images[1]) return false;
  // Allocate both relays before recording. No recorded work references a
  // resource that a later allocation failure can free.
  for (int eye = 0; eye < 2; ++eye) {
    auto* r = &relays[eye];
    if (r->width != width || r->height != height) {
      Free(*r);
      if (!Make(*r, width, height)) {
        Free(*r); failed = true;
        ModLog("vk frame     : stereo relay creation failed"); return false;
      }
    }
  }
  auto cb = vk::BeginOnce();
  if (!cb) { failed = true; return false; }
  for (int eye = 0; eye < 2; ++eye)
    RecordCopy(cb, images[eye], layers[eye], relays[eye], width, height);
  if (!vk::EndOnce(cb)) {
    failed = true; ModLog("vk frame     : stereo relay submission failed"); return false;
  }
  for (int eye = 0; eye < 2; ++eye) {
    relays[eye].initialized = true; textures[eye] = relays[eye].texture;
  }
  return true;
}
void VkFrameBridgeSetBatch(bool on) {
  batch = on;
  bwvr::XrScreen::ConfigureVulkanRelayPair(on ? &VkFrameBridgePair : nullptr);
  ModLog("vk frame     : xr_bridge_vk_batch=%d. %s", on ? 1 : 0,
         on ? "Both eyes use one Vulkan submission and fence wait" : "Each eye uses its own Vulkan submission and fence wait");
}
void VkFrameBridgeToggleBatch() {
  if (device && !failed) VkFrameBridgeSetBatch(!batch);
}
void VkFrameBridgeStop() {
  bwvr::XrScreen::ConfigureVulkanRelayPair(nullptr);
  for (auto& r : relays) Free(r);
  if (device) device->Release();
  device = nullptr;
}
