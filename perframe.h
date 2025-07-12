#ifndef __PERFRAME_H__
#define __PERFRAME_H__

#include "vulkan/vulkan.h"
#include "math/math.h"

typedef struct
{
	VkFramebuffer frameBuffer;

	// Descriptor pool
	VkDescriptorPool descriptorPool;

	// Command buffer
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;

	// Fences/semaphores
	VkFence frameFence;
	VkSemaphore completeSemaphore;
} PerFrame_t;

#define FRAMES_IN_FLIGHT 3
extern PerFrame_t perFrame[FRAMES_IN_FLIGHT];

#endif
