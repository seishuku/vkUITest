/*
	Copyright 2020 Matt Williams/NitroGL
	Image texture loading for Vulkan, based on my OpenGL code.

	TODO:
		manual mipmaps?
		more comments?
*/

#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../system/system.h"
#include "../vulkan/vulkan.h"
#include "../math/math.h"
#include "image.h"

// _MakeNormalMap, internal function used by Image_Upload.
// Make a normal map from a greyscale height map (or the first channel of an RGB/RGBA image).
// 
// This is done by applying a Sobel kernel filter in the X and Y directions, Z=1.0, then normalizing that.
static bool _MakeNormalMap(VkuImage_t *Image)
{
	const uint32_t Channels=Image->Depth>>3;
	const float OneOver255=1.0f/255.0f;
	const float KernelX[9]=
	{
		 1.0f,  0.0f, -1.0f,
		 2.0f,  0.0f, -2.0f,
		 1.0f,  0.0f, -1.0f
	};
	const float KernelY[9]=
	{
		-1.0f, -2.0f, -1.0f,
		 0.0f,  0.0f,  0.0f,
		 1.0f,  2.0f,  1.0f
	};

	// Check if input image is a valid bit depth.
	if(Image->Depth!=32&&Image->Depth!=8)
		return false;

	// Allocate memory for output data.
	uint16_t *Buffer=(uint16_t *)Zone_Malloc(Zone, sizeof(uint16_t)*Image->Width*Image->Height*4);

	// Check that memory allocated.
	if(Buffer==NULL)
		return false;

	// Loop over all pixels of input image.
	for(uint32_t y=0;y<Image->Height;y++)
	{
		for(uint32_t x=0;x<Image->Width;x++)
		{
			vec3 n={ 0.0f, 0.0f, 1.0f };

			// Loop over the kernel(s) to convolve the image.
			for(uint32_t yy=0;yy<3;yy++)
			{
				const int oy=min(Image->Height-1, y+yy);

				for(uint32_t xx=0;xx<3;xx++)
				{
					const int ox=min(Image->Width-1, x+xx);
					const float Pixel=(float)Image->Data[Channels*(oy*Image->Width+ox)]*OneOver255;

					n.x+=KernelX[yy*3+xx]*Pixel;
					n.y+=KernelY[yy*3+xx]*Pixel;
				}
			}

			// Resulting convolution is the "normal" after normalizing.
			Vec3_Normalize(&n);

			// Precalculate destination image index
			const uint32_t Index=4*(y*Image->Width+x);

			// Set destination image RGB to the normal value,
			//   scaled and biased to fit the 16bit/channel unsigned integer image format
			Buffer[Index+0]=(uint16_t)(65535.0f*(0.5f*n.x+0.5f));
			Buffer[Index+1]=(uint16_t)(65535.0f*(0.5f*n.y+0.5f));
			Buffer[Index+2]=(uint16_t)(65535.0f*(0.5f*n.z+0.5f));

			// If the input image was an RGBA image, copy over the alpha channel from that, otherwise alpha=1.0.
			if(Channels==4)
				Buffer[Index+3]=(uint16_t)(Image->Data[Index+3]<<8);
			else
				Buffer[Index+3]=65535;
		}
	}

	// Reset input color depth to the new format
	Image->Depth=64;

	// Free input image data and set it's pointer to the new data.
	Zone_Free(Zone, Image->Data);
	Image->Data=(unsigned char *)Buffer;

	return true;
}

// _Normalize, internal function used by Image_Upload.
// Takes an 8bit/channel RGB(A) image and re-normalizes it into a 16bit/channel image.
static bool _Normalize(VkuImage_t *Image)
{
	uint32_t Channels=Image->Depth>>3;
	const float OneOver255=1.0f/255.0f;

	if(Image->Depth!=32)
		return false;

	uint16_t *Buffer=(uint16_t *)Zone_Malloc(Zone, sizeof(uint16_t)*Image->Width*Image->Height*4);

	if(Buffer==NULL)
		return false;

	for(uint32_t i=0;i<Image->Width*Image->Height;i++)
	{
		vec3 n;

		// scale/bias BGR unorm -> Normal float
		n.x=2.0f*((float)Image->Data[Channels*i+2]*OneOver255)-1.0f;
		n.y=2.0f*((float)Image->Data[Channels*i+1]*OneOver255)-1.0f;
		n.z=2.0f*((float)Image->Data[Channels*i+0]*OneOver255)-1.0f;

		// Normalize it
		Vec3_Normalize(&n);

		// scale/bias normal back into a 16bit/channel unorm image
		Buffer[4*i+0]=(uint16_t)(65535.0f*(0.5f*n.x+0.5f));
		Buffer[4*i+1]=(uint16_t)(65535.0f*(0.5f*n.y+0.5f));
		Buffer[4*i+2]=(uint16_t)(65535.0f*(0.5f*n.z+0.5f));

		// Pass along alpha channel if original image was RGBA
		if(Channels==4)
			Buffer[4*i+3]=(uint16_t)(Image->Data[4*i+3]<<8);
		else
			Buffer[4*i+3]=65535;
	}

	// Reset new color depth
	Image->Depth=64;

	// Free original image and reset the buffer pointer
	Zone_Free(Zone, Image->Data);
	Image->Data=(uint8_t *)Buffer;

	return true;
}

