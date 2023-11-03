/*
	Copyright 2023 Matt Williams/NitroGL
	SDF Vulakn Font/Text printing function
	Uses two vertex buffers, one for a single triangle strip making up a quad,
	  the other contains instancing data for character position, size, and color.
	Font is completely internal to the fragment shader, no external texture.

	SDF font design courtesy of Andr� van Kammen (https://www.shadertoy.com/view/4s3XDn)
*/
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../system/system.h"
#include "../vulkan/vulkan.h"
#include "../math/math.h"
#include "../font/font.h"
#include "../perframe.h"

// external Vulkan context data/functions for this module:
extern VkuContext_t Context;
extern VkSampleCountFlags MSAA;
extern VkuSwapchain_t Swapchain;

extern VkuMemZone_t *VkZone;

extern VkRenderPass RenderPass;
// ---

bool Font_Init(Font_t *Font)
{
	VkuBuffer_t stagingBuffer;
	VkCommandBuffer CopyCommand;
	void *data=NULL;

	// Create descriptors and pipeline
	vkuInitDescriptorSet(&Font->DescriptorSet, &Context);
	vkuAssembleDescriptorSetLayout(&Font->DescriptorSet);

	vkCreatePipelineLayout(Context.Device, &(VkPipelineLayoutCreateInfo)
	{
		.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount=1,
		.pSetLayouts=&Font->DescriptorSet.DescriptorSetLayout,
		.pushConstantRangeCount=1,
		.pPushConstantRanges=&(VkPushConstantRange)
		{
			.offset=0,
			.size=sizeof(uint32_t)*2,
			.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	}, 0, &Font->PipelineLayout);

	vkuInitPipeline(&Font->Pipeline, &Context);

	vkuPipeline_SetPipelineLayout(&Font->Pipeline, Font->PipelineLayout);
	vkuPipeline_SetRenderPass(&Font->Pipeline, RenderPass);

	Font->Pipeline.Topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	Font->Pipeline.CullMode=VK_CULL_MODE_BACK_BIT;
	Font->Pipeline.RasterizationSamples=VK_SAMPLE_COUNT_1_BIT;

	Font->Pipeline.Blend=VK_TRUE;
	Font->Pipeline.SrcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
	Font->Pipeline.DstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	Font->Pipeline.ColorBlendOp=VK_BLEND_OP_ADD;
	Font->Pipeline.SrcAlphaBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
	Font->Pipeline.DstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	Font->Pipeline.AlphaBlendOp=VK_BLEND_OP_ADD;

	if(!vkuPipeline_AddStage(&Font->Pipeline, "shaders/font.vert.spv", VK_SHADER_STAGE_VERTEX_BIT))
		return false;

	if(!vkuPipeline_AddStage(&Font->Pipeline, "shaders/font.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT))
		return false;

	vkuPipeline_AddVertexBinding(&Font->Pipeline, 0, sizeof(vec4), VK_VERTEX_INPUT_RATE_VERTEX);
	vkuPipeline_AddVertexAttribute(&Font->Pipeline, 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(vec4)*0);

	vkuPipeline_AddVertexBinding(&Font->Pipeline, 1, sizeof(vec4)*2, VK_VERTEX_INPUT_RATE_INSTANCE);
	vkuPipeline_AddVertexAttribute(&Font->Pipeline, 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(vec4)*0);
	vkuPipeline_AddVertexAttribute(&Font->Pipeline, 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(vec4)*1);

	//VkPipelineRenderingCreateInfo PipelineRenderingCreateInfo=
	//{
	//	.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
	//	.colorAttachmentCount=1,
	//	.pColorAttachmentFormats=&Swapchain.SurfaceFormat.format,
	//};

	if(!vkuAssemblePipeline(&Font->Pipeline, VK_NULL_HANDLE/*&PipelineRenderingCreateInfo*/))
		return false;
	// ---

	// Create static vertex data buffer
	vkuCreateGPUBuffer(&Context, &Font->VertexBuffer, sizeof(float)*4*4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	// Create staging buffer, map it, and copy vertex data to it
	vkuCreateHostBuffer(&Context, &stagingBuffer, sizeof(float)*4*4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

	// Map it
	vkMapMemory(Context.Device, stagingBuffer.DeviceMemory, 0, VK_WHOLE_SIZE, 0, &data);

	if(data==NULL)
		return false;

	vec4 *Ptr=(vec4 *)data;

	*Ptr++=Vec4(-0.5f, 1.0f, -1.0f, 1.0f);	// XYUV
	*Ptr++=Vec4(-0.5f, 0.0f, -1.0f, -1.0f);
	*Ptr++=Vec4(0.5f, 1.0f, 1.0f, 1.0f);
	*Ptr++=Vec4(0.5f, 0.0f, 1.0f, -1.0f);

	vkUnmapMemory(Context.Device, stagingBuffer.DeviceMemory);

	CopyCommand=vkuOneShotCommandBufferBegin(&Context);
	vkCmdCopyBuffer(CopyCommand, stagingBuffer.Buffer, Font->VertexBuffer.Buffer, 1, &(VkBufferCopy) {.srcOffset=0, .dstOffset=0, .size=sizeof(vec4)*4 });
	vkuOneShotCommandBufferEnd(&Context, CopyCommand);

	// Delete staging data
	vkuDestroyBuffer(&Context, &stagingBuffer);

	// Create instance buffer and map it
	vkuCreateHostBuffer(&Context, &Font->InstanceBuffer, sizeof(vec4)*2*8192, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	vkMapMemory(Context.Device, Font->InstanceBuffer.DeviceMemory, 0, VK_WHOLE_SIZE, 0, (void *)&Font->InstanceBufferPtr);

	// Set initial instance data pointer
	Font->Instance=(vec4 *)Font->InstanceBufferPtr;

	// Check if it's still valid
	if(Font->Instance==NULL)
		return false;

	// Set initial character count
	Font->NumChar=0;

	return true;
}

// Returns normalized (base) spacing for each character, to be later scaled by a size
float Font_CharacterBaseWidth(const char ch)
{
	switch(ch)
	{
		case 32:	return 0.25f;	// Space

		case 33:	return 0.165f;	// !
		case 34:	return 0.25f;	// "
		case 35:	return 0.375f;	// #
		case 36:	return 0.375f;	// $
		case 37:	return 0.375f;	// %
		case 38:	return 0.425f;	// &
		case 39:	return 0.165f;	// '
		case 40:	return 0.375f;	// (
		case 41:	return 0.375f;	// )
		case 42:	return 0.375f;	// *
		case 43:	return 0.375f;	// +
		case 44:	return 0.165f;	// ,
		case 45:	return 0.375f;	// -
		case 46:	return 0.165f;	// .
		case 47:	return 0.375f;	// /
			
		case 48:    return 0.375f;	// 0
		case 50:    return 0.375f;	// 2
		case 49:    return 0.375f;	// 1
		case 51:    return 0.375f;	// 3
		case 52:    return 0.375f;	// 4
		case 53:    return 0.375f;	// 5
		case 54:    return 0.375f;	// 6
		case 55:    return 0.375f;	// 7
		case 56:    return 0.375f;	// 8
		case 57:    return 0.375f;	// 9

		case 58:	return 0.19f;	// :
		case 59:	return 0.19f;	// ;
		case 60:	return 0.375f;	// <
		case 61:	return 0.375f;	// =
		case 62:	return 0.375f;	// >
		case 63:	return 0.4f;	// ?
		case 64:	return 0.65f;	// @

		case 65:    return 0.375f;	// A
		case 66:    return 0.375f;	// B
		case 67:    return 0.375f;	// C
		case 68:    return 0.375f;	// D
		case 69:    return 0.375f;	// E
		case 70:    return 0.375f;	// F
		case 71:    return 0.375f;	// G
		case 72:    return 0.375f;	// H
		case 73:    return 0.275f;	// I
		case 74:    return 0.275f;	// J
		case 75:    return 0.375f;	// K
		case 76:    return 0.275f;	// L
		case 77:    return 0.475f;	// M
		case 78:    return 0.375f;	// N
		case 79:    return 0.375f;	// O
		case 80:    return 0.375f;	// P
		case 81:    return 0.375f;	// Q
		case 82:    return 0.375f;	// R
		case 83:    return 0.375f;	// S
		case 84:    return 0.375f;	// T
		case 85:    return 0.375f;	// U
		case 86:    return 0.375f;	// V
		case 87:    return 0.425f;	// W
		case 88:    return 0.425f;	// X
		case 89:    return 0.425f;	// Y
		case 90:    return 0.375f;	// Z

		case 91:	return 0.325f;	// [
		case 92:	return 0.375f;	// '\'
		case 93:	return 0.325f;	// ]
		case 94:	return 0.325f;	// ^
		case 95:	return 0.375f;	// _
		case 96:	return 0.165f;	// `

		case 97:    return 0.375f;	// a
		case 98:    return 0.375f;	// b
		case 99:    return 0.375f;	// c
		case 100:   return 0.375f;	// d
		case 101:   return 0.375f;	// e
		case 102:   return 0.325f;	// f
		case 103:   return 0.375f;	// g
		case 104:   return 0.375f;	// h
		case 105:   return 0.19f;	// i
		case 106:   return 0.19f;	// j
		case 107:   return 0.375f;	// k
		case 108:   return 0.19f;	// l
		case 109:   return 0.475f;	// m
		case 110:   return 0.375f;	// n
		case 111:   return 0.375f;	// o
		case 112:   return 0.375f;	// p
		case 113:   return 0.375f;	// q
		case 114:   return 0.375f;	// r
		case 115:   return 0.375f;	// s
		case 116:   return 0.375f;	// t
		case 117:   return 0.375f;	// u
		case 118:   return 0.375f;	// v
		case 119:   return 0.425f;	// w
		case 120:   return 0.425f;	// x
		case 121:   return 0.425f;	// y
		case 122:   return 0.375f;	// z

		case 123:	return 0.375f;	// {
		case 124:	return 0.165f;	// |
		case 125:	return 0.375f;	// }
		case 126:	return 0.375f;	// ~

		default:	return 0.0f;
	};
}

// Sums up base character widths in a string to give a total that can be scaled by whatever text size
float Font_StringBaseWidth(const char *string)
{
	float total=0.0f;

	while(*string!='\0')
		total+=Font_CharacterBaseWidth(*string++);

	return total;
}

// Accumulates text to render
void Font_Print(Font_t *Font, float size, float x, float y, char *string, ...)
{
	// Pointer and buffer for formatted text
	char *ptr, text[255];
	// Variable arguments list
	va_list	ap;
	// Save starting x position
	float sx=x;
	// Current text color
	float r=1.0f, g=1.0f, b=1.0f;

	// Check if the string is even valid first
	if(string==NULL)
		return;

	// Format string, including variable arguments
	va_start(ap, string);
	vsprintf(text, string, ap);
	va_end(ap);

	// Add in how many characters were need to deal with
	Font->NumChar+=(uint32_t)strlen(text);

	// Loop through and pre-offset 'y' by any CRs in the string, so nothing goes off screen
	for(ptr=text;*ptr!='\0';ptr++)
	{
		if(*ptr=='\n')
			y+=size;
	}

	// Loop through the text string until EOL
	for(ptr=text;*ptr!='\0';ptr++)
	{
		// Decrement 'y' for any CR's
		if(*ptr=='\n')
		{
			x=sx;
			y-=size;
			Font->NumChar--;
			continue;
		}

		// Just advance spaces instead of rendering empty quads
		if(*ptr==' ')
		{
			x+=Font_CharacterBaseWidth(*ptr)*size;
			Font->NumChar--;
			continue;
		}

		// ANSI color escape codes
		// I'm sure there's a better way to do this!
		// But it works, so whatever.
		if(*ptr=='\x1B')
		{
			ptr++;
			     if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='0'&&*(ptr+3)=='m')	{ r=0.0f; g=0.0f; b=0.0f; } // BLACK
			else if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='1'&&*(ptr+3)=='m')	{ r=0.5f; g=0.0f; b=0.0f; } // DARK RED
			else if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='2'&&*(ptr+3)=='m')	{ r=0.0f; g=0.5f; b=0.0f; } // DARK GREEN
			else if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='3'&&*(ptr+3)=='m')	{ r=0.5f; g=0.5f; b=0.0f; } // DARK YELLOW
			else if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='4'&&*(ptr+3)=='m')	{ r=0.0f; g=0.0f; b=0.5f; } // DARK BLUE
			else if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='5'&&*(ptr+3)=='m')	{ r=0.5f; g=0.0f; b=0.5f; } // DARK MAGENTA
			else if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='6'&&*(ptr+3)=='m')	{ r=0.0f; g=0.5f; b=0.5f; } // DARK CYAN
			else if(*(ptr+0)=='['&&*(ptr+1)=='3'&&*(ptr+2)=='7'&&*(ptr+3)=='m')	{ r=0.5f; g=0.5f; b=0.5f; } // GREY
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='0'&&*(ptr+3)=='m')	{ r=0.5f; g=0.5f; b=0.5f; } // GREY
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='1'&&*(ptr+3)=='m')	{ r=1.0f; g=0.0f; b=0.0f; } // RED
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='2'&&*(ptr+3)=='m')	{ r=0.0f; g=1.0f; b=0.0f; } // GREEN
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='3'&&*(ptr+3)=='m')	{ r=1.0f; g=1.0f; b=0.0f; } // YELLOW
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='4'&&*(ptr+3)=='m')	{ r=0.0f; g=0.0f; b=1.0f; } // BLUE
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='5'&&*(ptr+3)=='m')	{ r=1.0f; g=0.0f; b=1.0f; } // MAGENTA
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='6'&&*(ptr+3)=='m')	{ r=0.0f; g=1.0f; b=1.0f; } // CYAN
			else if(*(ptr+0)=='['&&*(ptr+1)=='9'&&*(ptr+2)=='7'&&*(ptr+3)=='m')	{ r=1.0f; g=1.0f; b=1.0f; } // WHITE
			ptr+=4;
			Font->NumChar-=5;
		}

		// Advance one character
		x+=Font_CharacterBaseWidth(*ptr)*size;

		// Emit position, atlas offset, and color for this character
		*Font->Instance++=Vec4(x-((Font_CharacterBaseWidth(*ptr)*0.5f)*size), y, (float)(*ptr), size);	// Instance position, character to render, size
		*Font->Instance++=Vec4(r, g, b, 1.0f);															// Instance color
	}
	// ---
}

