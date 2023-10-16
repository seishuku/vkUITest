#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <string.h>
#include "system/system.h"
#include "network/network.h"
#include "vulkan/vulkan.h"
#include "math/math.h"
#include "camera/camera.h"
#include "model/bmodel.h"
#include "image/image.h"
#include "utils/list.h"
#include "lights/lights.h"
#include "utils/event.h"
#include "utils/input.h"
#include "particle/particle.h"
#include "threads/threads.h"
#include "vr/vr.h"
#include "font/font.h"
#include "audio/audio.h"
#include "physics/physics.h"
#include "ui/ui.h"
#include "perframe.h"

// Done flag from system code
extern bool Done;

// Initial window size
uint32_t Width=1280, Height=720;

// Vulkan instance handle and context structs
VkInstance Instance;
VkuContext_t Context;

// Vulkan memory allocator zone
VkuMemZone_t *VkZone;

// extern timing data from system main
extern float fps, fTimeStep, fTime;

// Vulkan swapchain helper struct
VkuSwapchain_t Swapchain;

// Depth buffer image and format
VkFormat DepthFormat=VK_FORMAT_D32_SFLOAT_S8_UINT;
VkuImage_t DepthImage;

VkRenderPass RenderPass;

UI_t UI;

uint32_t CursorID=UINT32_MAX;

uint32_t SliderID=UINT32_MAX;

uint32_t Color1ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };
uint32_t Color2ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };
uint32_t Color3ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };
uint32_t Color4ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };

uint32_t ColorMapID=UINT32_MAX;

uint32_t SpriteID=UINT32_MAX;

VkuBuffer_t FireStagingBuffer;
VkuImage_t FireImage;

#define FIRE_WIDTH 512
#define FIRE_HEIGHT 256

unsigned char buffer1[FIRE_WIDTH*FIRE_HEIGHT], buffer2[FIRE_WIDTH*FIRE_HEIGHT];

vec3 Bezier(float t, vec3 p0, vec3 p1, vec3 p2, vec3 p3)
{
	vec3 c=Vec3_Muls(Vec3_Subv(p1, p0), 3.0);
	vec3 b=Vec3_Subv(Vec3_Muls(Vec3_Subv(p2, p1), 3.0), c);
	vec3 a=Vec3_Subv(Vec3_Subv(Vec3_Subv(p3, p0), c), b);

	return Vec3_Addv(Vec3_Addv(Vec3_Addv(Vec3_Muls(a, t*t*t), Vec3_Muls(b, t*t)), Vec3_Muls(c, t)), p0);
}

