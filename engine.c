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
//#include "vr/vr.h"
#include "font/font.h"
#include "audio/audio.h"
#include "physics/physics.h"
#include "ui/ui.h"
#include "utils/config.h"
#include "perframe.h"

// Done flag from system code
extern bool isDone;

// Vulkan instance handle and context structs
VkInstance vkInstance;
VkuContext_t vkContext;

// extern timing data from system main
extern float fps, fTimeStep, fTime;

// Vulkan swapchain helper struct
VkuSwapchain_t swapchain;

// Depth buffer image and format
VkFormat depthFormat;
VkuImage_t depthImage;

VkRenderPass renderPass;

VkuImage_t faceTexture;

Sample_t explode;

Font_t font;
UI_t UI;

PerFrame_t perFrame[FRAMES_IN_FLIGHT];

uint32_t cursorID=UINT32_MAX;

uint32_t sliderID=UINT32_MAX;

uint32_t colorWinID=UINT32_MAX;
uint32_t color1ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };
uint32_t color2ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };
uint32_t color3ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };
uint32_t color4ID[3]={ UINT32_MAX, UINT32_MAX, UINT32_MAX };

uint32_t colorMapID=UINT32_MAX;

uint32_t spriteID=UINT32_MAX;
uint32_t faceID=UINT32_MAX;

VkuBuffer_t fireStagingBuffer;
VkuImage_t fireImage;

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

