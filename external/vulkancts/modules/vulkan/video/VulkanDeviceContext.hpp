#ifndef _VULKANDEVICECONTEXT_HPP
#define _VULKANDEVICECONTEXT_HPP
/*
* Copyright 2022 NVIDIA Corporation.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "vkDefs.hpp"

#include <vector>

using namespace vk;

inline VkResult MapMemoryTypeToIndex(const InstanceInterface& vkIf,
									 VkPhysicalDevice vkPhysicalDev,
                                     uint32_t typeBits,
                                     VkFlags requirements_mask, uint32_t *typeIndex)
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkIf.getPhysicalDeviceMemoryProperties(vkPhysicalDev, &memoryProperties);
    // Search memtypes to find first index with those properties
    for (uint32_t i = 0; i < 32; i++) {
        if ((typeBits & 1) == 1) {
            // Type is available, does it match user properties?
            if ((memoryProperties.memoryTypes[i].propertyFlags & requirements_mask) ==
                    requirements_mask) {
                *typeIndex = i;
                return VK_SUCCESS;
            }
        }
        typeBits >>= 1;
    }
    return VK_ERROR_VALIDATION_FAILED_EXT;
}


inline VkResult get(const InstanceInterface& vkIf,
                    VkPhysicalDevice phy, std::vector<VkQueueFamilyProperties2> &queues,
                    std::vector<VkQueueFamilyVideoPropertiesKHR> &videoQueues,
                    std::vector<VkQueueFamilyQueryResultStatusPropertiesKHR> &queryResultStatus) {
    uint32_t count = 0;
    vkIf.getPhysicalDeviceQueueFamilyProperties2(phy, &count, nullptr);

    queues.resize(count);
    videoQueues.resize(count);
    queryResultStatus.resize(count);
    for (uint32_t i = 0; i < queues.size(); i++) {
        queues[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        videoQueues[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
        queues[i].pNext = &videoQueues[i];
        queryResultStatus[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR;
        videoQueues[i].pNext = &queryResultStatus[i];
    }

    vkIf.getPhysicalDeviceQueueFamilyProperties2(phy, &count, queues.data());

    return VK_SUCCESS;
}


#endif // _VULKANDEVICECONTEXT_HPP