// _RGBE2Float, internal function used by Image_Upload
// Takes in an 8bit/channel RGBE encoded image and outputs a
//   32bit float/channel RGB image, mainly used for HDR images.
static bool _RGBE2Float(VkuImage_t *Image)
{
	float *Buffer=(float *)Zone_Malloc(Zone, sizeof(float)*Image->Width*Image->Height*4);

	if(Buffer==NULL)
		return false;

	for(uint32_t i=0;i<Image->Width*Image->Height;i++)
	{
		unsigned char *rgbe=&Image->Data[4*i];
		float *rgb=&Buffer[4*i];

		// This is basically rgb*exponent to reconstruct floating point pixels from 8bit/channel image.
		if(rgbe[3])
		{
			float f=1.0f;
			// Exponent is stored in the alpha channel
			int8_t e=rgbe[3]-(128+8);

			// f=exp2(e)
			if(e>0)
			{
				for(uint8_t i=0;i<e;i++)
					f*=2.0f;
			}
			else
			{
				for(uint8_t i=0;i<-e;i++)
					f/=2.0f;
			}

			// Bias and multiple
			rgb[0]=((float)rgbe[0]+0.5f)*f;
			rgb[1]=((float)rgbe[1]+0.5f)*f;
			rgb[2]=((float)rgbe[2]+0.5f)*f;

			// RGBE images can't have an alpha channel, but we're outputting in RGBA anyway.
			rgb[3]=1.0f;
		}
		else
			rgb[0]=rgb[1]=rgb[2]=rgb[3]=0.0f;
	}

	// Reset color depth to new format
	Image->Depth=128;

	// Free and reset data pointer
	Zone_Free(Zone, Image->Data);
	Image->Data=(uint8_t *)Buffer;

	return true;
}

// _Resize, internal function not currently used.
// Does a bilinear resize of an image, destination struct width/height set image size.
//
// TODO: Source/destination color depths *could* be different,
//         maybe make this do a format conversion as well?
static bool _Resize(VkuImage_t *Src, VkuImage_t *Dst)
{
	float fx, fy, hx, hy, lx, ly;
	float xPercent, yPercent;
	float Total[4], Sum;
	uint32_t iy, ix;

	// Destination format must match source format.
	if(Dst->Depth!=Src->Depth)
		return false;

	Dst->Data=(uint8_t *)Zone_Malloc(Zone, (size_t)Dst->Width*Dst->Height*(Dst->Depth>>3));

	if(Dst->Data==NULL)
		return false;

	float sx=(float)Src->Width/Dst->Width;
	float sy=(float)Src->Height/Dst->Height;

	for(uint32_t y=0;y<Dst->Height;y++)
	{
		if(Src->Height>Dst->Height)
		{
			fy=((float)y+0.5f)*sy;
			hy=fy+(sy*0.5f);
			ly=fy-(sy*0.5f);
		}
		else
		{
			fy=(float)y*sy;
			hy=fy+0.5f;
			ly=fy-0.5f;
		}

		for(uint32_t x=0;x<Dst->Width;x++)
		{
			if(Src->Width>Dst->Width)
			{
				fx=((float)x+0.5f)*sx;
				hx=fx+(sx*0.5f);
				lx=fx-(sx*0.5f);
			}
			else
			{
				fx=(float)x*sx;
				hx=fx+0.5f;
				lx=fx-0.5f;
			}

			Total[0]=Total[1]=Total[2]=Total[3]=Sum=0.0f;

			fy=ly;
			iy=(int)fy;

			while(fy<hy)
			{
				if(hy<iy+1)
					yPercent=hy-fy;
				else
					yPercent=(iy+1)-fy;

				fx=lx;
				ix=(int)fx;

				while(fx<hx)
				{
					if(hx<ix+1)
						xPercent=hx-fx;
					else
						xPercent=(ix+1)-fx;

					float Percent=xPercent*yPercent;
					Sum+=Percent;

					uint32_t Index=min(Src->Height-1, iy)*Src->Width+min(Src->Width-1, ix);

					switch(Src->Depth)
					{
						case 128:
							Total[0]+=((float *)Src->Data)[4*Index+0]*Percent;
							Total[1]+=((float *)Src->Data)[4*Index+1]*Percent;
							Total[2]+=((float *)Src->Data)[4*Index+2]*Percent;
							Total[3]+=((float *)Src->Data)[4*Index+3]*Percent;
							break;

						case 64:
							Total[0]+=((uint16_t *)Src->Data)[4*Index+0]*Percent;
							Total[1]+=((uint16_t *)Src->Data)[4*Index+1]*Percent;
							Total[2]+=((uint16_t *)Src->Data)[4*Index+2]*Percent;
							Total[3]+=((uint16_t *)Src->Data)[4*Index+3]*Percent;
							break;

						case 32:
							Total[0]+=(float)Src->Data[4*Index+0]*Percent;
							Total[1]+=(float)Src->Data[4*Index+1]*Percent;
							Total[2]+=(float)Src->Data[4*Index+2]*Percent;
							Total[3]+=(float)Src->Data[4*Index+3]*Percent;
							break;

						case 16:
							Total[0]+=((((uint16_t *)Src->Data)[Index]>>0x0)&0x1F)*Percent;
							Total[1]+=((((uint16_t *)Src->Data)[Index]>>0x5)&0x1F)*Percent;
							Total[2]+=((((uint16_t *)Src->Data)[Index]>>0xA)&0x1F)*Percent;
							break;

						case 8:
							Total[0]+=Src->Data[Index]*Percent;
					}

					ix++;
					fx=(float)ix;
				}

				iy++;
				fy=(float)iy;
			}

			uint32_t Index=y*Dst->Width+x;
			Sum=1.0f/Sum;

			switch(Dst->Depth)
			{
				case 128:
					((float *)Dst->Data)[4*Index+0]=(float)(Total[0]*Sum);
					((float *)Dst->Data)[4*Index+1]=(float)(Total[1]*Sum);
					((float *)Dst->Data)[4*Index+2]=(float)(Total[2]*Sum);
					((float *)Dst->Data)[4*Index+3]=(float)(Total[3]*Sum);
					break;

				case 64:
					((uint16_t *)Dst->Data)[4*Index+0]=(uint16_t)(Total[0]*Sum);
					((uint16_t *)Dst->Data)[4*Index+1]=(uint16_t)(Total[1]*Sum);
					((uint16_t *)Dst->Data)[4*Index+2]=(uint16_t)(Total[2]*Sum);
					((uint16_t *)Dst->Data)[4*Index+3]=(uint16_t)(Total[3]*Sum);
					break;

				case 32:
					((uint8_t *)Dst->Data)[4*Index+0]=(uint8_t)(Total[0]*Sum);
					((uint8_t *)Dst->Data)[4*Index+1]=(uint8_t)(Total[1]*Sum);
					((uint8_t *)Dst->Data)[4*Index+2]=(uint8_t)(Total[2]*Sum);
					((uint8_t *)Dst->Data)[4*Index+3]=(uint8_t)(Total[3]*Sum);
					break;

				case 16:
					((uint16_t *)Dst->Data)[Index]=((uint16_t)((Total[0]*Sum))&0x1F)<<0x0|((uint16_t)(Total[1]*Sum)&0x1F)<<0x5|((uint16_t)(Total[2]*Sum)&0x1F)<<0xA;
					break;

				case 8:
					((uint8_t *)Dst->Data)[Index]=(uint8_t)(Total[0]*Sum);
					break;
			}
		}
	}

	return true;
}