void FireRunStep(uint32_t index)
{
	for(uint32_t i=0;i<FIRE_WIDTH*4;i++)
		buffer1[Random()%(FIRE_WIDTH*4)]=Random()%255;

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

	vec3 Color1=Vec3(UI_GetBarGraphValue(&UI, color1ID[0]), UI_GetBarGraphValue(&UI, color1ID[1]), UI_GetBarGraphValue(&UI, color1ID[2]));
	vec3 Color2=Vec3(UI_GetBarGraphValue(&UI, color2ID[0]), UI_GetBarGraphValue(&UI, color2ID[1]), UI_GetBarGraphValue(&UI, color2ID[2]));
	vec3 Color3=Vec3(UI_GetBarGraphValue(&UI, color3ID[0]), UI_GetBarGraphValue(&UI, color3ID[1]), UI_GetBarGraphValue(&UI, color3ID[2]));
	vec3 Color4=Vec3(UI_GetBarGraphValue(&UI, color4ID[0]), UI_GetBarGraphValue(&UI, color4ID[1]), UI_GetBarGraphValue(&UI, color4ID[2]));

	uint8_t *fb=(uint8_t *)fireStagingBuffer.memory->mappedPointer;

	for(uint32_t y=0;y<FIRE_HEIGHT;y++)
	{
		int flipy=FIRE_HEIGHT-1-y;

		for(uint32_t x=0;x<FIRE_WIDTH;x++)
		{
			float Fire=clampf(((float)buffer2[flipy*FIRE_WIDTH+x]/255.0f)-UI_GetBarGraphValue(&UI, sliderID), 0.0f, 1.0f);
			vec3 Color;

			if(UI_GetCheckBoxValue(&UI, colorMapID))
				Color=Bezier(clampf(Fire, 0.0f, 1.0f), Color1, Color2, Color3, Color4);
			else
				Color=Vec3b(Fire);

			fb[4*(y*FIRE_WIDTH+x)+0]=(uint8_t)(Color.z*255.0f);
			fb[4*(y*FIRE_WIDTH+x)+1]=(uint8_t)(Color.y*255.0f);
			fb[4*(y*FIRE_WIDTH+x)+2]=(uint8_t)(Color.x*255.0f);
			fb[4*(y*FIRE_WIDTH+x)+3]=0xFF;
		}
	}

	memcpy(buffer1, buffer2, FIRE_WIDTH*FIRE_HEIGHT);

	vkuTransitionLayout(perFrame[index].commandBuffer, fireImage.image, 1, 0, 1, 0, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkCmdCopyBufferToImage(perFrame[index].commandBuffer, fireStagingBuffer.buffer, fireImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, (VkBufferImageCopy[1])
	{
		{
			0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { FIRE_WIDTH, FIRE_HEIGHT, 1 }
		}
	});
	vkuTransitionLayout(perFrame[index].commandBuffer, fireImage.image, 1, 0, 1, 0, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RecreateSwapchain(void);

// Create functions for creating render data for asteroids
bool CreateFramebuffers(uint32_t targetWidth, uint32_t targetHeight)
{
	VkImageFormatProperties imageFormatProps;
	VkResult result;

	depthFormat=VK_FORMAT_D32_SFLOAT_S8_UINT;
	result=vkGetPhysicalDeviceImageFormatProperties(vkContext.physicalDevice,
															 depthFormat,
															 VK_IMAGE_TYPE_2D,
															 VK_IMAGE_TILING_OPTIMAL,
															 VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
															 0,
															 &imageFormatProps);

	if(result!=VK_SUCCESS)
	{
		depthFormat=VK_FORMAT_D24_UNORM_S8_UINT;
		result=vkGetPhysicalDeviceImageFormatProperties(vkContext.physicalDevice, depthFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &imageFormatProps);

		if(result!=VK_SUCCESS)
		{
			DBGPRINTF(DEBUG_ERROR, "CreateFramebuffers: No suitable depth format found.\n");
			return false;
		}
	}

	vkuCreateTexture2D(&vkContext, &depthImage, targetWidth, targetHeight, depthFormat, VK_SAMPLE_COUNT_1_BIT);

	VkCommandBuffer CommandBuffer=vkuOneShotCommandBufferBegin(&vkContext);
	vkuTransitionLayout(CommandBuffer, depthImage.image, 1, 0, 1, 0, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	vkuOneShotCommandBufferEnd(&vkContext, CommandBuffer);

	vkCreateRenderPass(vkContext.device, &(VkRenderPassCreateInfo)
	{
		.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount=2,
		.pAttachments=(VkAttachmentDescription[])
		{
			{
				.format=swapchain.surfaceFormat.format,
				.samples=VK_SAMPLE_COUNT_1_BIT,
				.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			{
				.format=depthFormat,
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
	}, 0, &renderPass);

	for(uint32_t i=0;i<swapchain.numImages;i++)
	{
		if(vkCreateFramebuffer(vkContext.device, &(VkFramebufferCreateInfo)
		{
			.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass=renderPass,
			.attachmentCount=2,
			.pAttachments=(VkImageView[]){ swapchain.imageView[i], depthImage.imageView },
			.width=swapchain.extent.width,
			.height=swapchain.extent.height,
			.layers=1,
		}, 0, &perFrame[i].frameBuffer)!=VK_SUCCESS)
			return false;
	}

	return true;
}

// Render call from system main event loop
void Render(void)
{
	static float fireTime=0.0f;
	const float OneOver60=1.0f/60.0f;
	static uint32_t index=0;
	uint32_t imageIndex;

	vkWaitForFences(vkContext.device, 1, &perFrame[index].frameFence, VK_TRUE, UINT64_MAX);

	UI_UpdateSpritePosition(&UI, faceID, Vec2(sinf(fTime*4.0f)*50.0f+(config.renderWidth/2), cosf(fTime*4.0f)*50.0f+(config.renderHeight/2)));

	VkResult result=vkAcquireNextImageKHR(vkContext.device, swapchain.swapchain, UINT64_MAX, perFrame[index].completeSemaphore, VK_NULL_HANDLE, &imageIndex);

	if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR)
	{
		DBGPRINTF(DEBUG_ERROR, "Swapchain out of date... Rebuilding.\n");
		RecreateSwapchain();
		return;
	}

	// Reset the frame fence and command pool (and thus the command buffer)
	vkResetFences(vkContext.device, 1, &perFrame[index].frameFence);
	vkResetDescriptorPool(vkContext.device, perFrame[index].descriptorPool, 0);
	vkResetCommandPool(vkContext.device, perFrame[index].commandPool, 0);

	// Start recording the commands
	vkBeginCommandBuffer(perFrame[index].commandBuffer, &(VkCommandBufferBeginInfo)
	{
		.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	});

	// Run the fire at 60FPS
	fireTime+=fTimeStep;
	if(fireTime>=OneOver60)
	{
		fireTime=0.0;
		FireRunStep(index);
	}

	// Start a render pass and clear the frame/depth buffer
	vkCmdBeginRenderPass(perFrame[index].commandBuffer, &(VkRenderPassBeginInfo)
	{
		.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass=renderPass,
		.framebuffer=perFrame[imageIndex].frameBuffer,
		.renderArea=(VkRect2D){ { 0, 0 }, { config.renderWidth, config.renderHeight } },
		.clearValueCount=2,
		.pClearValues=(VkClearValue[]){ {{{ 0.0f, 0.0f, 0.0f, 1.0f }}}, {{{ 0.0f, 0 }}} },
	}, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetViewport(perFrame[index].commandBuffer, 0, 1, &(VkViewport) { 0.0f, 0, (float)config.renderWidth, (float)config.renderHeight, 0.0f, 1.0f });
	vkCmdSetScissor(perFrame[index].commandBuffer, 0, 1, &(VkRect2D) { { 0, 0 }, { config.renderWidth, config.renderHeight } });

	UI_Draw(&UI, index, 0, fTimeStep);

	Font_Print(&font, 32.0f, 0.0f, 0.0f, "FPS: %0.1f\n\x1B[91mFrame time: %0.5fms", fps, fTimeStep*1000.0f);
	Font_Draw(&font, index, 0);
	
	// Reset the font text collection for the next frame
	Font_Reset(&font);

	vkCmdEndRenderPass(perFrame[index].commandBuffer);

	vkEndCommandBuffer(perFrame[index].commandBuffer);

	// Submit command queue
	vkQueueSubmit(vkContext.graphicsQueue, 1, &(VkSubmitInfo)
	{
		.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pWaitDstStageMask=&(VkPipelineStageFlags) { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT },
		.waitSemaphoreCount=1,
		.pWaitSemaphores=&perFrame[index].completeSemaphore,
		.signalSemaphoreCount=1,
		.pSignalSemaphores=&swapchain.waitSemaphore[imageIndex],
		.commandBufferCount=1,
		.pCommandBuffers=&perFrame[index].commandBuffer,
	}, perFrame[index].frameFence);

	// And present it to the screen
	result=vkQueuePresentKHR(vkContext.graphicsQueue, &(VkPresentInfoKHR)
	{
		.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount=1,
		.pWaitSemaphores=&swapchain.waitSemaphore[imageIndex],
		.swapchainCount=1,
		.pSwapchains=&swapchain.swapchain,
		.pImageIndices=&imageIndex,
	});

	if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR)
	{
		DBGPRINTF(DEBUG_ERROR, "Swapchain out of date... Rebuilding.\n");
		RecreateSwapchain();
		return;
	}

	index=(index+1)%FRAMES_IN_FLIGHT;
}

void ExitButton(void *arg)
{
	isDone=true;
}

void ExplodeButton(void *arg)
{
	Audio_PlaySample(&explode, false, 1.0f, Vec3b(0.0f));
}

// Initialization call from system main
bool Init(void)
{
	RandomSeed(123);

	vkuMemAllocator_Init(&vkContext);

	if(!Audio_Init())
		return false;

	if(!Audio_LoadStatic("assets/explode.wav", &explode))
		return false;

	// Create primary frame buffers, depth image
	CreateFramebuffers(config.renderWidth, config.renderHeight);

	vkuCreateHostBuffer(&vkContext, &fireStagingBuffer, FIRE_WIDTH*FIRE_HEIGHT*4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	vkuCreateTexture2D(&vkContext, &fireImage, FIRE_WIDTH, FIRE_HEIGHT, VK_FORMAT_B8G8R8A8_SRGB, VK_SAMPLE_COUNT_1_BIT);

	VkCommandBuffer CommandBuffer=vkuOneShotCommandBufferBegin(&vkContext);
	vkuTransitionLayout(CommandBuffer, fireImage.image, 1, 0, 1, 0, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vkuOneShotCommandBufferEnd(&vkContext, CommandBuffer);

	// Other per-frame data
	for(uint32_t i=0;i<FRAMES_IN_FLIGHT;i++)
	{
		// Create needed fence and semaphores for rendering
		// Wait fence for command queue, to signal when we can submit commands again
		vkCreateFence(vkContext.device, &(VkFenceCreateInfo) {.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags=VK_FENCE_CREATE_SIGNALED_BIT }, VK_NULL_HANDLE, &perFrame[i].frameFence);

		// Semaphore for render complete, to signal when we can render again
		vkCreateSemaphore(vkContext.device, &(VkSemaphoreCreateInfo) {.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext=VK_NULL_HANDLE }, VK_NULL_HANDLE, &perFrame[i].completeSemaphore);

		// Per-frame descriptor pool for main thread
		vkCreateDescriptorPool(vkContext.device, &(VkDescriptorPoolCreateInfo)
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
		}, VK_NULL_HANDLE, &perFrame[i].descriptorPool);

		// Create per-frame command pools
		vkCreateCommandPool(vkContext.device, &(VkCommandPoolCreateInfo)
		{
			.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags=0,
			.queueFamilyIndex=vkContext.graphicsQueueIndex,
		}, VK_NULL_HANDLE, &perFrame[i].commandPool);

		// Allocate the command buffers we will be rendering into
		vkAllocateCommandBuffers(vkContext.device, &(VkCommandBufferAllocateInfo)
		{
			.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool=perFrame[i].commandPool,
			.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount=1,
		}, &perFrame[i].commandBuffer);
	}

	Image_Upload(&vkContext, &faceTexture, "assets/face.tga", IMAGE_BILINEAR);

	Font_Init(&font);

	UI_Init(&UI, Vec2(0.0f, 0.0f), Vec2((float)config.renderWidth, (float)config.renderHeight));

	spriteID=UI_AddSprite(&UI, Vec2((float)config.renderWidth/2.0f, (float)config.renderHeight/2.0f), Vec2(FIRE_WIDTH, FIRE_HEIGHT), Vec3(1.0f, 1.0f, 1.0f), false, &fireImage, 0.0f);

	faceID=UI_AddSprite(&UI, Vec2((float)config.renderWidth/2.0f, (float)config.renderHeight/2.0f), Vec2(128.0f, 128.0f), Vec3(1.0f, 1.0f, 1.0f), false, &faceTexture, 0.0f);

	UI_AddButton(&UI, Vec2((float)config.renderWidth-200.0f, 100.0f), Vec2(100.0f, 50.0f), Vec3(0.5f, 0.5f, 0.5f), false, "Exit", ExitButton);
	UI_AddButton(&UI, Vec2((float)config.renderWidth-200.0f, 400.0f), Vec2(100.0f, 50.0f), Vec3(0.5f, 0.5f, 0.5f), false, "Explode", ExplodeButton);

	sliderID=UI_AddBarGraph(&UI, Vec2((float)config.renderWidth-400.0f, (float)config.renderHeight-50.0f), Vec2(400.0f, 50.0f), Vec3(0.5f, 0.5f, 0.5f), false, "Fire", false, 0.4f, 0.0f, 0.0f);

	colorWinID=UI_AddWindow(&UI, Vec2(10.0f, config.renderHeight-32.0f), Vec2(256.0f, 480.0f), Vec3(0.25, 0.25, 0.25), false, "Color Controls");

	color1ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, -32.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), false, "Color 1 Red", false, 0.0f, 1.0f, 0.0f);
	color1ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, -64.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), false, "Color 1 Green", false, 0.0f, 1.0f, 0.0f);
	color1ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, -96.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), false, "Color 1 Blue", false, 0.0f, 1.0f, 0.0f);

	color2ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, -160.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), false, "Color 2 Red", false, 0.0f, 1.0f, 1.0f);
	color2ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, -192.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), false, "Color 2 Green", false, 0.0f, 1.0f, 0.0f);
	color2ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, -224.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), false, "Color 2 Blue", false, 0.0f, 1.0f, 0.0f);

	color3ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, -288.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), false, "Color 3 Red", false, 0.0f, 1.0f, 1.0f);
	color3ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, -320.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), false, "Color 3 Green", false, 0.0f, 1.0f, 1.0f);
	color3ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, -352.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), false, "Color 3 Blue", false, 0.0f, 1.0f, 0.0f);

	color4ID[0]=UI_AddBarGraph(&UI, Vec2(0.0f, -416.0f), Vec2(256.0f, 32.0f), Vec3(1.0f, 0.0f, 0.0f), false, "Color 4 Red", false, 0.0f, 1.0f, 1.0f);
	color4ID[1]=UI_AddBarGraph(&UI, Vec2(0.0f, -448.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 1.0f, 0.0f), false, "Color 4 Green", false, 0.0f, 1.0f, 1.0f);
	color4ID[2]=UI_AddBarGraph(&UI, Vec2(0.0f, -480.0f), Vec2(256.0f, 32.0f), Vec3(0.0f, 0.0f, 1.0f), false, "Color 4 Blue", false, 0.0f, 1.0f, 1.0f);

	UI_WindowAddControl(&UI, colorWinID, color1ID[0]);
	UI_WindowAddControl(&UI, colorWinID, color1ID[1]);
	UI_WindowAddControl(&UI, colorWinID, color1ID[2]);
	UI_WindowAddControl(&UI, colorWinID, color2ID[0]);
	UI_WindowAddControl(&UI, colorWinID, color2ID[1]);
	UI_WindowAddControl(&UI, colorWinID, color2ID[2]);
	UI_WindowAddControl(&UI, colorWinID, color3ID[0]);
	UI_WindowAddControl(&UI, colorWinID, color3ID[1]);
	UI_WindowAddControl(&UI, colorWinID, color3ID[2]);
	UI_WindowAddControl(&UI, colorWinID, color4ID[0]);
	UI_WindowAddControl(&UI, colorWinID, color4ID[1]);
	UI_WindowAddControl(&UI, colorWinID, color4ID[2]);

	colorMapID=UI_AddCheckBox(&UI, Vec2((float)config.renderWidth-375.0f, (float)config.renderHeight-75.0f), 25.0f, Vec3(0.5f, 0.5f, 0.5f), false, "Color mapping", true);

	// Cursor has to be last, otherwise layer order will obstruct it
	cursorID=UI_AddCursor(&UI, Vec2(0.0f, 0.0f), 16.0, Vec3(1.0f, 1.0f, 1.0f), false);

	if(!Zone_VerifyHeap(zone))
		exit(-1);

	DBGPRINTF(DEBUG_WARNING, "\nCurrent vulkan zone memory allocations:\n");
	vkuMemAllocator_Print();

	return true;
}