void FireRunStep(uint32_t Index)
{
	for(uint32_t i=0;i<FIRE_WIDTH*4;i++)
		buffer1[rand()%(FIRE_WIDTH*4)]=RandRange(0, 255);

	for(uint32_t y=2;y<FIRE_HEIGHT-1;y++)
	{
		for(uint32_t x=1;x<FIRE_WIDTH-1;x++)
		{
			buffer2[y*FIRE_WIDTH+x]=(
				buffer1[(y-1)*FIRE_WIDTH+(x-1)]+
				buffer1[(y+1)*FIRE_WIDTH+(x+0)]+
				buffer1[(y-2)*FIRE_WIDTH+(x+1)]+
				buffer1[(y-2)*FIRE_WIDTH+(x+0)]
				)/4;
		}
	}

	vec3 Color1=Vec3(UI_GetBarGraphValue(&UI, Color1ID[0]), UI_GetBarGraphValue(&UI, Color1ID[1]), UI_GetBarGraphValue(&UI, Color1ID[2]));
	vec3 Color2=Vec3(UI_GetBarGraphValue(&UI, Color2ID[0]), UI_GetBarGraphValue(&UI, Color2ID[1]), UI_GetBarGraphValue(&UI, Color2ID[2]));
	vec3 Color3=Vec3(UI_GetBarGraphValue(&UI, Color3ID[0]), UI_GetBarGraphValue(&UI, Color3ID[1]), UI_GetBarGraphValue(&UI, Color3ID[2]));
	vec3 Color4=Vec3(UI_GetBarGraphValue(&UI, Color4ID[0]), UI_GetBarGraphValue(&UI, Color4ID[1]), UI_GetBarGraphValue(&UI, Color4ID[2]));

	void *Data=NULL;
	vkMapMemory(Context.Device, FireStagingBuffer.DeviceMemory, 0, VK_WHOLE_SIZE, 0, &Data);

	uint8_t *fb=(uint8_t *)Data;

	for(uint32_t y=0;y<FIRE_HEIGHT;y++)
	{
		int flipy=FIRE_HEIGHT-1-y;

		for(uint32_t x=0;x<FIRE_WIDTH;x++)
		{
			float Fire=min(max(((float)buffer2[flipy*FIRE_WIDTH+x]/255.0f)-UI_GetBarGraphValue(&UI, SliderID), 0.0f), 1.0f);
			vec3 Color;

			if(UI_GetCheckBoxValue(&UI, ColorMapID))
				Color=Bezier(min(max(Fire, 0.0f), 1.0f), Color1, Color2, Color3, Color4);
			else
				Color=Vec3b(Fire);

			fb[4*(y*FIRE_WIDTH+x)+0]=(uint8_t)(Color.z*255.0f);
			fb[4*(y*FIRE_WIDTH+x)+1]=(uint8_t)(Color.y*255.0f);
			fb[4*(y*FIRE_WIDTH+x)+2]=(uint8_t)(Color.x*255.0f);
			fb[4*(y*FIRE_WIDTH+x)+3]=0xFF;
		}
	}

	vkUnmapMemory(Context.Device, FireStagingBuffer.DeviceMemory);

	memcpy(buffer1, buffer2, FIRE_WIDTH*FIRE_HEIGHT);

	vkuTransitionLayout(PerFrame[Index].CommandBuffer, FireImage.Image, 1, 0, 1, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkCmdCopyBufferToImage(PerFrame[Index].CommandBuffer, FireStagingBuffer.Buffer, FireImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, (VkBufferImageCopy[1])
	{
		{
			0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { FIRE_WIDTH, FIRE_HEIGHT, 1 }
		}
	});
	vkuTransitionLayout(PerFrame[Index].CommandBuffer, FireImage.Image, 1, 0, 1, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RecreateSwapchain(void);

// Create functions for creating render data for asteroids
bool CreateFramebuffers(uint32_t targetWidth, uint32_t targetHeight)
{
	vkuCreateTexture2D(&Context, &DepthImage, targetWidth, targetHeight, DepthFormat, VK_SAMPLE_COUNT_1_BIT);

	VkCommandBuffer CommandBuffer=vkuOneShotCommandBufferBegin(&Context);
	vkuTransitionLayout(CommandBuffer, DepthImage.Image, 1, 0, 1, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	vkuOneShotCommandBufferEnd(&Context, CommandBuffer);

	vkCreateRenderPass(Context.Device, &(VkRenderPassCreateInfo)
	{
		.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount=2,
		.pAttachments=(VkAttachmentDescription[])
		{
			{
				.format=Swapchain.SurfaceFormat.format,
				.samples=VK_SAMPLE_COUNT_1_BIT,
				.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			{
				.format=DepthFormat,
				.samples=VK_SAMPLE_COUNT_1_BIT,
				.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
		},
		.subpassCount=1,
		.pSubpasses=&(VkSubpassDescription)
		{
			.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount=1,
			.pColorAttachments=&(VkAttachmentReference)
			{
				.attachment=0,
				.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			},
			.pDepthStencilAttachment=&(VkAttachmentReference)
			{
				.attachment=1,
				.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
		},
	}, 0, &RenderPass);

	for(uint32_t i=0;i<Swapchain.NumImages;i++)
	{
		if(vkCreateFramebuffer(Context.Device, &(VkFramebufferCreateInfo)
		{
			.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass=RenderPass,
			.attachmentCount=2,
			.pAttachments=(VkImageView[]){ Swapchain.ImageView[i], DepthImage.View },
			.width=Swapchain.Extent.width,
			.height=Swapchain.Extent.height,
			.layers=1,
		}, 0, &PerFrame[i].FrameBuffer)!=VK_SUCCESS)
			return false;
	}

	return true;
}

// Render call from system main event loop
void Render(void)
{
	static double fireTime=0.0;
	const double OneOver60=1.0/60.0;
	static uint32_t Index=0;
	uint32_t imageIndex;

	VkResult Result=vkAcquireNextImageKHR(Context.Device, Swapchain.Swapchain, UINT64_MAX, PerFrame[Index].PresentCompleteSemaphore, VK_NULL_HANDLE, &imageIndex);

	if(Result==VK_ERROR_OUT_OF_DATE_KHR||Result==VK_SUBOPTIMAL_KHR)
	{
		DBGPRINTF("Swapchain out of date... Rebuilding.\n");
		RecreateSwapchain();
		return;
	}

	vkWaitForFences(Context.Device, 1, &PerFrame[Index].FrameFence, VK_TRUE, UINT64_MAX);

	// Reset the frame fence and command pool (and thus the command buffer)
	vkResetFences(Context.Device, 1, &PerFrame[Index].FrameFence);
	vkResetDescriptorPool(Context.Device, PerFrame[Index].DescriptorPool, 0);
	vkResetCommandPool(Context.Device, PerFrame[Index].CommandPool, 0);

	// Start recording the commands
	vkBeginCommandBuffer(PerFrame[Index].CommandBuffer, &(VkCommandBufferBeginInfo)
	{
		.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	});

	// Run the fire at 60FPS
	fireTime+=fTimeStep;
	if(fireTime>=OneOver60)
	{
		fireTime=0.0;
		FireRunStep(Index);
	}

	// Start a render pass and clear the frame/depth buffer
	vkCmdBeginRenderPass(PerFrame[Index].CommandBuffer, &(VkRenderPassBeginInfo)
	{
		.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass=RenderPass,
		.framebuffer=PerFrame[Index].FrameBuffer,
		.clearValueCount=2,
		.pClearValues=(VkClearValue[]){ { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0 } },
		.renderArea.offset={ 0, 0 },
		.renderArea.extent=Swapchain.Extent,
	}, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetViewport(PerFrame[Index].CommandBuffer, 0, 1, &(VkViewport) { 0.0f, 0, (float)Swapchain.Extent.width, (float)Swapchain.Extent.height, 0.0f, 1.0f });
	vkCmdSetScissor(PerFrame[Index].CommandBuffer, 0, 1, &(VkRect2D) { { 0, 0 }, Swapchain.Extent});

	UI_Draw(&UI, Index);

	Font_Print(16.0f, 0.0f, 0.0f, "FPS: %0.1f\n\x1B[91mFrame time: %0.5fms", fps, fTimeStep*1000.0f);
	Font_Draw(Index);

	vkCmdEndRenderPass(PerFrame[Index].CommandBuffer);

	vkEndCommandBuffer(PerFrame[Index].CommandBuffer);

	// Submit command queue
	vkQueueSubmit(Context.Queue, 1, &(VkSubmitInfo)
	{
		.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pWaitDstStageMask=&(VkPipelineStageFlags) { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT },
		.waitSemaphoreCount=1,
		.pWaitSemaphores=&PerFrame[Index].PresentCompleteSemaphore,
		.signalSemaphoreCount=1,
		.pSignalSemaphores=&PerFrame[Index].RenderCompleteSemaphore,
		.commandBufferCount=1,
		.pCommandBuffers=&PerFrame[Index].CommandBuffer,
	}, PerFrame[Index].FrameFence);

	// And present it to the screen
	Result=vkQueuePresentKHR(Context.Queue, &(VkPresentInfoKHR)
	{
		.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount=1,
		.pWaitSemaphores=&PerFrame[Index].RenderCompleteSemaphore,
		.swapchainCount=1,
		.pSwapchains=&Swapchain.Swapchain,
		.pImageIndices=&imageIndex,
	});

	if(Result==VK_ERROR_OUT_OF_DATE_KHR||Result==VK_SUBOPTIMAL_KHR)
	{
		DBGPRINTF("Swapchain out of date... Rebuilding.\n");
		RecreateSwapchain();
		return;
	}

	Index=(Index+1)%Swapchain.NumImages;
}

void ExitButton(void *arg)
{
	Done=true;
}

// Initialization call from system main
bool Init(void)
{
	Event_Add(EVENT_KEYDOWN, Event_KeyDown);
	Event_Add(EVENT_KEYUP, Event_KeyUp);
	Event_Add(EVENT_MOUSEDOWN, Event_MouseDown);
	Event_Add(EVENT_MOUSEUP, Event_MouseUp);
	Event_Add(EVENT_MOUSEMOVE, Event_Mouse);

	VkZone=vkuMem_Init(&Context, (size_t)(Context.DeviceProperties2.maxMemoryAllocationSize*0.8f));

	if(VkZone==NULL)
		return false;

	// Create primary frame buffers, depth image
	CreateFramebuffers(Width, Height);

	vkuCreateHostBuffer(&Context, &FireStagingBuffer, FIRE_WIDTH*FIRE_HEIGHT*4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	vkuCreateTexture2D(&Context, &FireImage, FIRE_WIDTH, FIRE_HEIGHT, VK_FORMAT_B8G8R8A8_SRGB, VK_SAMPLE_COUNT_1_BIT);

	VkCommandBuffer CommandBuffer=vkuOneShotCommandBufferBegin(&Context);
	vkuTransitionLayout(CommandBuffer, FireImage.Image, 1, 0, 1, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vkuOneShotCommandBufferEnd(&Context, CommandBuffer);

	// Other per-frame data
	for(uint32_t i=0;i<Swapchain.NumImages;i++)
	{
		// Create needed fence and semaphores for rendering
		// Wait fence for command queue, to signal when we can submit commands again
		vkCreateFence(Context.Device, &(VkFenceCreateInfo) {.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags=VK_FENCE_CREATE_SIGNALED_BIT }, VK_NULL_HANDLE, &PerFrame[i].FrameFence);

		// Semaphore for image presentation, to signal when we can present again
		vkCreateSemaphore(Context.Device, &(VkSemaphoreCreateInfo) {.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext=VK_NULL_HANDLE }, VK_NULL_HANDLE, &PerFrame[i].PresentCompleteSemaphore);

		// Semaphore for render complete, to signal when we can render again
		vkCreateSemaphore(Context.Device, &(VkSemaphoreCreateInfo) {.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext=VK_NULL_HANDLE }, VK_NULL_HANDLE, &PerFrame[i].RenderCompleteSemaphore);

		// Per-frame descriptor pool for main thread
		vkCreateDescriptorPool(Context.Device, &(VkDescriptorPoolCreateInfo)
		{
			.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets=1024, // Max number of descriptor sets that can be allocated from this pool
			.poolSizeCount=4,
			.pPoolSizes=(VkDescriptorPoolSize[])
			{
				{
					.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount=1024, // Max number of this descriptor type that can be in each descriptor set?
				},
				{
					.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.descriptorCount=1024,
				},
				{
					.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount=1024,
				},
				{
					.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount=1024,
				},
			},
		}, VK_NULL_HANDLE, &PerFrame[i].DescriptorPool);

		// Create per-frame command pools
		vkCreateCommandPool(Context.Device, &(VkCommandPoolCreateInfo)
		{
			.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags=0,
			.queueFamilyIndex=Context.QueueFamilyIndex,
		}, VK_NULL_HANDLE, &PerFrame[i].CommandPool);

		// Allocate the command buffers we will be rendering into
		vkAllocateCommandBuffers(Context.Device, &(VkCommandBufferAllocateInfo)
		{
			.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool=PerFrame[i].CommandPool,
			.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount=1,
		}, &PerFrame[i].CommandBuffer);
	}

	UI_Init(&UI, Vec2(0.0f, 0.0f), Vec2((float)Width, (float)Height));

	SpriteID=UI_AddSprite(&UI, Vec2((float)Width/2.0f, (float)Height/2.0f), UI.Size, Vec3(1.0f, 1.0f, 1.0f), &FireImage, 0.0f);

	UI_AddButton(&UI, Vec2((float)Width-200.0f, 100.0f), Vec2(100.0f, 100.0f), Vec3(0.5f, 0.5f, 0.5f), "Exit", ExitButton);

	SliderID=UI_AddBarGraph(&UI, Vec2((float)Width-400.0f, (float)Height-50.0f), Vec2(400.0f, 50.0f), Vec3(0.5f, 0.5f, 0.5f), "Fire", false, 0.4f, 0.0f, 0.0f);

	Color1ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-32.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), "Color 1 Red", false, 0.0f, 1.0f, 0.0f);
	Color1ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-64.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), "Color 1 Green", false, 0.0f, 1.0f, 0.0f);
	Color1ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-96.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), "Color 1 Blue", false, 0.0f, 1.0f, 0.0f);

	Color2ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-160.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), "Color 2 Red", false, 0.0f, 1.0f, 1.0f);
	Color2ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-192.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), "Color 2 Green", false, 0.0f, 1.0f, 0.0f);
	Color2ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-224.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), "Color 2 Blue", false, 0.0f, 1.0f, 0.0f);

	Color3ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-288.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), "Color 3 Red", false, 0.0f, 1.0f, 1.0f);
	Color3ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-320.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), "Color 3 Green", false, 0.0f, 1.0f, 1.0f);
	Color3ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-352.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), "Color 3 Blue", false, 0.0f, 1.0f, 0.0f);

	Color4ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-416.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), "Color 4 Red", false, 0.0f, 1.0f, 1.0f);
	Color4ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-448.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), "Color 4 Green", false, 0.0f, 1.0f, 1.0f);
	Color4ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, (float)Height-480.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), "Color 4 Blue", false, 0.0f, 1.0f, 1.0f);

	ColorMapID=UI_AddCheckBox(&UI, Vec2((float)Width-375.0f, (float)Height-75.0f), 25.0f, Vec3(0.5f, 0.5f, 0.5f), "Color mapping", true);

	// Cursor has to be last, otherwise layer order will obstruct it
	CursorID=UI_AddCursor(&UI, Vec2(0.0f, 0.0f), 16.0f, Vec3(1.0f, 1.0f, 1.0f));

	return true;
}

