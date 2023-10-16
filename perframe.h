#ifndef __PERFRAME_H__
#define __PERFRAME_H__

#include "vulkan/vulkan.h"
#include "math/math.h"

typedef struct
{
	VkFramebuffer FrameBuffer;

	// Descriptor pool
	VkDescriptorPool DescriptorPool;

	// Command buffer
	VkCommandPool CommandPool;
	VkCommandBuffer CommandBuffer;

	// Fences/semaphores
	VkFence FrameFence;
	VkSemaphore PresentCompleteSemaphore;
	VkSemaphore RenderCompleteSemaphore;
} PerFrame_t;

extern PerFrame_t PerFrame[VKU_MAX_FRAME_COUNT];

#endif