// _HalfImage, internal function not currently used.
// Simple 4->1 pixel average downscale to only half the image size.
static bool _HalfImage(VkuImage_t *Src, VkuImage_t *Dst)
{
	Dst->Width=Src->Width>>1;
	Dst->Height=Src->Height>>1;
	Dst->Depth=Src->Depth;
	Dst->Data=(uint8_t *)Zone_Malloc(Zone, (size_t)Dst->Width*Dst->Height*(Dst->Depth>>3));

	if(Dst->Data==NULL)
		return false;

	for(uint32_t y=0;y<Dst->Height;y++)
	{
		uint32_t sy=y<<1;

		for(uint32_t x=0;x<Dst->Width;x++)
		{
			uint32_t sx=x<<1;

			uint32_t indexDst=y*Dst->Width+x;
			uint32_t indexSrc00=(sy+0)*Src->Width+(sx+0);
			uint32_t indexSrc10=(sy+0)*Src->Width+(sx+1);
			uint32_t indexSrc01=(sy+1)*Src->Width+(sx+0);
			uint32_t indexSrc11=(sy+1)*Src->Width+(sx+1);

			switch(Src->Depth)
			{
				case 128:
					((float *)Dst->Data)[4*indexDst+0]=(((float *)Src->Data)[4*indexSrc00+0]+
														((float *)Src->Data)[4*indexSrc10+0]+
														((float *)Src->Data)[4*indexSrc01+0]+
														((float *)Src->Data)[4*indexSrc11+0])*0.25f;
					((float *)Dst->Data)[4*indexDst+1]=(((float *)Src->Data)[4*indexSrc00+1]+
														((float *)Src->Data)[4*indexSrc10+1]+
														((float *)Src->Data)[4*indexSrc01+1]+
														((float *)Src->Data)[4*indexSrc11+1])*0.25f;
					((float *)Dst->Data)[4*indexDst+2]=(((float *)Src->Data)[4*indexSrc00+2]+
														((float *)Src->Data)[4*indexSrc10+2]+
														((float *)Src->Data)[4*indexSrc01+2]+
														((float *)Src->Data)[4*indexSrc11+2])*0.25f;
					((float *)Dst->Data)[4*indexDst+3]=(((float *)Src->Data)[4*indexSrc00+3]+
														((float *)Src->Data)[4*indexSrc10+3]+
														((float *)Src->Data)[4*indexSrc01+3]+
														((float *)Src->Data)[4*indexSrc11+3])*0.25f;
					break;

				case 64:
					((uint16_t *)Dst->Data)[4*indexDst+0]=(((uint16_t *)Src->Data)[4*indexSrc00+0]+
														   ((uint16_t *)Src->Data)[4*indexSrc10+0]+
														   ((uint16_t *)Src->Data)[4*indexSrc01+0]+
														   ((uint16_t *)Src->Data)[4*indexSrc11+0])>>2;
					((uint16_t *)Dst->Data)[4*indexDst+1]=(((uint16_t *)Src->Data)[4*indexSrc00+1]+
														   ((uint16_t *)Src->Data)[4*indexSrc10+1]+
														   ((uint16_t *)Src->Data)[4*indexSrc01+1]+
														   ((uint16_t *)Src->Data)[4*indexSrc11+1])>>2;
					((uint16_t *)Dst->Data)[4*indexDst+2]=(((uint16_t *)Src->Data)[4*indexSrc00+2]+
														   ((uint16_t *)Src->Data)[4*indexSrc10+2]+
														   ((uint16_t *)Src->Data)[4*indexSrc01+2]+
														   ((uint16_t *)Src->Data)[4*indexSrc11+2])>>2;
					((uint16_t *)Dst->Data)[4*indexDst+3]=(((uint16_t *)Src->Data)[4*indexSrc00+3]+
														   ((uint16_t *)Src->Data)[4*indexSrc10+3]+
														   ((uint16_t *)Src->Data)[4*indexSrc01+3]+
														   ((uint16_t *)Src->Data)[4*indexSrc11+3])>>2;
					break;

				case 32:
					Dst->Data[4*indexDst+0]=(Src->Data[4*indexSrc00+0]+
											 Src->Data[4*indexSrc10+0]+
											 Src->Data[4*indexSrc01+0]+
											 Src->Data[4*indexSrc11+0])>>2;
					Dst->Data[4*indexDst+1]=(Src->Data[4*indexSrc00+1]+
											 Src->Data[4*indexSrc10+1]+
											 Src->Data[4*indexSrc01+1]+
											 Src->Data[4*indexSrc11+1])>>2;
					Dst->Data[4*indexDst+2]=(Src->Data[4*indexSrc00+2]+
											 Src->Data[4*indexSrc10+2]+
											 Src->Data[4*indexSrc01+2]+
											 Src->Data[4*indexSrc11+2])>>2;
					Dst->Data[4*indexDst+3]=(Src->Data[4*indexSrc00+3]+
											 Src->Data[4*indexSrc10+3]+
											 Src->Data[4*indexSrc01+3]+
											 Src->Data[4*indexSrc11+3])>>2;
					break;

				case 16:
				{
					uint16_t p0=((uint16_t *)Src->Data)[indexSrc00];
					uint16_t p1=((uint16_t *)Src->Data)[indexSrc10];
					uint16_t p2=((uint16_t *)Src->Data)[indexSrc01];
					uint16_t p3=((uint16_t *)Src->Data)[indexSrc11];

					((uint16_t *)Dst->Data)[indexDst] =(uint16_t)((((p0>>0x0)&0x1F)+((p1>>0x0)&0x1F)+((p2>>0x0)&0x1F)+((p3>>0x0)&0x1F))>>2)<<0x0;
					((uint16_t *)Dst->Data)[indexDst]|=(uint16_t)((((p0>>0x5)&0x1F)+((p1>>0x5)&0x1F)+((p2>>0x5)&0x1F)+((p3>>0x5)&0x1F))>>2)<<0x5;
					((uint16_t *)Dst->Data)[indexDst]|=(uint16_t)((((p0>>0xA)&0x1F)+((p1>>0xA)&0x1F)+((p2>>0xA)&0x1F)+((p3>>0xA)&0x1F))>>2)<<0xA;
					break;
				}

				case 8:
					Dst->Data[indexDst]=(Src->Data[indexSrc00]+
										 Src->Data[indexSrc10]+
										 Src->Data[indexSrc01]+
										 Src->Data[indexSrc11])>>2;
					break;
			}
		}
	}

	return true;
}