// Rebuild vulkan swapchain and related data
void RecreateSwapchain(void)
{
	if(Context.Device!=VK_NULL_HANDLE) // Windows quirk, WM_SIZE is signaled on window creation, *before* Vulkan get initalized
	{
		UI.Size.x=(float)Width;
		UI.Size.y=(float)Height;

		UI_UpdateSpriteSize(&UI, SpriteID, UI.Size);

		// Wait for the device to complete any pending work
		vkDeviceWaitIdle(Context.Device);

		// To resize a surface, we need to destroy and recreate anything that's tied to the surface.
		// This is basically just the swapchain, framebuffers, and depthbuffer.

		// Swapchain, framebuffer, and depthbuffer destruction
		vkuDestroyImageBuffer(&Context, &DepthImage);

		// Destroy framebuffers
		for(uint32_t i=0;i<Swapchain.NumImages;i++)
			vkDestroyFramebuffer(Context.Device, PerFrame[i].FrameBuffer, VK_NULL_HANDLE);

		vkDestroyRenderPass(Context.Device, RenderPass, VK_NULL_HANDLE);
			
		// Destroy the swapchain
		vkuDestroySwapchain(&Context, &Swapchain);

		// Recreate the swapchain
		vkuCreateSwapchain(&Context, &Swapchain, Width, Height, VK_TRUE);

		// Recreate the framebuffer
		CreateFramebuffers(Width, Height);
	}
}

