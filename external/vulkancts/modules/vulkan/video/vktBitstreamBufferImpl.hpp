#ifndef _VKTBITSTREAMBUFFERIMPL_HPP
#define _VKTBITSTREAMBUFFERIMPL_HPP
/*------------------------------------------------------------------------
 * Vulkan Conformance Tests
 * ------------------------
 *
 * Copyright (c) 2023 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *//*!
 * \file
 * \brief Bitstream buffer implementation for the CTS.
 *//*--------------------------------------------------------------------*/
/*
* Copyright 2023 NVIDIA Corporation.
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

#include <atomic>
#include <iostream>
#include <vector>

#include "vkvideo_parser/VulkanBitstreamBuffer.h"

#include "vktVideoTestUtils.hpp"

namespace vkt
{
namespace video
{

using BufferPtr = de::MovePtr<BufferWithMemory>;

// This class is required by the NVIDIA sample decoder interface.
// The following is a CTS implementation of the VulkanBitstreamBuffer interface upstream.
class BitstreamBufferImpl : public VulkanBitstreamBuffer
{
public:
    static VkResult Create(DeviceContext* devctx, deUint32 queueFamilyIndex,
             VkDeviceSize bufferSize, VkDeviceSize bufferOffsetAlignment, VkDeviceSize bufferSizeAlignment,
             VkSharedBaseObj<BitstreamBufferImpl>& vulkanBitstreamBuffer,
						   const VkVideoProfileListInfoKHR* profileList);

    int32_t AddRef() override
    {
        return ++m_refCount;
    }

    int32_t Release() override
    {
        deUint32 ret = --m_refCount;
        if (ret == 0) {
            delete this;
        }
        return ret;
    }

    int32_t GetRefCount() override
    {
        DE_ASSERT(m_refCount > 0);
        return m_refCount;
    }

    size_t GetMaxSize() const override;
    size_t GetOffsetAlignment() const override;
    size_t GetSizeAlignment() const override;
    size_t Resize(size_t newSize, size_t copySize = 0, size_t copyOffset = 0) override;

    int64_t  MemsetData(deUint32 value, size_t offset, size_t size) override;
    int64_t  CopyDataToBuffer(deUint8 *dstBuffer, size_t dstOffset,
                                      size_t srcOffset, size_t size) const override;
    int64_t  CopyDataToBuffer(VkSharedBaseObj<VulkanBitstreamBuffer>& dstBuffer, size_t dstOffset,
                                      size_t srcOffset, size_t size) const override;
    int64_t  CopyDataFromBuffer(const deUint8 *sourceBuffer, size_t srcOffset,
                                        size_t dstOffset, size_t size) override;
    int64_t  CopyDataFromBuffer(const VkSharedBaseObj<VulkanBitstreamBuffer>& sourceBuffer, size_t srcOffset,
                                        size_t dstOffset, size_t size) override;
    deUint8* GetDataPtr(size_t offset, size_t &maxSize) override;
    const deUint8* GetReadOnlyDataPtr(size_t offset, size_t &maxSize) const override;

    void FlushRange(size_t offset, size_t size) const override;
    void InvalidateRange(size_t offset, size_t size) const override;

    VkBuffer GetBuffer() const override { return m_bitstreamBuffer->get(); }
    VkDeviceMemory GetDeviceMemory() const override { return m_bitstreamBuffer->getAllocation().getMemory(); }

    deUint32  AddStreamMarker(deUint32 streamOffset) override;
    deUint32  SetStreamMarker(deUint32 streamOffset, deUint32 index) override;
    deUint32  GetStreamMarker(deUint32 index) const override;
    deUint32  GetStreamMarkersCount() const override;
    const deUint32* GetStreamMarkersPtr(deUint32 startIndex, deUint32& maxCount) const override;
    deUint32  ResetStreamMarkers() override;

    operator VkDeviceMemory() { return GetDeviceMemory(); }
    operator bool() { return !!m_bitstreamBuffer; }

    VkResult CopyDataToBuffer(const deUint8* pData, VkDeviceSize size,
                              VkDeviceSize &dstBufferOffset) const;

private:

    VkResult CreateBuffer(DeviceContext* ctx, deUint32 queueFamilyIndex,
                                 VkDeviceSize& bufferSize,
                                 VkDeviceSize bufferSizeAlignment,
								 const VkVideoProfileListInfoKHR* profileList);

    deUint8* CheckAccess(size_t offset, size_t size) const;

    VkResult Initialize(VkDeviceSize bufferSize);

	BitstreamBufferImpl(DeviceContext* devctx,
                              deUint32 queueFamilyIndex,
                              VkDeviceSize bufferOffsetAlignment,
                              VkDeviceSize bufferSizeAlignment,
							  const VkVideoProfileListInfoKHR* profileList)
        : VulkanBitstreamBuffer()
        , m_refCount(0)
		, m_devctx(devctx)
		, m_profileList(profileList)
        , m_queueFamilyIndex(queueFamilyIndex)
        , m_bufferOffsetAlignment(bufferOffsetAlignment)
        , m_bufferSizeAlignment(bufferSizeAlignment)
		, m_bufferSize(0)
        , m_streamMarkers(256) { }


private:
	std::atomic<int32_t>			 m_refCount;
	DeviceContext*					 m_devctx;
	const VkVideoProfileListInfoKHR* m_profileList;
	deUint32						 m_queueFamilyIndex;
	VkDeviceSize					 m_bufferOffsetAlignment;
	VkDeviceSize					 m_bufferSizeAlignment;
	BufferPtr						 m_bitstreamBuffer;
	size_t							 m_bufferSize;
	std::vector<deUint32>			 m_streamMarkers;
};

} // video
} // vkt

#endif // _VKTBITSTREAMBUFFERIMPL_HPP