// _BuildMipmaps, internal function used by Image_Upload.
// Builds a Vulkan mipmap chain by using a CPU scaler to resize the images.
// 
// TODO: This doesn't obviously work, the data must persist until the command buffer is complete and submitted
// 
//static void _BuildMipmaps(VkDevice Device, VkCommandBuffer CommandBuffer, VkuBuffer_t *StagingBuffer, VkuImage_t *Image, uint32_t mipLevels, uint32_t layerCount, uint32_t baseLayer)
//{
//	VkuImage_t Stage, Temp;
//
//	// Create a temporary image
//	Temp.Width=Image->Width;
//	Temp.Height=Image->Height;
//	Temp.Depth=Image->Depth;
//
//	// Create and map memory for the already existing staging buffer (this contains the original image)
//	Stage.Width=Image->Width;
//	Stage.Height=Image->Height;
//	Stage.Depth=Image->Depth;
//	vkMapMemory(Device, StagingBuffer->DeviceMemory, 0, VK_WHOLE_SIZE, 0, &Stage.Data);
//
//	for(uint32_t i=1;i<mipLevels;i++)
//	{
//		// Half the image from the staging buffer image into the temporary image
//		_HalfImage(&Stage, &Temp);
//
//		// Copy the halved image from the temporary image into the staging buffer
//		memcpy(Stage.Data, Temp.Data, Temp.Width*Temp.Height*(Temp.Depth>>3));
//
//		// Copy from staging buffer to the texture buffer
//		vkCmdCopyBufferToImage(CommandBuffer, StagingBuffer->Buffer, Image->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, (VkBufferImageCopy[1])
//		{
//			{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, i, baseLayer, layerCount }, { 0, 0, 0 }, { Temp.Width, Temp.Height, 1 } }
//		});
//
//		// The staging buffer now contains the halved image, so update the width/height for the next round
//		Stage.Width=Temp.Width;
//		Stage.Height=Temp.Height;
//
//		// Free the temp data allocated by _HalfImage
//		Zone_Free(Zone, Temp.Data);
//	}
//
//	// Unmap the staging buffer
//	vkUnmapMemory(Device, StagingBuffer->DeviceMemory);
//
//	// Final image layout transition to shader read-only
//	vkuTransitionLayout(CommandBuffer, Image->Image, 1, mipLevels-1, 1, baseLayer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
//}