// Submits text draw data to GPU and resets for next frame
void Font_Draw(Font_t *Font, uint32_t Index)
{
	// Bind the font rendering pipeline (sets states and shaders)
	vkCmdBindPipeline(PerFrame[Index].CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Font->Pipeline.Pipeline);

	vkCmdPushConstants(PerFrame[Index].CommandBuffer, Font->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkExtent2D), &Swapchain.Extent);

	// Bind vertex data buffer
	vkCmdBindVertexBuffers(PerFrame[Index].CommandBuffer, 0, 1, &Font->VertexBuffer.Buffer, &(VkDeviceSize) { 0 });
	// Bind object instance buffer
	vkCmdBindVertexBuffers(PerFrame[Index].CommandBuffer, 1, 1, &Font->InstanceBuffer.Buffer, &(VkDeviceSize) { 0 });

	// Draw the number of characters
	vkCmdDraw(PerFrame[Index].CommandBuffer, 4, Font->NumChar, 0, 0);

	// Reset instance data pointer and character count
	Font->Instance=Font->InstanceBufferPtr;
	Font->NumChar=0;
}

void Font_Destroy(Font_t *Font)
{
	// Instance buffer handles
	if(Font->InstanceBuffer.DeviceMemory)
		vkUnmapMemory(Context.Device, Font->InstanceBuffer.DeviceMemory);

	vkuDestroyBuffer(&Context, &Font->InstanceBuffer);

	// Vertex data handles
	vkuDestroyBuffer(&Context, &Font->VertexBuffer);

	// Pipeline
	vkDestroyPipelineLayout(Context.Device, Font->PipelineLayout, VK_NULL_HANDLE);
	vkDestroyPipeline(Context.Device, Font->Pipeline.Pipeline, VK_NULL_HANDLE);

	// Descriptors
	vkDestroyDescriptorSetLayout(Context.Device, Font->DescriptorSet.DescriptorSetLayout, VK_NULL_HANDLE);
}