// Rebuild vulkan swapchain and related data
void RecreateSwapchain(void)
{
	if(vkContext.device!=VK_NULL_HANDLE) // Windows quirk, WM_SIZE is signaled on window creation, *before* Vulkan get initalized
	{
		// Wait for the device to complete any pending work
		vkDeviceWaitIdle(vkContext.device);

		// To resize a surface, we need to destroy and recreate anything that's tied to the surface.
		// This is basically just the swapchain, framebuffers, and depthbuffer.

		// Swapchain, framebuffer, and depthbuffer destruction
		vkuDestroyImageBuffer(&vkContext, &depthImage);

		// Destroy framebuffers
		for(uint32_t i=0;i<swapchain.numImages;i++)
			vkDestroyFramebuffer(vkContext.device, perFrame[i].frameBuffer, VK_NULL_HANDLE);

		vkDestroyRenderPass(vkContext.device, renderPass, VK_NULL_HANDLE);
			
		// Destroy the swapchain
		vkuDestroySwapchain(&vkContext, &swapchain);

		// Recreate the swapchain
		vkuCreateSwapchain(&vkContext, &swapchain, VK_TRUE);

		config.renderWidth=swapchain.extent.width;
		config.renderHeight=swapchain.extent.height;
		UI.size.x=(float)config.renderWidth;
		UI.size.y=(float)config.renderHeight;

		// Recreate the framebuffer
		CreateFramebuffers(config.renderWidth, config.renderHeight);
	}
}

