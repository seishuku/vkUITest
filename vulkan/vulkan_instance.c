// Vulkan helper functions
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../system/system.h"
#include "vulkan.h"

PFN_vkCreateDebugUtilsMessengerEXT _vkCreateDebugUtilsMessengerEXT=VK_NULL_HANDLE;
PFN_vkDestroyDebugUtilsMessengerEXT _vkDestroyDebugUtilsMessengerEXT=VK_NULL_HANDLE;

// Create Vulkan Instance
VkBool32 CreateVulkanInstance(VkInstance *Instance)
{
	uint32_t ExtensionCount=0;

	if(vkEnumerateInstanceExtensionProperties(NULL, &ExtensionCount, NULL)!=VK_SUCCESS)
	{
		DBGPRINTF(DEBUG_ERROR, "vkEnumerateInstanceExtensionProperties failed.\n");
		return VK_FALSE;
	}

	VkExtensionProperties *ExtensionProperties=(VkExtensionProperties *)Zone_Malloc(Zone, sizeof(VkExtensionProperties)*ExtensionCount);

	if(ExtensionProperties==VK_NULL_HANDLE)
	{
		DBGPRINTF(DEBUG_ERROR, "Failed to allocate memory for extension properties.\n");
		return VK_FALSE;
	}

	if(vkEnumerateInstanceExtensionProperties(VK_NULL_HANDLE, &ExtensionCount, ExtensionProperties)!=VK_SUCCESS)
	{
		Zone_Free(Zone, ExtensionProperties);
		DBGPRINTF(DEBUG_ERROR, "vkEnumerateInstanceExtensionProperties failed.\n");
		return VK_FALSE;
	}

	VkBool32 SurfaceOSExtension=VK_FALSE;
	VkBool32 SurfaceExtension=VK_FALSE;
	VkBool32 DebugExtension=VK_FALSE;

	for(uint32_t i=0;i<ExtensionCount;i++)
	{
#ifdef WIN32
		if(strcmp(ExtensionProperties[i].extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME)==0)
		{
			DBGPRINTF(DEBUG_INFO, VK_KHR_WIN32_SURFACE_EXTENSION_NAME" extension is supported!\n");
			SurfaceOSExtension=VK_TRUE;
		}
#else
		if(strcmp(ExtensionProperties[i].extensionName, VK_KHR_XLIB_SURFACE_EXTENSION_NAME)==0)
		{
			DBGPRINTF(DEBUG_INFO, VK_KHR_XLIB_SURFACE_EXTENSION_NAME" extension is supported!\n");
			SurfaceOSExtension=VK_TRUE;
		}
#endif
		else if(strcmp(ExtensionProperties[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME)==0)
		{
			DBGPRINTF(DEBUG_INFO, VK_KHR_SURFACE_EXTENSION_NAME" extension is supported!\n");
			SurfaceExtension=VK_TRUE;
		}
#ifdef _DEBUG
		else if(strcmp(ExtensionProperties[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)==0)
		{
			DBGPRINTF(DEBUG_INFO, VK_EXT_DEBUG_UTILS_EXTENSION_NAME" extension is supported!\n");
			DebugExtension=VK_TRUE;
		}
#endif
	}

	Zone_Free(Zone, ExtensionProperties);

	if(!SurfaceExtension||!SurfaceOSExtension)
	{
		DBGPRINTF(DEBUG_ERROR, "Missing required instance surface extension!\n");
		return VK_FALSE;
	}

	VkApplicationInfo AppInfo=
	{
		.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName="Engine",
		.applicationVersion=VK_MAKE_VERSION(1, 0, 0),
		.pEngineName="Engine",
		.engineVersion=VK_MAKE_VERSION(1, 0, 0),
		.apiVersion=VK_API_VERSION_1_3
	};

	const char *Extensions[]=
	{
#ifdef WIN32
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#else
			VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
			VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
	};

//#ifdef _DEBUG
//	VkValidationFeatureEnableEXT enabledValidationFeatures[]=
//	{
////		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
////		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
////		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
//	};
//#endif

	VkInstanceCreateInfo InstanceInfo=
	{
		.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo=&AppInfo,
		.enabledExtensionCount=sizeof(Extensions)/sizeof(void *),
		.ppEnabledExtensionNames=Extensions,
#ifdef _DEBUG
		//.pNext=&(VkValidationFeaturesEXT)
		//{
		//	.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		//	.pEnabledValidationFeatures=enabledValidationFeatures,
		//	.enabledValidationFeatureCount=sizeof(enabledValidationFeatures)/sizeof(VkValidationFeatureEnableEXT),
		//},
		.enabledLayerCount=1,
		.ppEnabledLayerNames=(const char *[]) { "VK_LAYER_KHRONOS_validation" },
#endif
	};

	if(vkCreateInstance(&InstanceInfo, 0, Instance)!=VK_SUCCESS)
	{
		DBGPRINTF(DEBUG_ERROR, "vkCreateInstance failed.\n");
		return VK_FALSE;
	}

	if(DebugExtension)
	{
		if((_vkCreateDebugUtilsMessengerEXT=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(*Instance, "vkCreateDebugUtilsMessengerEXT"))==VK_NULL_HANDLE)
		{
			DBGPRINTF("vkGetInstanceProcAddr failed on vkCreateDebugUtilsMessengerEXT.\n");
			return VK_FALSE;
		}
		else if((_vkDestroyDebugUtilsMessengerEXT=(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(*Instance, "vkDestroyDebugUtilsMessengerEXT"))==VK_NULL_HANDLE)
		{
			DBGPRINTF("vkGetInstanceProcAddr failed on vkDestroyDebugUtilsMessengerEXT.\n");
			return VK_FALSE;
		}
	}

	return VK_TRUE;
}
