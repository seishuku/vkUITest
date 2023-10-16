#ifndef __VULKAN_H__
#define __VULKAN_H__

#ifdef WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#else
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define vkCreateDebugUtilsMessengerEXT _vkCreateDebugUtilsMessengerEXT
extern PFN_vkCreateDebugUtilsMessengerEXT _vkCreateDebugUtilsMessengerEXT;

#define vkDestroyDebugUtilsMessengerEXT _vkDestroyDebugUtilsMessengerEXT
extern PFN_vkDestroyDebugUtilsMessengerEXT _vkDestroyDebugUtilsMessengerEXT;

#define vkCmdPushDescriptorSetKHR _vkCmdPushDescriptorSetKHR
extern PFN_vkCmdPushDescriptorSetKHR _vkCmdPushDescriptorSetKHR;

#define VKU_MAX_PIPELINE_VERTEX_BINDINGS 4
#define VKU_MAX_PIPELINE_VERTEX_ATTRIBUTES 16
#define VKU_MAX_PIPELINE_SHADER_STAGES 4

#define VKU_MAX_DESCRIPTORSET_BINDINGS 16

// This defines how many frames in flight
#define VKU_MAX_FRAME_COUNT 4

#define VKU_MAX_FRAMEBUFFER_ATTACHMENTS 8

#define VKU_MAX_RENDERPASS_ATTACHMENTS 8
#define VKU_MAX_RENDERPASS_SUBPASS_DEPENDENCIES 8

typedef struct
{
#ifdef WIN32
	HWND hWnd;
#else
	Display *Dpy;
	Window Win;
#endif

	VkSurfaceKHR Surface;

	uint32_t QueueFamilyIndex;
	VkPhysicalDevice PhysicalDevice;

	VkPhysicalDeviceProperties2 DeviceProperties;
	VkPhysicalDeviceMaintenance3Properties DeviceProperties2;
	VkPhysicalDeviceMemoryProperties DeviceMemProperties;

	VkDevice Device;
	VkQueue Queue;
	VkPipelineCache PipelineCache;
	VkCommandPool CommandPool;
} VkuContext_t;

typedef struct
{
	// Handles to dependencies
	VkDevice Device;
	VkPipelineCache PipelineCache;
	VkPipelineLayout PipelineLayout;
	VkRenderPass RenderPass;

	// Pipeline to be created
	VkPipeline Pipeline;

	// Settable states:

	// Vertex bindings
	uint32_t NumVertexBindings;
	VkVertexInputBindingDescription VertexBindings[VKU_MAX_PIPELINE_VERTEX_BINDINGS];

	// Vertex attributes
	uint32_t NumVertexAttribs;
	VkVertexInputAttributeDescription VertexAttribs[VKU_MAX_PIPELINE_VERTEX_ATTRIBUTES];

	// Shader Stages
	uint32_t NumStages;
	VkPipelineShaderStageCreateInfo Stages[VKU_MAX_PIPELINE_SHADER_STAGES];

	// Input assembly state
	VkPrimitiveTopology Topology;
	VkBool32 PrimitiveRestart;

	// Rasterization state
	VkBool32 DepthClamp;
	VkBool32 RasterizerDiscard;
	VkPolygonMode PolygonMode;
	VkCullModeFlags CullMode;
	VkFrontFace FrontFace;
	VkBool32 DepthBias;
	float DepthBiasConstantFactor;
	float DepthBiasClamp;
	float DepthBiasSlopeFactor;
	float LineWidth;

	// Depth/stencil state
	VkBool32 DepthTest;
	VkBool32 DepthWrite;
	VkCompareOp DepthCompareOp;
	VkBool32 DepthBoundsTest;
	VkBool32 StencilTest;
	float MinDepthBounds;
	float MaxDepthBounds;

	// Front face stencil functions
	VkStencilOp FrontStencilFailOp;
	VkStencilOp FrontStencilPassOp;
	VkStencilOp FrontStencilDepthFailOp;
	VkCompareOp FrontStencilCompareOp;
	uint32_t FrontStencilCompareMask;
	uint32_t FrontStencilWriteMask;
	uint32_t FrontStencilReference;

	// Back face stencil functions
	VkStencilOp BackStencilFailOp;
	VkStencilOp BackStencilPassOp;
	VkStencilOp BackStencilDepthFailOp;
	VkCompareOp BackStencilCompareOp;
	uint32_t BackStencilCompareMask;
	uint32_t BackStencilWriteMask;
	uint32_t BackStencilReference;

	// Multisample state
	VkSampleCountFlagBits RasterizationSamples;
	VkBool32 SampleShading;
	float MinSampleShading;
	const VkSampleMask *SampleMask;
	VkBool32 AlphaToCoverage;
	VkBool32 AlphaToOne;

	// Blend state
	VkBool32 BlendLogicOp;
	VkLogicOp BlendLogicOpState;
	VkBool32 Blend;
	VkBlendFactor SrcColorBlendFactor;
	VkBlendFactor DstColorBlendFactor;
	VkBlendOp ColorBlendOp;
	VkBlendFactor SrcAlphaBlendFactor;
	VkBlendFactor DstAlphaBlendFactor;
	VkBlendOp AlphaBlendOp;
	VkColorComponentFlags ColorWriteMask;
} VkuPipeline_t;