// Destroy call from system main
void Destroy(void)
{
	vkDeviceWaitIdle(vkContext.device);

	Audio_Destroy();

	Zone_Free(zone, explode.data);

	UI_Destroy(&UI);
	Font_Destroy(&font);

	vkuDestroyBuffer(&vkContext, &fireStagingBuffer);
	vkuDestroyImageBuffer(&vkContext, &fireImage);

	vkuDestroyImageBuffer(&vkContext, &faceTexture);

	vkDestroyRenderPass(vkContext.device, renderPass, VK_NULL_HANDLE);

	// Swapchain, framebuffer, and depthbuffer destruction
	vkuDestroyImageBuffer(&vkContext, &depthImage);

	// Destroy framebuffers
	for(uint32_t i=0;i<swapchain.numImages;i++)
		vkDestroyFramebuffer(vkContext.device, perFrame[i].frameBuffer, VK_NULL_HANDLE);

	for(uint32_t i=0;i<FRAMES_IN_FLIGHT;i++)
	{
		// Destroy sync objects
		vkDestroyFence(vkContext.device, perFrame[i].frameFence, VK_NULL_HANDLE);
		vkDestroySemaphore(vkContext.device, perFrame[i].completeSemaphore, VK_NULL_HANDLE);

		// Destroy main thread descriptor pools
		vkDestroyDescriptorPool(vkContext.device, perFrame[i].descriptorPool, VK_NULL_HANDLE);

		// Destroy command pools
		vkDestroyCommandPool(vkContext.device, perFrame[i].commandPool, VK_NULL_HANDLE);
	}

	vkuDestroySwapchain(&vkContext, &swapchain);
	//////////

	DBGPRINTF(DEBUG_INFO, "Remaining Vulkan memory blocks:\n");
	vkuMemAllocator_Print();
	vkuMemAllocator_Destroy();
}