// Destroy call from system main
void Destroy(void)
{
	vkDeviceWaitIdle(Context.Device);

	UI_Destroy(&UI);
	Font_Destroy();

	vkuDestroyBuffer(&Context, &FireStagingBuffer);
	vkuDestroyImageBuffer(&Context, &FireImage);

	vkDestroyRenderPass(Context.Device, RenderPass, VK_NULL_HANDLE);

	// Swapchain, framebuffer, and depthbuffer destruction
	vkuDestroyImageBuffer(&Context, &DepthImage);

	for(uint32_t i=0;i<Swapchain.NumImages;i++)
	{
		// Destroy framebuffers
		vkDestroyFramebuffer(Context.Device, PerFrame[i].FrameBuffer, VK_NULL_HANDLE);

		// Destroy sync objects
		vkDestroyFence(Context.Device, PerFrame[i].FrameFence, VK_NULL_HANDLE);

		vkDestroySemaphore(Context.Device, PerFrame[i].PresentCompleteSemaphore, VK_NULL_HANDLE);
		vkDestroySemaphore(Context.Device, PerFrame[i].RenderCompleteSemaphore, VK_NULL_HANDLE);

		// Destroy main thread descriptor pools
		vkDestroyDescriptorPool(Context.Device, PerFrame[i].DescriptorPool, VK_NULL_HANDLE);

		// Destroy command pools
		vkDestroyCommandPool(Context.Device, PerFrame[i].CommandPool, VK_NULL_HANDLE);
	}

	vkuDestroySwapchain(&Context, &Swapchain);
	//////////

	DBGPRINTF(DEBUG_INFO"Remaining Vulkan memory blocks:\n");
	vkuMem_Print(VkZone);
	vkuMem_Destroy(&Context, VkZone);
	DBGPRINTF(DEBUG_NONE);
}
