#pragma once
#include <stdint.h>
// The Frame host reads a D3D11 relay after Vulkan has finished writing it.
bool VkFrameBridgeStart(void* device);
void VkFrameBridgeStop();
void* VkFrameBridgeCopy(uint64_t image, uint32_t layer, int slot,
                       uint32_t width, uint32_t height);

// Startup setting and frame-boundary A/B control.
void VkFrameBridgeSetBatch(bool on);
void VkFrameBridgeToggleBatch();