// _GetPixelBilinear, internal function used by functions that need to sample pixels out of image data.
// Essentially does what the GPU does when sampling textures, xy coords are unnormalized (0 to image size).
//
// Output must be sized correctly for sampled image bit depth and format.
// TODO: Source/destination color depths *could* be different,
//         maybe make this do a format conversion as well?
static void _GetPixelBilinear(VkuImage_t *Image, float x, float y, void *Out)
{
	uint32_t ix=(int)x, iy=(int)y;
	uint32_t ox=ix+1, oy=iy+1;
	float fx=x-ix, fy=y-iy;

	if(ox>=Image->Width)
		ox=Image->Width-1;

	if(oy>=Image->Height)
		oy=Image->Height-1;

	if(fx<0.0f)
		ix=ox=0;

	if(fy<0.0f)
		iy=oy=0;

	float w11=fx*fy;
	float w00=1.0f-fx-fy+w11;
	float w10=fx-w11;
	float w01=fy-w11;

	uint32_t i00=iy*Image->Width+ix;
	uint32_t i10=iy*Image->Width+ox;
	uint32_t i01=oy*Image->Width+ix;
	uint32_t i11=oy*Image->Width+ox;

	switch(Image->Depth)
	{
		case 128:
			((float *)Out)[0]=((float *)Image->Data)[4*i00+0]*w00+((float *)Image->Data)[4*i10+0]*w10+((float *)Image->Data)[4*i01+0]*w01+((float *)Image->Data)[4*i11+0]*w11;
			((float *)Out)[1]=((float *)Image->Data)[4*i00+1]*w00+((float *)Image->Data)[4*i10+1]*w10+((float *)Image->Data)[4*i01+1]*w01+((float *)Image->Data)[4*i11+1]*w11;
			((float *)Out)[2]=((float *)Image->Data)[4*i00+2]*w00+((float *)Image->Data)[4*i10+2]*w10+((float *)Image->Data)[4*i01+2]*w01+((float *)Image->Data)[4*i11+2]*w11;
			((float *)Out)[3]=((float *)Image->Data)[4*i00+3]*w00+((float *)Image->Data)[4*i10+3]*w10+((float *)Image->Data)[4*i01+3]*w01+((float *)Image->Data)[4*i11+3]*w11;
			break;

		case 64:
			((uint16_t *)Out)[0]=(uint16_t)(((uint16_t *)Image->Data)[4*i00+0]*w00+((uint16_t *)Image->Data)[4*i10+0]*w10+((uint16_t *)Image->Data)[4*i01+0]*w01+((uint16_t *)Image->Data)[4*i11+0]*w11);
			((uint16_t *)Out)[1]=(uint16_t)(((uint16_t *)Image->Data)[4*i00+1]*w00+((uint16_t *)Image->Data)[4*i10+1]*w10+((uint16_t *)Image->Data)[4*i01+1]*w01+((uint16_t *)Image->Data)[4*i11+1]*w11);
			((uint16_t *)Out)[2]=(uint16_t)(((uint16_t *)Image->Data)[4*i00+2]*w00+((uint16_t *)Image->Data)[4*i10+2]*w10+((uint16_t *)Image->Data)[4*i01+2]*w01+((uint16_t *)Image->Data)[4*i11+2]*w11);
			((uint16_t *)Out)[3]=(uint16_t)(((uint16_t *)Image->Data)[4*i00+3]*w00+((uint16_t *)Image->Data)[4*i10+3]*w10+((uint16_t *)Image->Data)[4*i01+3]*w01+((uint16_t *)Image->Data)[4*i11+3]*w11);
			break;

		case 32:
			((uint8_t *)Out)[0]=(uint8_t)(Image->Data[4*i00+0]*w00+Image->Data[4*i10+0]*w10+Image->Data[4*i01+0]*w01+Image->Data[4*i11+0]*w11);
			((uint8_t *)Out)[1]=(uint8_t)(Image->Data[4*i00+1]*w00+Image->Data[4*i10+1]*w10+Image->Data[4*i01+1]*w01+Image->Data[4*i11+1]*w11);
			((uint8_t *)Out)[2]=(uint8_t)(Image->Data[4*i00+2]*w00+Image->Data[4*i10+2]*w10+Image->Data[4*i01+2]*w01+Image->Data[4*i11+2]*w11);
			((uint8_t *)Out)[3]=(uint8_t)(Image->Data[4*i00+3]*w00+Image->Data[4*i10+3]*w10+Image->Data[4*i01+3]*w01+Image->Data[4*i11+3]*w11);
			break;

		case 16:
		{
			uint16_t p0=((uint16_t *)Image->Data)[i00];
			uint16_t p1=((uint16_t *)Image->Data)[i10];
			uint16_t p2=((uint16_t *)Image->Data)[i01];
			uint16_t p3=((uint16_t *)Image->Data)[i11];

			*((uint16_t *)Out) =(uint16_t)(((p0>>0x0)&0x1F)*w00+((p1>>0x0)&0x1F)*w10+((p2>>0x0)&0x1F)*w01+((p3>>0x0)&0x1F)*w11)<<0x0;
			*((uint16_t *)Out)|=(uint16_t)(((p0>>0x5)&0x1F)*w00+((p1>>0x5)&0x1F)*w10+((p2>>0x5)&0x1F)*w01+((p3>>0x5)&0x1F)*w11)<<0x5;
			*((uint16_t *)Out)|=(uint16_t)(((p0>>0xA)&0x1F)*w00+((p1>>0xA)&0x1F)*w10+((p2>>0xA)&0x1F)*w01+((p3>>0xA)&0x1F)*w11)<<0xA;
			break;
		}

		case 8:
			*((uint8_t *)Out)=(uint8_t)(Image->Data[i00]*w00+Image->Data[i10]*w10+Image->Data[i01]*w01+Image->Data[i11]*w11);
			break;
	}
}

// _GetUVAngularMap, internal function used by _AngularMapFace.
// Gets angular map lightprobe UV coorinate from a 3D coordinate.
static vec2 _GetUVAngularMap(vec3 xyz)
{
	float phi=-(float)acos(xyz.z), theta=(float)atan2(xyz.y, xyz.x);

	return Vec2(
		0.5f*((phi/PI)*(float)cos(theta))+0.5f,
		0.5f*((phi/PI)*(float)sin(theta))+0.5f);
}

// _GetXYZFace, internal function used by _AngularMapFace.
// Gets 3D coordinate from a 2D UV coorinate and face selection.
static vec3 _GetXYZFace(vec2 uv, int face)
{
	vec3 xyz;

	switch(face)
	{
		case 0: xyz=Vec3(1.0f, (uv.y-0.5f)*2.0f, (0.5f-uv.x)*2.0f);		break; // +X
		case 1: xyz=Vec3(-1.0f, (uv.y-0.5f)*2.0f, (uv.x-0.5f)*2.0f);	break; // -X
		case 2: xyz=Vec3((uv.x-0.5f)*2.0f, -1.0f, (uv.y-0.5f)*2.0f);	break; // +Y
		case 3: xyz=Vec3((uv.x-0.5f)*2.0f, 1.0f, (0.5f-uv.y)*2.0f);		break; // -Y
		case 4: xyz=Vec3((uv.x-0.5f)*2.0f, (uv.y-0.5f)*2.0f, 1.0f);		break; // +Z
		case 5: xyz=Vec3((0.5f-uv.x)*2.0f, (uv.y-0.5f)*2.0f, -1.0f);	break; // -Z
	}

	Vec3_Normalize(&xyz);

	return xyz;
}