typedef struct
{
	VkDevice Device;

	VkDescriptorSetLayout DescriptorSetLayout;
	VkDescriptorSet DescriptorSet;

	uint32_t NumBindings;
	VkDescriptorSetLayoutBinding Bindings[VKU_MAX_DESCRIPTORSET_BINDINGS];
	VkWriteDescriptorSet WriteDescriptorSet[VKU_MAX_DESCRIPTORSET_BINDINGS];
	VkDescriptorImageInfo ImageInfo[VKU_MAX_DESCRIPTORSET_BINDINGS];
	VkDescriptorBufferInfo BufferInfo[VKU_MAX_DESCRIPTORSET_BINDINGS];
} VkuDescriptorSet_t;

typedef struct VkuMemBlock_s
{
	size_t Offset;
	size_t Size;
	bool Free;
	struct VkuMemBlock_s *Next, *Prev;
} VkuMemBlock_t;

typedef struct
{
	size_t Size;
	VkuMemBlock_t *Blocks;
	VkDeviceMemory DeviceMemory;
} VkuMemZone_t;

typedef struct
{
	size_t Size;
	VkBuffer Buffer;
	VkDeviceMemory DeviceMemory;
	VkuMemBlock_t *Memory;
} VkuBuffer_t;

typedef struct
{
	uint32_t Width, Height, Depth;
	uint8_t *Data;

	VkSampler Sampler;
	VkImage Image;
	VkuMemBlock_t *DeviceMemory;
	VkImageView View;
} VkuImage_t;

typedef struct
{
	VkSwapchainKHR Swapchain;

	VkExtent2D Extent;
	VkSurfaceFormatKHR SurfaceFormat;

	uint32_t NumImages;
	VkImage Image[VKU_MAX_FRAME_COUNT];
	VkImageView ImageView[VKU_MAX_FRAME_COUNT];
} VkuSwapchain_t;

typedef struct
{
	VkDevice Device;
	VkRenderPass RenderPass;

	VkFramebuffer Framebuffer;

	uint32_t NumAttachments;
	VkImageView Attachments[VKU_MAX_FRAMEBUFFER_ATTACHMENTS];
} VkuFramebuffer_t;

typedef enum
{
	VKU_RENDERPASS_ATTACHMENT_INPUT=0,
	VKU_RENDERPASS_ATTACHMENT_COLOR,
	VKU_RENDERPASS_ATTACHMENT_DEPTH,
	VKU_RENDERPASS_ATTACHMENT_RESOLVE
} VkuRenderPassAttachmentType;

typedef struct
{
	VkuRenderPassAttachmentType Type;
	VkFormat Format;
	VkSampleCountFlagBits Samples;
	VkAttachmentLoadOp LoadOp;
	VkAttachmentStoreOp StoreOp;
	VkImageLayout SubpassLayout;
	VkImageLayout FinalLayout;
} VkuRenderPassAttachments_t;

typedef struct
{
	VkDevice Device;

	VkRenderPass RenderPass;

	uint32_t NumAttachments;
	VkuRenderPassAttachments_t Attachments[VKU_MAX_RENDERPASS_ATTACHMENTS];

	uint32_t NumSubpassDependencies;
	VkSubpassDependency SubpassDependencies[VKU_MAX_RENDERPASS_SUBPASS_DEPENDENCIES];
} VkuRenderPass_t;

VkShaderModule vkuCreateShaderModule(VkDevice Device, const char *shaderFile);

uint32_t vkuMemoryTypeFromProperties(VkPhysicalDeviceMemoryProperties memory_properties, uint32_t typeBits, VkFlags requirements_mask);

VkBool32 vkuCreateImageBuffer(VkuContext_t* Context, VkuImage_t* Image, VkImageType ImageType, VkFormat Format, uint32_t MipLevels, uint32_t Layers, uint32_t Width, uint32_t Height, uint32_t Depth, VkSampleCountFlagBits Samples, VkImageTiling Tiling, VkBufferUsageFlags Flags, VkFlags RequirementsMask, VkImageCreateFlags CreateFlags);
void vkuCreateTexture2D(VkuContext_t *Context, VkuImage_t *Image, uint32_t Width, uint32_t Height, VkFormat Format, VkSampleCountFlagBits Samples);
void vkuDestroyImageBuffer(VkuContext_t *Context, VkuImage_t *Image);

VkBool32 vkuCreateHostBuffer(VkuContext_t *Context, VkuBuffer_t *Buffer, uint32_t Size, VkBufferUsageFlags Flags);
VkBool32 vkuCreateGPUBuffer(VkuContext_t *Context, VkuBuffer_t *Buffer, uint32_t Size, VkBufferUsageFlags Flags);
void vkuDestroyBuffer(VkuContext_t *Context, VkuBuffer_t *Buffer);

