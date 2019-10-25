//
// Created by frol on 15.06.19.
//

#ifndef VULKAN_MINIMAL_COMPUTE_VK_UTILS_H
#define VULKAN_MINIMAL_COMPUTE_VK_UTILS_H

#include <vulkan/vulkan.h>
#include <vector>

#include <stdexcept>
#include <sstream>

namespace vk_utils
{

  typedef VkBool32 (VKAPI_PTR *DebugReportCallbackFuncType)(VkDebugReportFlagsEXT      flags,
                                                            VkDebugReportObjectTypeEXT objectType,
                                                            uint64_t                   object,
                                                            size_t                     location,
                                                            int32_t                    messageCode,
                                                            const char*                pLayerPrefix,
                                                            const char*                pMessage,
                                                            void*                      pUserData);


  static void RunTimeError(const char* file, int line, const char* msg)
  {
    std::stringstream strout;
    strout << "runtime_error at " << file << ", line " << line << ": " << msg << std::endl;
    throw std::runtime_error(strout.str().c_str());
  }


  VkInstance CreateInstance(bool a_enableValidationLayers, std::vector<const char *>& a_enabledLayers);
  void       InitDebugReportCallback(VkInstance a_instance, DebugReportCallbackFuncType a_callback, VkDebugReportCallbackEXT* a_debugReportCallback);

  VkPhysicalDevice FindPhysicalDevice(VkInstance a_instance, bool a_printInfo, int a_preferredDeviceId);

  uint32_t GetComputeQueueFamilyIndex(VkPhysicalDevice physicalDevice);
  VkDevice CreateLogicalDevice(uint32_t queueFamilyIndex, VkPhysicalDevice physicalDevice, const std::vector<const char *>& a_enabledLayers);
  uint32_t FindMemoryType(uint32_t memoryTypeBits, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice);

  std::vector<uint32_t> ReadFile(const char* filename);
};

#undef  RUN_TIME_ERROR
#undef  RUN_TIME_ERROR_AT
#define RUN_TIME_ERROR(e) (vk_utils::RunTimeError(__FILE__,__LINE__,(e)))
#define RUN_TIME_ERROR_AT(e, file, line) (vk_utils::RunTimeError((file),(line),(e)))

// Used for validating return values of Vulkan API calls.
//
#define VK_CHECK_RESULT(f) 													\
{																										\
    VkResult res = (f);															\
    if (res != VK_SUCCESS)													\
    {																								\
        printf("Fatal : VkResult is %d in %s at line %d\n", res,  __FILE__, __LINE__); \
        assert(res == VK_SUCCESS);									\
    }																								\
}

#endif //VULKAN_MINIMAL_COMPUTE_VK_UTILS_H