// _AngularMapFace, internal function used by Image_Upload.
// Gets a selected 2D cubemap face from an angular lightmap probe image
static bool _AngularMapFace(VkuImage_t *In, int Face, VkuImage_t *Out)
{
	// Allocate memory for output
	Out->Data=(uint8_t *)Zone_Malloc(Zone, (size_t)Out->Width*Out->Height*(Out->Depth>>3));

	if(Out->Data==NULL)
		return false;

	for(uint32_t y=0;y<Out->Height;y++)
	{
		float fy=(float)y/(Out->Height-1);

		for(uint32_t x=0;x<Out->Width;x++)
		{
			float fx=(float)x/(Out->Width-1);
			vec3 xyz;

			// UV coordinate into the image we're producing
			vec2 uv={ fx, fy };

			// Get a 3D cubemap coordinate for the given UV and selected cube face
			xyz=_GetXYZFace(uv, Face);

			// Use that 3D coordinate and this function to transform it into a UV coord into an angular map lightprobe
			// TODO: This function can also be substituted for other image formats (lat/lon,  vertical cross, mirror ball),
			//		   maybe add a flag for different layouts?
			uv=_GetUVAngularMap(xyz);

			// Bilinearly sample the pixel from the source image and set it into the output data (destination cubemap face)
			_GetPixelBilinear(In, uv.x*In->Width, uv.y*In->Height, (void *)&Out->Data[(Out->Depth>>3)*(y*Out->Width+x)]);
		}
	}

	return true;
}

// _RGBtoRGBA, internal function used by Image_Upload.
// Converts any RGB format into an RGBA image.
//
// This is needed for Vulkan, most GPUs do not have RGB image format support.
// Also allows any helper functions here to be simplified by not needing to support as many bit depths.
static void _RGBtoRGBA(VkuImage_t *Image)
{
	if(Image->Depth==96)
	{
		float *Dst=(float *)Zone_Malloc(Zone, sizeof(float)*Image->Width*Image->Height*4);

		if(Dst==NULL)
			return;

		for(uint32_t i=0;i<Image->Width*Image->Height;i++)
		{
			uint32_t SrcIdx=3*i;
			uint32_t DstIdx=4*i;

			Dst[DstIdx+0]=((float *)Image->Data)[SrcIdx+0];
			Dst[DstIdx+1]=((float *)Image->Data)[SrcIdx+1];
			Dst[DstIdx+2]=((float *)Image->Data)[SrcIdx+2];
			Dst[DstIdx+3]=1.0f;
		}

		Zone_Free(Zone, Image->Data);
		Image->Data=(uint8_t *)Dst;
		Image->Depth=128;
	}
	else if(Image->Depth==48)
	{
		uint16_t *Dst=(uint16_t *)Zone_Malloc(Zone, sizeof(uint16_t)*Image->Width*Image->Height*4);

		if(Dst==NULL)
			return;

		for(uint32_t i=0;i<Image->Width*Image->Height;i++)
		{
			uint32_t SrcIdx=3*i;
			uint32_t DstIdx=4*i;

			Dst[DstIdx+0]=((uint16_t *)Image->Data)[SrcIdx+0];
			Dst[DstIdx+1]=((uint16_t *)Image->Data)[SrcIdx+1];
			Dst[DstIdx+2]=((uint16_t *)Image->Data)[SrcIdx+2];
			Dst[DstIdx+3]=UINT16_MAX;
		}

		Zone_Free(Zone, Image->Data);
		Image->Data=(uint8_t *)Dst;
		Image->Depth=64;
	}
	else if(Image->Depth==24)
	{
		uint8_t *Dst=(uint8_t *)Zone_Malloc(Zone, sizeof(uint8_t)*Image->Width*Image->Height*4);

		if(Dst==NULL)
			return;

		for(uint32_t i=0;i<Image->Width*Image->Height;i++)
		{
			uint32_t SrcIdx=3*i;
			uint32_t DstIdx=4*i;

			Dst[DstIdx+0]=Image->Data[SrcIdx+0];
			Dst[DstIdx+1]=Image->Data[SrcIdx+1];
			Dst[DstIdx+2]=Image->Data[SrcIdx+2];
			Dst[DstIdx+3]=UINT8_MAX;
		}

		Zone_Free(Zone, Image->Data);
		Image->Data=Dst;
		Image->Depth=32;
	}
}