void vkuTransitionLayout(VkCommandBuffer CommandBuffer, VkImage Image, uint32_t levelCount, uint32_t baseLevel, uint32_t layerCount, uint32_t baseLayer, VkImageLayout oldLayout, VkImageLayout newLayout);

VkCommandBuffer vkuOneShotCommandBufferBegin(VkuContext_t *Context);
VkBool32 vkuOneShotCommandBufferFlush(VkuContext_t *Context, VkCommandBuffer CommandBuffer);
VkBool32 vkuOneShotCommandBufferEnd(VkuContext_t *Context, VkCommandBuffer CommandBuffer);

VkBool32 vkuPipeline_AddVertexBinding(VkuPipeline_t *Pipeline, uint32_t Binding, uint32_t Stride, VkVertexInputRate InputRate);
VkBool32 vkuPipeline_AddVertexAttribute(VkuPipeline_t *Pipeline, uint32_t Location, uint32_t Binding, VkFormat Format, uint32_t Offset);
VkBool32 vkuPipeline_AddStage(VkuPipeline_t *Pipeline, const char *ShaderFilename, VkShaderStageFlagBits Stage);
VkBool32 vkuPipeline_SetRenderPass(VkuPipeline_t *Pipeline, VkRenderPass RenderPass);
VkBool32 vkuPipeline_SetPipelineLayout(VkuPipeline_t *Pipeline, VkPipelineLayout PipelineLayout);
VkBool32 vkuInitPipeline(VkuPipeline_t *Pipeline, VkuContext_t *Context);
VkBool32 vkuAssemblePipeline(VkuPipeline_t *Pipeline, void *pNext);

VkBool32 vkuDescriptorSet_AddBinding(VkuDescriptorSet_t *DescriptorSet, uint32_t Binding, VkDescriptorType Type, VkShaderStageFlags Stage);
VkBool32 vkuDescriptorSet_UpdateBindingImageInfo(VkuDescriptorSet_t *DescriptorSet, uint32_t Binding, VkuImage_t *Image);
VkBool32 vkuDescriptorSet_UpdateBindingBufferInfo(VkuDescriptorSet_t *DescriptorSet, uint32_t Binding, VkBuffer Buffer, VkDeviceSize Offset, VkDeviceSize Range);
VkBool32 vkuInitDescriptorSet(VkuDescriptorSet_t *DescriptorSet, VkuContext_t *Context);
VkBool32 vkuAssembleDescriptorSetLayout(VkuDescriptorSet_t *DescriptorSet);
VkBool32 vkuAllocateUpdateDescriptorSet(VkuDescriptorSet_t *DescriptorSet, VkDescriptorPool DescriptorPool);

VkuMemZone_t *vkuMem_Init(VkuContext_t *Context, size_t Size);
void vkuMem_Destroy(VkuContext_t *Context, VkuMemZone_t *VkZone);
void vkuMem_Free(VkuMemZone_t *VkZone, VkuMemBlock_t *Ptr);
VkuMemBlock_t *vkuMem_Malloc(VkuMemZone_t *VkZone, VkMemoryRequirements Requirements);
void vkuMem_Print(VkuMemZone_t *VkZone);

VkBool32 CreateVulkanInstance(VkInstance *Instance);
VkBool32 CreateVulkanContext(VkInstance Instance, VkuContext_t *Context);
void DestroyVulkan(VkInstance Instance, VkuContext_t *Context);

VkBool32 vkuCreateSwapchain(VkuContext_t *Context, VkuSwapchain_t *Swapchain, uint32_t Width, uint32_t Height, VkBool32 VSync);
void vkuDestroySwapchain(VkuContext_t *Context, VkuSwapchain_t *Swapchain);

VkBool32 vkuFramebufferAddAttachment(VkuFramebuffer_t *Framebuffer, VkImageView Attachment);
VkBool32 vkuInitFramebuffer(VkuFramebuffer_t *Framebuffer, VkuContext_t *Context, VkRenderPass RenderPass);
VkBool32 vkuFramebufferCreate(VkuFramebuffer_t *Framebuffer, VkExtent2D Size);

VkBool32 vkuRenderPass_AddAttachment(VkuRenderPass_t *RenderPass, VkuRenderPassAttachmentType Type, VkFormat Format, VkSampleCountFlagBits Samples, VkAttachmentLoadOp LoadOp, VkAttachmentStoreOp StoreOp, VkImageLayout SubpassLayout, VkImageLayout FinalLayout);
VkBool32 vkuRenderPass_AddSubpassDependency(VkuRenderPass_t *RenderPass, uint32_t SourceSubpass, uint32_t DestinationSubpass, VkPipelineStageFlags SourceStageMask, VkPipelineStageFlags DestinationStageMask, VkAccessFlags SourceAccessMask, VkAccessFlags DestinationAccessMask, VkDependencyFlags DependencyFlags);
VkBool32 vkuInitRenderPass(VkuContext_t *Context, VkuRenderPass_t *RenderPass);
VkBool32 vkuCreateRenderPass(VkuRenderPass_t *RenderPass);

#endif