// _GenerateMipmaps, internal function used by Image_Upload.
// Builds a Vulkan mipmap chain by using the GPU image blitter to resize the images.
static void _GenerateMipmaps(VkCommandBuffer CommandBuffer, VkuImage_t *Image, uint32_t MipLevels, uint32_t LayerCount, uint32_t BaseLayer)
{
	for(uint32_t i=1;i<MipLevels;i++)
	{
		vkuTransitionLayout(CommandBuffer, Image->Image, 1, i-1, 1, BaseLayer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		vkCmdBlitImage(CommandBuffer, Image->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Image->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &(VkImageBlit)
		{
			.srcOffsets[0]={ 0, 0, 0 },
			.srcOffsets[1]={ Image->Width>>(i-1), Image->Height>>(i-1), 1 },
			.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,
			.srcSubresource.mipLevel=i-1,
			.srcSubresource.baseArrayLayer=BaseLayer,
			.srcSubresource.layerCount=1,
			.dstOffsets[0]={ 0, 0, 0 },
			.dstOffsets[1]={ Image->Width>>i, Image->Height>>i, 1 },
			.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,
			.dstSubresource.mipLevel=i,
			.dstSubresource.baseArrayLayer=BaseLayer,
			.dstSubresource.layerCount=1,
		}, VK_FILTER_LINEAR);

		vkuTransitionLayout(CommandBuffer, Image->Image, 1, i-1, 1, BaseLayer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	vkuTransitionLayout(CommandBuffer, Image->Image, 1, MipLevels-1, 1, BaseLayer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// Image_Upload, external function.
// Loads an image file and uploads to the GPU, while performing conversions
//   and other processes based on the set flags.
//
// Flags also set Vulkan texture sampler properties.
VkBool32 Image_Upload(VkuContext_t *Context, VkuImage_t *Image, const char *Filename, uint32_t Flags)
{
	char *Extension=strrchr(Filename, '.');
	VkFilter MinFilter=VK_FILTER_LINEAR;
	VkFilter MagFilter=VK_FILTER_LINEAR;
	VkSamplerMipmapMode MipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
	VkSamplerAddressMode WrapModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VkSamplerAddressMode WrapModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VkSamplerAddressMode WrapModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VkFormat Format=VK_FORMAT_UNDEFINED;
	VkCommandBuffer CommandBuffer;
	VkuBuffer_t StagingBuffer;
	uint32_t MipLevels=1;
	void *Data=NULL;

	if(Extension!=NULL)
	{
		if(!strcmp(Extension, ".tga"))
		{
			if(!TGA_Load(Filename, Image))
				return VK_FALSE;
		}
		else
			if(!strcmp(Extension, ".qoi"))
			{
				if(!QOI_Load(Filename, Image))
					return VK_FALSE;
			}
			else
				return VK_FALSE;
	}

	if(Flags&IMAGE_NEAREST)
	{
		MagFilter=VK_FILTER_NEAREST;
		MipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
		MinFilter=VK_FILTER_NEAREST;
	}

	if(Flags&IMAGE_BILINEAR)
	{
		MagFilter=VK_FILTER_LINEAR;
		MipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
		MinFilter=VK_FILTER_LINEAR;
	}

	if(Flags&IMAGE_TRILINEAR)
	{
		MagFilter=VK_FILTER_LINEAR;
		MipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
		MinFilter=VK_FILTER_LINEAR;
	}

	if(Flags&IMAGE_CLAMP_U)
		WrapModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	if(Flags&IMAGE_CLAMP_V)
		WrapModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	if(Flags&IMAGE_CLAMP_W)
		WrapModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	if(Flags&IMAGE_REPEAT_U)
		WrapModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;

	if(Flags&IMAGE_REPEAT_V)
		WrapModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT;

	if(Flags&IMAGE_REPEAT_W)
		WrapModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;

	// Convert from RGB to RGBA if needed.
	_RGBtoRGBA(Image);

	if(Flags&IMAGE_RGBE)
		_RGBE2Float(Image);

	if(Flags&IMAGE_NORMALMAP)
		_MakeNormalMap(Image);

	if(Flags&IMAGE_NORMALIZE)
		_Normalize(Image);

	switch(Image->Depth)
	{
		case 128:
			Format=VK_FORMAT_R32G32B32A32_SFLOAT;
			break;

		case 64:
			Format=VK_FORMAT_R16G16B16A16_UNORM;
			break;

		case 32:
			Format=VK_FORMAT_B8G8R8A8_SRGB;
			break;

		case 16:
			Format=VK_FORMAT_R5G6B5_UNORM_PACK16;
			break;

		case 8:
			Format=VK_FORMAT_R8_UNORM;
			break;

		default:
			Zone_Free(Zone, Image->Data);
			return VK_FALSE;
	}

	// Upload as cubemap if that flag was set
	if(Flags&IMAGE_CUBEMAP_ANGULAR)
	{
		VkuImage_t Out;
		void *Data=NULL;

		// Calculate the cubemap face size from the angular map size (half the size of the original seems to be good)
		Out.Width=NextPower2(Image->Width>>1);
		Out.Height=NextPower2(Image->Height>>1);
		Out.Depth=Image->Depth;

		// Precalculate each cube face iamge size in bytes
		uint32_t Size=Out.Width*Out.Height*(Image->Depth>>3);

		// Create staging buffer
		vkuCreateHostBuffer(Context, &StagingBuffer, Size*6, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

		// Map image memory and copy data for each cube face
		vkMapMemory(Context->Device, StagingBuffer.DeviceMemory, 0, VK_WHOLE_SIZE, 0, &Data);
		for(uint32_t i=0;i<6;i++)
		{
			// Extract the cubemap face from the angular map lightprobe
			if(!_AngularMapFace(Image, i, &Out))
				return VK_FALSE;

			// Copy the image into the staging buffer
			memcpy((uint8_t *)Data+((size_t)Size*i), Out.Data, Size);

			// Free the output data for the next face
			Zone_Free(Zone, Out.Data);
		}
		vkUnmapMemory(Context->Device, StagingBuffer.DeviceMemory);

		// All faces are copied, free the original image data
		Zone_Free(Zone, Image->Data);

		// Going from an angular map to 6 faces, the faces are a different size than the original image,
		//	so change the width/height of the working image we're loading.
		Image->Width=Out.Width;
		Image->Height=Out.Height;

		// If mipmaps are requested, calculate how many levels there are
		if(Flags&IMAGE_MIPMAP)
			MipLevels=ComputeLog(max(Image->Width, Image->Height))+1;

		// Create the GPU side image memory buffer
		vkuCreateImageBuffer(Context, Image,
							 VK_IMAGE_TYPE_2D, Format, MipLevels, 6, Out.Width, Out.Height, 1, VK_SAMPLE_COUNT_1_BIT,
							 Format==VK_FORMAT_R32G32B32A32_SFLOAT?VK_IMAGE_TILING_LINEAR:VK_IMAGE_TILING_OPTIMAL,
							 VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
							 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							 VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

		// Start a one shot command buffer
		CommandBuffer=vkuOneShotCommandBufferBegin(Context);

		// Change image layout from undefined to destination optimal, so we can copy from the staging buffer to the texture.
		vkuTransitionLayout(CommandBuffer, Image->Image, MipLevels, 0, 6, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		// Copy all faces from staging buffer to the texture image buffer.
		vkCmdCopyBufferToImage(CommandBuffer, StagingBuffer.Buffer, Image->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, (VkBufferImageCopy[6])
		{
			{ (VkDeviceSize)Size*0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { Out.Width, Out.Height, 1 } },
			{ (VkDeviceSize)Size*1, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1 }, { 0, 0, 0 }, { Out.Width, Out.Height, 1 } },
			{ (VkDeviceSize)Size*2, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 2, 1 }, { 0, 0, 0 }, { Out.Width, Out.Height, 1 } },
			{ (VkDeviceSize)Size*3, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 1 }, { 0, 0, 0 }, { Out.Width, Out.Height, 1 } },
			{ (VkDeviceSize)Size*4, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 4, 1 }, { 0, 0, 0 }, { Out.Width, Out.Height, 1 } },
			{ (VkDeviceSize)Size*5, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 5, 1 }, { 0, 0, 0 }, { Out.Width, Out.Height, 1 } }
		});

		// Final change to image layout from destination optimal to be optimal reading only by shader.
		// This is also done by generating mipmaps, if requested.
		if(Flags&IMAGE_MIPMAP)
		{
			_GenerateMipmaps(CommandBuffer, Image, MipLevels, 6, 0);
			_GenerateMipmaps(CommandBuffer, Image, MipLevels, 6, 1);
			_GenerateMipmaps(CommandBuffer, Image, MipLevels, 6, 2);
			_GenerateMipmaps(CommandBuffer, Image, MipLevels, 6, 3);
			_GenerateMipmaps(CommandBuffer, Image, MipLevels, 6, 4);
			_GenerateMipmaps(CommandBuffer, Image, MipLevels, 6, 5);
		}
		else
			vkuTransitionLayout(CommandBuffer, Image->Image, MipLevels, 0, 6, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// End one shot command buffer and submit
		vkuOneShotCommandBufferEnd(Context, CommandBuffer);

		// Delete staging buffers
		vkDestroyBuffer(Context->Device, StagingBuffer.Buffer, VK_NULL_HANDLE);
		vkFreeMemory(Context->Device, StagingBuffer.DeviceMemory, VK_NULL_HANDLE);
	}
	else // Otherwise it's a 2D texture
	{
		// Byte size of image data
		uint32_t Size=Image->Width*Image->Height*(Image->Depth>>3);

		// Create staging buffer
		vkuCreateHostBuffer(Context, &StagingBuffer, Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

		// Map image memory and copy data
		vkMapMemory(Context->Device, StagingBuffer.DeviceMemory, 0, VK_WHOLE_SIZE, 0, &Data);
		memcpy(Data, Image->Data, Size);
		vkUnmapMemory(Context->Device, StagingBuffer.DeviceMemory);

		// Original image data is now in a Vulkan memory object, so no longer need the original data.
		Zone_Free(Zone, Image->Data);

		if(Flags&IMAGE_MIPMAP)
			MipLevels=ComputeLog(max(Image->Width, Image->Height))+1;

		if(!vkuCreateImageBuffer(Context, Image,
		   VK_IMAGE_TYPE_2D, Format, MipLevels, 1, Image->Width, Image->Height, 1,
		   VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
		   VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0))
			return VK_FALSE;

		// Start a one shot command buffer
		CommandBuffer=vkuOneShotCommandBufferBegin(Context);

		// Change image layout from undefined to destination optimal, so we can copy from the staging buffer to the texture.
		vkuTransitionLayout(CommandBuffer, Image->Image, MipLevels, 0, 1, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		// Copy from staging buffer to the texture buffer.
		vkCmdCopyBufferToImage(CommandBuffer, StagingBuffer.Buffer, Image->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, (VkBufferImageCopy[1])
		{
			{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { Image->Width, Image->Height, 1 } }
		});

		// Final change to image layout from destination optimal to be optimal reading only by shader.
		// This is also done by generating mipmaps, if requested.
		if(Flags&IMAGE_MIPMAP)
			_GenerateMipmaps(CommandBuffer, Image, MipLevels, 1, 0);
		else
			vkuTransitionLayout(CommandBuffer, Image->Image, MipLevels, 0, 1, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// End one shot command buffer and submit
		vkuOneShotCommandBufferEnd(Context, CommandBuffer);

		// Delete staging buffers
		vkDestroyBuffer(Context->Device, StagingBuffer.Buffer, VK_NULL_HANDLE);
		vkFreeMemory(Context->Device, StagingBuffer.DeviceMemory, VK_NULL_HANDLE);
	}

	// Create texture sampler object
	vkCreateSampler(Context->Device, &(VkSamplerCreateInfo)
	{
		.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter=MagFilter,
		.minFilter=MinFilter,
		.mipmapMode=MipmapMode,
		.addressModeU=WrapModeU,
		.addressModeV=WrapModeV,
		.addressModeW=WrapModeW,
		.mipLodBias=0.0f,
		.compareOp=VK_COMPARE_OP_NEVER,
		.minLod=0.0f,
		.maxLod=VK_LOD_CLAMP_NONE,
		.maxAnisotropy=1.0f,
		.anisotropyEnable=VK_FALSE,
		.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	}, VK_NULL_HANDLE, &Image->Sampler);

	// Create texture image view object
	vkCreateImageView(Context->Device, &(VkImageViewCreateInfo)
	{
		.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image=Image->Image,
		.viewType=Flags&IMAGE_CUBEMAP_ANGULAR?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_2D,
		.format=Format,
		.components={ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
		.subresourceRange={ VK_IMAGE_ASPECT_COLOR_BIT, 0, MipLevels, 0, Flags&IMAGE_CUBEMAP_ANGULAR?6:1 },
	}, VK_NULL_HANDLE, &Image->View);

	return VK_TRUE;
}
