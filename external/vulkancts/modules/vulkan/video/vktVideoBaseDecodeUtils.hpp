#ifndef _VKTVIDEOBASEDECODEUTILS_HPP
#define _VKTVIDEOBASEDECODEUTILS_HPP
/*------------------------------------------------------------------------
 * Vulkan Conformance Tests
 * ------------------------
 *
 * Copyright (c) 2021 The Khronos Group Inc.
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
 * \brief Video Decoding Base Classe Functionality
 *//*--------------------------------------------------------------------*/
 /*
 * Copyright 2020 NVIDIA Corporation.
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

#include "vktVideoTestUtils.hpp"
#include "extNvidiaVideoParserIf.hpp"

#include "deMemory.h"
#include "vkImageWithMemory.hpp"
#include "vkBufferWithMemory.hpp"

#include <array>
#include <vector>
#include <list>
#include <bitset>
#include <queue>

namespace vkt
{
namespace video
{

using namespace vk;
using namespace std;

#define MAKEFRAMERATE(num, den) (((num) << 14) | (den))
#define NV_FRAME_RATE_NUM(rate) ((rate) >> 14)
#define NV_FRAME_RATE_DEN(rate) ((rate)&0x3fff)

bool videoLoggingEnabled();

class VkImageResource : public VkVideoRefCountBase
{
public:
	static VkResult Create(DeviceContext& vkDevCtx,
						   const VkImageCreateInfo* pImageCreateInfo,
						   VkMemoryPropertyFlags memoryPropertyFlags,
						   VkSharedBaseObj<VkImageResource>& imageResource);

	bool IsCompatible (const VkImageCreateInfo* pImageCreateInfo)
	{

		if (pImageCreateInfo->extent.width > m_imageCreateInfo.extent.width) {
			return false;
		}

		if (pImageCreateInfo->extent.height > m_imageCreateInfo.extent.height) {
			return false;
		}

		if (pImageCreateInfo->arrayLayers > m_imageCreateInfo.arrayLayers) {
			return false;
		}

		if (pImageCreateInfo->tiling != m_imageCreateInfo.tiling) {
			return false;
		}

		if (pImageCreateInfo->imageType != m_imageCreateInfo.imageType) {
			return false;
		}

		if (pImageCreateInfo->format != m_imageCreateInfo.format) {
			return false;
		}

		return true;
	}


	virtual int32_t AddRef()
	{
		return ++m_refCount;
	}

	virtual int32_t Release()
	{
		uint32_t ret = --m_refCount;
		// Destroy the device if ref-count reaches zero
		if (ret == 0) {
			delete this;
		}
		return ret;
	}

	operator VkImage() const { return m_image; }
	VkImage GetImage() const { return m_image; }
	VkDevice GetDevice() const { return m_vkDevCtx.device; }
	VkDeviceMemory GetDeviceMemory() const { return *m_vulkanDeviceMemory; }

	VkSharedBaseObj<VulkanDeviceMemoryImpl>& GetMemory() { return m_vulkanDeviceMemory; }

	VkDeviceSize GetImageDeviceMemorySize() const { return m_imageSize; }
	VkDeviceSize GetImageDeviceMemoryOffset() const { return m_imageOffset; }

	const VkImageCreateInfo& GetImageCreateInfo() const { return m_imageCreateInfo; }

private:
	std::atomic<int32_t>    m_refCount;
	const VkImageCreateInfo m_imageCreateInfo;
	DeviceContext&			m_vkDevCtx;
	VkImage                 m_image;
	VkDeviceSize            m_imageOffset;
	VkDeviceSize            m_imageSize;
	VkSharedBaseObj<VulkanDeviceMemoryImpl> m_vulkanDeviceMemory;

	VkImageResource(DeviceContext& vkDevCtx,
					const VkImageCreateInfo* pImageCreateInfo,
					VkImage image, VkDeviceSize imageOffset, VkDeviceSize imageSize,
					VkSharedBaseObj<VulkanDeviceMemoryImpl>& vulkanDeviceMemory)
			: m_refCount(0), m_imageCreateInfo(*pImageCreateInfo), m_vkDevCtx(vkDevCtx),
			m_image(image), m_imageOffset(imageOffset), m_imageSize(imageSize),
			m_vulkanDeviceMemory(vulkanDeviceMemory) { }

	void Destroy();

	virtual ~VkImageResource() { Destroy(); }
};

class VkImageResourceView : public VkVideoRefCountBase
{
public:
	static VkResult Create(DeviceContext& vkDevCtx,
						   VkSharedBaseObj<VkImageResource>& imageResource,
						   VkImageSubresourceRange &imageSubresourceRange,
						   VkSharedBaseObj<VkImageResourceView>& imageResourceView);


	virtual int32_t AddRef()
	{
		return ++m_refCount;
	}

	virtual int32_t Release()
	{
		uint32_t ret = --m_refCount;
		// Destroy the device if ref-count reaches zero
		if (ret == 0) {
			delete this;
		}
		return ret;
	}

	operator VkImageView() const { return m_imageView; }
	VkImageView GetImageView() const { return m_imageView; }
	VkDevice GetDevice() const { return m_vkDevCtx.device; }

	const VkImageSubresourceRange& GetImageSubresourceRange() const
	{ return m_imageSubresourceRange; }

	const VkSharedBaseObj<VkImageResource>& GetImageResource()
	{
		return m_imageResource;
	}

private:
	std::atomic<int32_t>             m_refCount;
	DeviceContext&				     m_vkDevCtx;
	VkSharedBaseObj<VkImageResource> m_imageResource;
	VkImageView                      m_imageView;
	VkImageSubresourceRange          m_imageSubresourceRange;


	VkImageResourceView(DeviceContext& vkDevCtx,
						VkSharedBaseObj<VkImageResource>& imageResource,
						VkImageView imageView, VkImageSubresourceRange &imageSubresourceRange)
			: m_refCount(0), m_vkDevCtx(vkDevCtx), m_imageResource(imageResource),
			m_imageView(imageView), m_imageSubresourceRange(imageSubresourceRange)
	{}

	virtual ~VkImageResourceView();
};

struct DecodedFrame {
	int32_t pictureIndex;
	int32_t displayWidth;
	int32_t displayHeight;
	VkSharedBaseObj<VkImageResourceView> decodedImageView;
	VkSharedBaseObj<VkImageResourceView> outputImageView;
	VkFence frameCompleteFence; // If valid, the fence is signaled when the decoder is done decoding the frame.
	VkFence frameConsumerDoneFence; // If valid, the fence is signaled when the consumer (graphics, compute or display) is done using the frame.
	VkSemaphore frameCompleteSemaphore; // If valid, the semaphore is signaled when the decoder is done decoding the frame.
	VkSemaphore frameConsumerDoneSemaphore; // If valid, the semaphore is signaled when the consumer (graphics, compute or display) is done using the frame.
	VkQueryPool queryPool; // queryPool handle used for the video queries.
	int32_t startQueryId;  // query Id used for the this frame.
	uint32_t numQueries;   // usually one query per frame
	// If multiple queues are available, submittedVideoQueueIndex is the queue index that the video frame was submitted to.
	// if only one queue is available, submittedVideoQueueIndex will always have a value of "0".
	int32_t  submittedVideoQueueIndex;
	uint64_t timestamp;
	uint32_t hasConsummerSignalFence : 1;
	uint32_t hasConsummerSignalSemaphore : 1;
	// For debugging
	int32_t decodeOrder;
	int32_t displayOrder;

	void Reset()
	{
		pictureIndex = -1;
		displayWidth = 0;
		displayHeight = 0;
		decodedImageView  = nullptr;
		outputImageView = nullptr;
		frameCompleteFence = VkFence();
		frameConsumerDoneFence = VkFence();
		frameCompleteSemaphore = VkSemaphore();
		frameConsumerDoneSemaphore = VkSemaphore();
		queryPool = VkQueryPool();
		startQueryId = 0;
		numQueries = 0;
		submittedVideoQueueIndex = 0;
		timestamp = 0;
		hasConsummerSignalFence = false;
		hasConsummerSignalSemaphore = false;
		// For debugging
		decodeOrder = 0;
		displayOrder = 0;
	}
};

struct DecodedFrameRelease {
	int32_t pictureIndex;
	VkVideotimestamp timestamp;
	uint32_t hasConsummerSignalFence : 1;
	uint32_t hasConsummerSignalSemaphore : 1;
	// For debugging
	int32_t decodeOrder;
	int32_t displayOrder;
};

// Keeps track of data associated with active internal reference frames
class DpbSlot {
public:
	bool isInUse() { return (m_reserved || m_inUse); }

	bool isAvailable() { return !isInUse(); }

	bool Invalidate()
	{
		bool wasInUse = isInUse();
		if (m_picBuf) {
			m_picBuf->Release();
			m_picBuf = NULL;
		}

		m_reserved = m_inUse = false;

		return wasInUse;
	}

	vkPicBuffBase* getPictureResource() { return m_picBuf; }

	vkPicBuffBase* setPictureResource(vkPicBuffBase* picBuf, int32_t age = 0)
	{
		vkPicBuffBase* oldPic = m_picBuf;

		if (picBuf) {
			picBuf->AddRef();
		}
		m_picBuf = picBuf;

		if (oldPic) {
			oldPic->Release();
		}

		m_pictureId = age;
		return oldPic;
	}

	void Reserve() { m_reserved = true; }

	void MarkInUse(int32_t age = 0)
	{
		m_pictureId = age;
		m_inUse = true;
	}

	int32_t getAge() { return m_pictureId; }

private:
	int32_t m_pictureId; // PictureID at map time (age)
	vkPicBuffBase* m_picBuf; // Associated resource

	uint32_t m_reserved : 1;
	uint32_t m_inUse : 1;
};

class DpbSlots {
public:
    explicit DpbSlots(uint8_t dpbMaxSize)
        : m_dpbMaxSize(0)
        , m_slotInUseMask(0)
        , m_dpb(m_dpbMaxSize)
        , m_dpbSlotsAvailable()
    {
        Init(dpbMaxSize, false);
    }

    int32_t Init(uint8_t newDpbMaxSize, bool reconfigure)
    {
        assert(newDpbMaxSize <= VkParserPerFrameDecodeParameters::MAX_DPB_REF_AND_SETUP_SLOTS);

        if (!reconfigure) {
            Deinit();
        }

        if (reconfigure && (newDpbMaxSize < m_dpbMaxSize)) {
            return m_dpbMaxSize;
        }

        uint8_t oldDpbMaxSize = reconfigure ? m_dpbMaxSize : 0;
        m_dpbMaxSize = newDpbMaxSize;

        m_dpb.resize(m_dpbMaxSize);

        for (uint32_t ndx = oldDpbMaxSize; ndx < m_dpbMaxSize; ndx++) {
            m_dpb[ndx].Invalidate();
        }

        for (uint8_t dpbIndx = oldDpbMaxSize; dpbIndx < m_dpbMaxSize; dpbIndx++) {
            m_dpbSlotsAvailable.push(dpbIndx);
        }

        return m_dpbMaxSize;
    }

    void Deinit()
    {
        for (uint32_t ndx = 0; ndx < m_dpbMaxSize; ndx++) {
            m_dpb[ndx].Invalidate();
        }

        while (!m_dpbSlotsAvailable.empty()) {
            m_dpbSlotsAvailable.pop();
        }

        m_dpbMaxSize = 0;
        m_slotInUseMask = 0;
    }

    ~DpbSlots() { Deinit(); }

    int8_t AllocateSlot()
    {
        if (m_dpbSlotsAvailable.empty()) {
            assert(!"No more h.264/5 DPB slots are available");
            return -1;
        }
        int8_t slot = (int8_t)m_dpbSlotsAvailable.front();
        assert((slot >= 0) && ((uint8_t)slot < m_dpbMaxSize));
        m_slotInUseMask |= (1 << slot);
        m_dpbSlotsAvailable.pop();
        m_dpb[slot].Reserve();
        return slot;
    }

    void FreeSlot(int8_t slot)
    {
        assert((uint8_t)slot < m_dpbMaxSize);
        assert(m_dpb[slot].isInUse());
        assert(m_slotInUseMask & (1 << slot));

        m_dpb[slot].Invalidate();
        m_dpbSlotsAvailable.push(slot);
        m_slotInUseMask &= ~(1 << slot);
    }

    DpbSlot& operator[](uint32_t slot)
    {
        assert(slot < m_dpbMaxSize);
        return m_dpb[slot];
    }

    // Return the remapped index given an external decode render target index
    int8_t GetSlotOfPictureResource(vkPicBuffBase* pPic)
    {
        for (int8_t i = 0; i < (int8_t)m_dpbMaxSize; i++) {
            if ((m_slotInUseMask & (1 << i)) && m_dpb[i].isInUse() && (pPic == m_dpb[i].getPictureResource())) {
                return i;
            }
        }
        return -1; // not found
    }

    void MapPictureResource(vkPicBuffBase* pPic, uint8_t dpbSlot,
        int32_t age = 0)
    {
        for (uint8_t slot = 0; slot < m_dpbMaxSize; slot++) {
            if (slot == dpbSlot) {
                m_dpb[slot].setPictureResource(pPic, age);
            } else if (pPic) {
                if (m_dpb[slot].getPictureResource() == pPic) {
                    FreeSlot(slot);
                }
            }
        }
    }

    uint32_t getSlotInUseMask() { return m_slotInUseMask; }

    uint32_t getMaxSize() { return m_dpbMaxSize; }

private:
    uint8_t m_dpbMaxSize;
    uint32_t m_slotInUseMask;
    std::vector<DpbSlot> m_dpb;
    std::queue<uint8_t> m_dpbSlotsAvailable;
};

class VulkanVideoSession : public VkVideoRefCountBase
{
    enum { MAX_BOUND_MEMORY = 9 };
public:
    static VkResult Create(DeviceContext& devCtx,
                           uint32_t            videoQueueFamily,
                           VkVideoCoreProfile* pVideoProfile,
                           VkFormat            pictureFormat,
                           const VkExtent2D&   maxCodedExtent,
                           VkFormat            referencePicturesFormat,
                           uint32_t            maxDpbSlots,
                           uint32_t            maxActiveReferencePictures,
                           VkSharedBaseObj<VulkanVideoSession>& videoSession);

    bool IsCompatible (VkDevice device,
                        uint32_t            videoQueueFamily,
                        VkVideoCoreProfile* pVideoProfile,
                        VkFormat            pictureFormat,
                        const VkExtent2D&   maxCodedExtent,
                        VkFormat            referencePicturesFormat,
                        uint32_t            maxDpbSlots,
                        uint32_t            maxActiveReferencePictures)
    {
        if (*pVideoProfile != m_profile) {
            return false;
        }

        if (maxCodedExtent.width > m_createInfo.maxCodedExtent.width) {
            return false;
        }

        if (maxCodedExtent.height > m_createInfo.maxCodedExtent.height) {
            return false;
        }

        if (maxDpbSlots > m_createInfo.maxDpbSlots) {
            return false;
        }

        if (maxActiveReferencePictures > m_createInfo.maxActiveReferencePictures) {
            return false;
        }

        if (m_createInfo.referencePictureFormat != referencePicturesFormat) {
            return false;
        }

        if (m_createInfo.pictureFormat != pictureFormat) {
            return false;
        }

        if (m_devCtx.device != device) {
            return false;
        }

        if (m_createInfo.queueFamilyIndex != videoQueueFamily) {
            return false;
        }

        return true;
    }


    virtual int32_t AddRef()
    {
        return ++m_refCount;
    }

    virtual int32_t Release()
    {
        uint32_t ret = --m_refCount;
        // Destroy the device if refcount reaches zero
        if (ret == 0) {
            delete this;
        }
        return ret;
    }

    VkVideoSessionKHR GetVideoSession() const { return m_videoSession; }

private:

    VulkanVideoSession(DeviceContext& devCtx,
                   VkVideoCoreProfile* pVideoProfile)
       : m_refCount(0), m_profile(*pVideoProfile), m_devCtx(devCtx),
         m_createInfo{ VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR, NULL },
         m_videoSession(VkVideoSessionKHR()), m_memoryBound{}
    {
    }

    ~VulkanVideoSession()
    {
		auto& vk = m_devCtx.getDeviceDriver();
        if (!!m_videoSession) {
			vk.destroyVideoSessionKHR(m_devCtx.device, m_videoSession, NULL);
            m_videoSession = VkVideoSessionKHR();
        }

        for (uint32_t memIdx = 0; memIdx < MAX_BOUND_MEMORY; memIdx++) {
            if (m_memoryBound[memIdx] != VK_NULL_HANDLE) {
                vk.freeMemory(m_devCtx.device, m_memoryBound[memIdx], 0);
                m_memoryBound[memIdx] = VK_NULL_HANDLE;
            }
        }
    }

private:
    std::atomic<int32_t>                   m_refCount;
    VkVideoCoreProfile                     m_profile;
	DeviceContext&						   m_devCtx;
    VkVideoSessionCreateInfoKHR            m_createInfo;
    VkVideoSessionKHR                      m_videoSession;
    VkDeviceMemory                         m_memoryBound[MAX_BOUND_MEMORY];
};


class VkParserVideoPictureParameters : public VkVideoRefCountBase {
public:
    static const uint32_t MAX_VPS_IDS =  16;
    static const uint32_t MAX_SPS_IDS =  32;
    static const uint32_t MAX_PPS_IDS = 256;

    //! Increment the reference count by 1.
    virtual int32_t AddRef();

    //! Decrement the reference count by 1. When the reference count
    //! goes to 0 the object is automatically destroyed.
    virtual int32_t Release();

    static VkParserVideoPictureParameters* VideoPictureParametersFromBase(VkVideoRefCountBase* pBase ) {
        if (!pBase) {
            return NULL;
        }
        VkParserVideoPictureParameters* pPictureParameters = static_cast<VkParserVideoPictureParameters*>(pBase);
        if (m_refClassId == pPictureParameters->m_classId) {
            return pPictureParameters;
        }
        assert(!"Invalid VkParserVideoPictureParameters from base");
        return nullptr;
    }

    static VkResult AddPictureParameters(DeviceContext& deviceContext,
                                         VkSharedBaseObj<VulkanVideoSession>& videoSession,
                                         VkSharedBaseObj<StdVideoPictureParametersSet>& stdPictureParametersSet,
                                         VkSharedBaseObj<VkParserVideoPictureParameters>& currentVideoPictureParameters);

    static bool CheckStdObjectBeforeUpdate(VkSharedBaseObj<StdVideoPictureParametersSet>& pictureParametersSet,
                                           VkSharedBaseObj<VkParserVideoPictureParameters>& currentVideoPictureParameters);

    static VkResult Create(DeviceContext& deviceContext,
                           VkSharedBaseObj<VkParserVideoPictureParameters>& templatePictureParameters,
                           VkSharedBaseObj<VkParserVideoPictureParameters>& videoPictureParameters);

    static int32_t PopulateH264UpdateFields(const StdVideoPictureParametersSet* pStdPictureParametersSet,
                                            VkVideoDecodeH264SessionParametersAddInfoKHR& h264SessionParametersAddInfo);

    static int32_t PopulateH265UpdateFields(const StdVideoPictureParametersSet* pStdPictureParametersSet,
                                            VkVideoDecodeH265SessionParametersAddInfoKHR& h265SessionParametersAddInfo);

    VkResult CreateParametersObject(VkSharedBaseObj<VulkanVideoSession>& videoSession,
                                    const StdVideoPictureParametersSet* pStdVideoPictureParametersSet,
                                    VkParserVideoPictureParameters* pTemplatePictureParameters);

    VkResult UpdateParametersObject(StdVideoPictureParametersSet* pStdVideoPictureParametersSet);

    VkResult HandleNewPictureParametersSet(VkSharedBaseObj<VulkanVideoSession>& videoSession,
                                           StdVideoPictureParametersSet* pStdVideoPictureParametersSet);

    operator VkVideoSessionParametersKHR() const {
        assert(m_sessionParameters != VK_NULL_HANDLE);
        return m_sessionParameters;
    }

    VkVideoSessionParametersKHR GetVideoSessionParametersKHR() const {
        assert(m_sessionParameters != VK_NULL_HANDLE);
        return m_sessionParameters;
    }

    int32_t GetId() const { return m_Id; }

    bool HasVpsId(uint32_t vpsId) const {
        return m_vpsIdsUsed[vpsId];
    }

    bool HasSpsId(uint32_t spsId) const {
        return m_spsIdsUsed[spsId];
    }

    bool HasPpsId(uint32_t ppsId) const {
        return m_ppsIdsUsed[ppsId];
    }


    bool UpdatePictureParametersHierarchy(VkSharedBaseObj<StdVideoPictureParametersSet>& pictureParametersObject);

    VkResult AddPictureParametersToQueue(VkSharedBaseObj<StdVideoPictureParametersSet>& pictureParametersSet);
    int32_t FlushPictureParametersQueue(VkSharedBaseObj<VulkanVideoSession>& videoSession);

protected:
    VkParserVideoPictureParameters(DeviceContext& deviceContext,
                                   VkSharedBaseObj<VkParserVideoPictureParameters>& templatePictureParameters)
        : m_classId(m_refClassId),
          m_Id(-1),
          m_refCount(0),
          m_deviceContext(deviceContext),
          m_videoSession(),
          m_sessionParameters(),
          m_templatePictureParameters(templatePictureParameters) { }

    virtual ~VkParserVideoPictureParameters();

private:
    static const char*              m_refClassId;
    static int32_t                  m_currentId;
    const char*                     m_classId;
    int32_t                         m_Id;
    std::atomic<int32_t>            m_refCount;
	DeviceContext&					m_deviceContext;
    VkSharedBaseObj<VulkanVideoSession> m_videoSession;
    VkVideoSessionParametersKHR     m_sessionParameters;
    std::bitset<MAX_VPS_IDS>        m_vpsIdsUsed;
    std::bitset<MAX_SPS_IDS>        m_spsIdsUsed;
    std::bitset<MAX_PPS_IDS>        m_ppsIdsUsed;
    VkSharedBaseObj<VkParserVideoPictureParameters> m_templatePictureParameters; // needed only for the create

    std::queue<VkSharedBaseObj<StdVideoPictureParametersSet>>  m_pictureParametersQueue;
    VkSharedBaseObj<StdVideoPictureParametersSet>              m_lastPictParamsQueue[StdVideoPictureParametersSet::NUM_OF_TYPES];
};


struct nvVideoDecodeH264DpbSlotInfo {
	VkVideoDecodeH264DpbSlotInfoKHR dpbSlotInfo;
	StdVideoDecodeH264ReferenceInfo stdReferenceInfo;

	nvVideoDecodeH264DpbSlotInfo()
			: dpbSlotInfo()
			  , stdReferenceInfo()
	{
	}

	const VkVideoDecodeH264DpbSlotInfoKHR* Init(int8_t slotIndex)
	{
		assert((slotIndex >= 0) && (slotIndex < (int8_t)VkParserPerFrameDecodeParameters::MAX_DPB_REF_AND_SETUP_SLOTS));
		dpbSlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
		dpbSlotInfo.pNext = NULL;
		dpbSlotInfo.pStdReferenceInfo = &stdReferenceInfo;
		return &dpbSlotInfo;
	}

	bool IsReference() const
	{
		return (dpbSlotInfo.pStdReferenceInfo == &stdReferenceInfo);
	}

	operator bool() const { return IsReference(); }
	void Invalidate() { memset(this, 0x00, sizeof(*this)); }
};

struct nvVideoDecodeH265DpbSlotInfo {
	VkVideoDecodeH265DpbSlotInfoKHR dpbSlotInfo;
	StdVideoDecodeH265ReferenceInfo stdReferenceInfo;

	nvVideoDecodeH265DpbSlotInfo()
			: dpbSlotInfo()
			  , stdReferenceInfo()
	{
	}

	const VkVideoDecodeH265DpbSlotInfoKHR* Init(int8_t slotIndex)
	{
		assert((slotIndex >= 0) && (slotIndex < (int8_t)VkParserPerFrameDecodeParameters::MAX_DPB_REF_SLOTS));
		dpbSlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR;
		dpbSlotInfo.pNext = NULL;
		dpbSlotInfo.pStdReferenceInfo = &stdReferenceInfo;
		return &dpbSlotInfo;
	}

	bool IsReference() const
	{
		return (dpbSlotInfo.pStdReferenceInfo == &stdReferenceInfo);
	}

	operator bool() const { return IsReference(); }

	void Invalidate() { memset(this, 0x00, sizeof(*this)); }
};

using VulkanBitstreamBufferPool = VulkanVideoRefCountedPool<VulkanBitstreamBufferImpl, 64>;

class NvVkDecodeFrameData {
public:
	NvVkDecodeFrameData(const DeviceInterface& vkd, VkDevice device, uint32_t decodeQueueIdx)
			: m_deviceInterface(vkd),
			m_device(device),
			m_decodeQueueIdx(decodeQueueIdx),
			m_videoCommandPool(),
			m_bitstreamBuffersQueue() {}

	void deinit() {

		if (!!m_videoCommandPool) {
			m_deviceInterface.freeCommandBuffers(m_device, m_videoCommandPool, (uint32_t)m_commandBuffers.size(), &m_commandBuffers[0]);
			m_deviceInterface.destroyCommandPool(m_device, m_videoCommandPool, NULL);
			m_videoCommandPool = VkCommandPool();
		}
	}

	~NvVkDecodeFrameData() {
		deinit();
	}

	size_t resize(size_t maxDecodeFramesCount) {
		size_t allocatedCommandBuffers = 0;
		if (!m_videoCommandPool) {
			VkCommandPoolCreateInfo cmdPoolInfo = {};
			cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			cmdPoolInfo.queueFamilyIndex = m_decodeQueueIdx;
			VkResult result = m_deviceInterface.createCommandPool(m_device, &cmdPoolInfo, nullptr, &m_videoCommandPool);
			assert(result == VK_SUCCESS);
			if (result != VK_SUCCESS) {
				fprintf(stderr, "\nERROR: CreateCommandPool() result: 0x%x\n", result);
			}

			VkCommandBufferAllocateInfo cmdInfo = {};
			cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cmdInfo.commandBufferCount = (uint32_t)maxDecodeFramesCount;
			cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cmdInfo.commandPool = m_videoCommandPool;

			m_commandBuffers.resize(maxDecodeFramesCount);
			result = m_deviceInterface.allocateCommandBuffers(m_device, &cmdInfo, &m_commandBuffers[0]);
			assert(result == VK_SUCCESS);
			if (result != VK_SUCCESS) {
				fprintf(stderr, "\nERROR: AllocateCommandBuffers() result: 0x%x\n", result);
			} else {
				allocatedCommandBuffers = maxDecodeFramesCount;
			}
		} else {
			allocatedCommandBuffers = m_commandBuffers.size();
			assert(maxDecodeFramesCount <= allocatedCommandBuffers);
		}

		return allocatedCommandBuffers;
	}

	VkCommandBuffer GetCommandBuffer(uint32_t slot) {
		assert(slot < m_commandBuffers.size());
		return m_commandBuffers[slot];
	}

	size_t size() {
		return m_commandBuffers.size();
	}

	VulkanBitstreamBufferPool& GetBitstreamBuffersQueue() { return m_bitstreamBuffersQueue; }

private:
	const DeviceInterface&                                m_deviceInterface;
	VkDevice m_device;
	uint32_t m_decodeQueueIdx;
	VkCommandPool                                             m_videoCommandPool;
	std::vector<VkCommandBuffer>                              m_commandBuffers;
	VulkanBitstreamBufferPool                                 m_bitstreamBuffersQueue;
};

class VulkanVideoFrameBuffer : public IVulkanVideoFrameBufferParserCb {
public:
	// Synchronization
	struct FrameSynchronizationInfo {
		VkFence frameCompleteFence;
		VkSemaphore frameCompleteSemaphore;
		VkFence frameConsumerDoneFence;
		VkSemaphore frameConsumerDoneSemaphore;
		VkQueryPool queryPool;
		int32_t startQueryId;
		uint32_t numQueries;
		uint32_t hasFrameCompleteSignalFence : 1;
		uint32_t hasFrameCompleteSignalSemaphore : 1;
	};

	struct ReferencedObjectsInfo {

		// The bitstream Buffer
		const VkVideoRefCountBase*     pBitstreamData;
		// PPS
		const VkVideoRefCountBase*     pStdPps;
		// SPS
		const VkVideoRefCountBase*     pStdSps;
		// VPS
		const VkVideoRefCountBase*     pStdVps;

		ReferencedObjectsInfo(const VkVideoRefCountBase* pBitstreamDataRef,
							  const VkVideoRefCountBase* pStdPpsRef,
							  const VkVideoRefCountBase* pStdSpsRef,
							  const VkVideoRefCountBase* pStdVpsRef = nullptr)
				: pBitstreamData(pBitstreamDataRef)
				  , pStdPps(pStdPpsRef)
				  , pStdSps(pStdSpsRef)
				  , pStdVps(pStdVpsRef) {}
	};

	struct PictureResourceInfo {
		VkImage  image;
		VkFormat imageFormat;
		VkImageLayout currentImageLayout;
	};

	virtual int32_t InitImagePool(const VkVideoProfileInfoKHR* pDecodeProfile,
								  uint32_t                 numImages,
								  VkFormat                 dpbImageFormat,
								  VkFormat                 outImageFormat,
								  const VkExtent2D&        codedExtent,
								  const VkExtent2D&        maxImageExtent,
								  VkImageUsageFlags        dpbImageUsage,
								  VkImageUsageFlags        outImageUsage,
								  uint32_t                 queueFamilyIndex,
								  bool                     useImageArray = false,
								  bool                     useImageViewArray = false,
								  bool                     useSeparateOutputImage = false,
								  bool                     useLinearOutput = false) = 0;

	virtual int32_t QueuePictureForDecode(int8_t picId, VkParserDecodePictureInfo* pDecodePictureInfo,
										  ReferencedObjectsInfo* pReferencedObjectsInfo,
										  FrameSynchronizationInfo* pFrameSynchronizationInfo) = 0;
	virtual int32_t DequeueDecodedPicture(DecodedFrame* pDecodedFrame) = 0;
	virtual int32_t ReleaseDisplayedPicture(DecodedFrameRelease** pDecodedFramesRelease, uint32_t numFramesToRelease) = 0;
	virtual int32_t GetDpbImageResourcesByIndex(uint32_t numResources, const int8_t* referenceSlotIndexes,
												VkVideoPictureResourceInfoKHR* pictureResources,
												PictureResourceInfo* pictureResourcesInfo,
												VkImageLayout newDpbImageLayerLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR) = 0;
	virtual int32_t GetCurrentImageResourceByIndex(int8_t referenceSlotIndex,
												   VkVideoPictureResourceInfoKHR* dpbPictureResource,
												   PictureResourceInfo* dpbPictureResourceInfo,
												   VkImageLayout newDpbImageLayerLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
												   VkVideoPictureResourceInfoKHR* outputPictureResource = nullptr,
												   PictureResourceInfo* outputPictureResourceInfo = nullptr,
												   VkImageLayout newOutputImageLayerLayout = VK_IMAGE_LAYOUT_MAX_ENUM) = 0;
	virtual int32_t ReleaseImageResources(uint32_t numResources, const uint32_t* indexes) = 0;
	virtual int32_t SetPicNumInDecodeOrder(int32_t picId, int32_t picNumInDecodeOrder) = 0;
	virtual int32_t SetPicNumInDisplayOrder(int32_t picId, int32_t picNumInDisplayOrder) = 0;
	virtual size_t GetSize() = 0;
	virtual size_t GetDisplayedFrameCount() const = 0;

	virtual ~VulkanVideoFrameBuffer() { }

	static VkResult Create(DeviceContext* devCtx,
						   VkSharedBaseObj<VulkanVideoFrameBuffer>& vkVideoFrameBuffer);
};

struct NvVkDecodeFrameDataSlot {
	uint32_t                                            slot;
	VkCommandBuffer                                     commandBuffer;
};

class VideoBaseDecoder final : public VkParserVideoDecodeClient
{
	enum
	{
		MAX_FRM_CNT = 32
	};

public:
	VideoBaseDecoder(DeviceContext*							  context,
					 const VkVideoCoreProfile&				  profile,
					 size_t									  framesToCheck,
					 VkSharedBaseObj<VulkanVideoFrameBuffer>& videoFrameBuffer);
	~VideoBaseDecoder()
	{
		Deinitialize();
	}

	int32_t					ReleaseDisplayedFrame(DecodedFrame* pDisplayedFrame);
	VulkanVideoFrameBuffer* GetVideoFrameBuffer()
	{
		return m_videoFrameBuffer.Get();
	}
	const VkVideoCapabilitiesKHR* getVideoCaps() const { return &m_videoCaps; }

private:
	// VkParserVideoDecodeClient callbacks
	// Returns max number of reference frames (always at least 2 for MPEG-2)
	int32_t			BeginSequence(const VkParserSequenceInfo* pnvsi) override;
	// Returns a new INvidiaVulkanPicture interface
	bool			AllocPictureBuffer(VkPicIf** ppNvidiaVulkanPicture) override;
	// Called when a picture is ready to be decoded
	bool			DecodePicture(VkParserPictureData* pNvidiaVulkanParserPictureData) override;
	// Called when the stream parameters have changed
	bool			UpdatePictureParameters(VkSharedBaseObj<StdVideoPictureParametersSet>& pictureParametersObject, /* in */
											VkSharedBaseObj<VkVideoRefCountBase>&		   client /* out */) override;
	// Called when a picture is ready to be displayed
	bool			DisplayPicture(VkPicIf* pNvidiaVulkanPicture, int64_t llPTS) override;
	// Called for custom NAL parsing (not required)
	void			UnhandledNALU(const uint8_t* pbData, size_t cbData) override;

	// Parser methods
	bool			DecodePicture(VkParserPictureData* pParserPictureData, vkPicBuffBase* pVkPicBuff, VkParserDecodePictureInfo*);
	uint32_t		FillDpbH264State(const VkParserPictureData*			pd,
									 const VkParserH264DpbEntry*		dpbIn,
									 uint32_t							maxDpbInSlotsInUse,
									 nvVideoDecodeH264DpbSlotInfo*		pDpbRefList,
									 uint32_t							maxRefPictures,
									 VkVideoReferenceSlotInfoKHR*		pReferenceSlots,
									 int8_t*							pGopReferenceImagesIndexes,
									 StdVideoDecodeH264PictureInfoFlags currPicFlags,
									 int32_t*							pCurrAllocatedSlotIndex);
	uint32_t		FillDpbH265State(const VkParserPictureData*		pd,
									 const VkParserHevcPictureData* pin,
									 nvVideoDecodeH265DpbSlotInfo*	pDpbSlotInfo,
									 StdVideoDecodeH265PictureInfo* pStdPictureInfo,
									 uint32_t						maxRefPictures,
									 VkVideoReferenceSlotInfoKHR*	pReferenceSlots,
									 int8_t*						pGopReferenceImagesIndexes,
									 int32_t*						pCurrAllocatedSlotIndex);

	int8_t			AllocateDpbSlotForCurrentH264(vkPicBuffBase*					 pPic,
												  StdVideoDecodeH264PictureInfoFlags currPicFlags,
												  int8_t							 presetDpbSlot);
	int8_t			AllocateDpbSlotForCurrentH265(vkPicBuffBase* pPic, bool isReference, int8_t presetDpbSlot);
	int8_t			GetPicIdx(vkPicBuffBase* pNvidiaVulkanPictureBase);
	int8_t			GetPicIdx(VkPicIf* pNvidiaVulkanPicture);
	int8_t			GetPicDpbSlot(int8_t picIndex);
	int8_t			SetPicDpbSlot(int8_t picIndex, int8_t dpbSlot);
	uint32_t		ResetPicDpbSlots(uint32_t picIndexSlotValidMask);
	bool			GetFieldPicFlag(int8_t picIndex);
	bool			SetFieldPicFlag(int8_t picIndex, bool fieldPicFlag);

	// Client callbacks
	virtual int32_t StartVideoSequence(const VkParserDetectedVideoFormat* pVideoFormat);
	virtual int32_t DecodePictureWithParameters(VkParserPerFrameDecodeParameters* pPicParams,
												VkParserDecodePictureInfo*		  pDecodePictureInfo);

	VkDeviceSize	GetBitstreamBuffer(VkDeviceSize							   size,
									   VkDeviceSize							   minBitstreamBufferOffsetAlignment,
									   VkDeviceSize							   minBitstreamBufferSizeAlignment,
									   const uint8_t*						   pInitializeBufferMemory,
									   VkDeviceSize							   initializeBufferMemorySize,
									   VkSharedBaseObj<VulkanBitstreamBuffer>& bitstreamBuffer) override;

	// Client methods
	void	Deinitialize();
	int32_t			GetCurrentFrameData(uint32_t slotId, NvVkDecodeFrameDataSlot& frameDataSlot)
	{
		if (slotId < m_decodeFramesData.size())
		{
			frameDataSlot.commandBuffer = m_decodeFramesData.GetCommandBuffer(slotId);
			frameDataSlot.slot			= slotId;
			return slotId;
		}
		return -1;
	}

	DeviceContext*												m_deviceContext{};
	VkVideoCoreProfile											m_profile{};
	// Parser fields
	int32_t														m_nCurrentPictureID{};
	uint32_t													m_dpbSlotsMask{};
	uint32_t													m_fieldPicFlagMask{};
	DpbSlots													m_dpb;
	std::array<int8_t, MAX_FRM_CNT>								m_pictureToDpbSlotMap;
	VkFormat													m_dpbImageFormat{VK_FORMAT_UNDEFINED};
	VkFormat													m_outImageFormat{VK_FORMAT_UNDEFINED};
	uint32_t													m_maxNumDecodeSurfaces{1};
	uint32_t													m_maxNumDpbSlots{1};
	vector<AllocationPtr>										m_videoDecodeSessionAllocs;
	uint32_t													m_numDecodeSurfaces{};
	Move<VkCommandPool>											m_videoCommandPool{};
	VkVideoCapabilitiesKHR										m_videoCaps{};
	VkVideoDecodeCapabilitiesKHR								m_decodeCaps{};
	VkVideoCodecOperationFlagsKHR								m_supportedVideoCodecs{};
	inline bool dpbAndOutputCoincide() const { return m_decodeCaps.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR; }

	VkSharedBaseObj<VulkanVideoSession>							m_videoSession{};
	VkSharedBaseObj<VulkanVideoFrameBuffer>						m_videoFrameBuffer{};
	NvVkDecodeFrameData											m_decodeFramesData;
	uint32_t													m_maxDecodeFramesCount{};
	int32_t														m_decodePicCount{};
	VkParserDetectedVideoFormat									m_videoFormat{};
	VkSharedBaseObj<VkParserVideoPictureParameters>				m_currentPictureParameters{};
	bool														m_queryResultWithStatus{false};

	vector<VkParserPerFrameDecodeParameters*>					m_pPerFrameDecodeParameters;
	vector<VkParserDecodePictureInfo*>							m_pVulkanParserDecodePictureInfo;
	vector<NvVkDecodeFrameData*>								m_pFrameDatas;
	vector<VkBufferMemoryBarrier2KHR>							m_bitstreamBufferMemoryBarriers;
	vector<vector<VkImageMemoryBarrier2KHR>>					m_imageBarriersVec;
	vector<VulkanVideoFrameBuffer::FrameSynchronizationInfo>	m_frameSynchronizationInfos;
	vector<VkCommandBufferSubmitInfoKHR>						m_commandBufferSubmitInfos;
	vector<VkVideoBeginCodingInfoKHR>							m_decodeBeginInfos;
	vector<vector<VulkanVideoFrameBuffer::PictureResourceInfo>> m_pictureResourcesInfos;
	vector<VkDependencyInfoKHR>									m_dependencyInfos;
	vector<VkVideoEndCodingInfoKHR>								m_decodeEndInfos;
	vector<VkSubmitInfo2KHR>									m_submitInfos;
	vector<VkFence>												m_frameCompleteFences;
	vector<VkFence>												m_frameConsumerDoneFences;
	vector<VkSemaphoreSubmitInfoKHR>							m_frameCompleteSemaphoreSubmitInfos;
	vector<VkSemaphoreSubmitInfoKHR>							m_frameConsumerDoneSemaphoreSubmitInfos;
	VkParserSequenceInfo										m_nvsi{};
	uint32_t													m_maxStreamBufferSize{};
	uint32_t													m_numBitstreamBuffersToPreallocate{8}; // TODO: Review
	bool														m_useImageArray{false};
	bool														m_useImageViewArray{false};
	bool														m_useSeparateOutputImages{false};
	bool														m_resetDecoder{false};
};

de::MovePtr<vkt::ycbcr::MultiPlaneImageData> getDecodedImage (const DeviceInterface&	vkd,
															  VkDevice					device,
															  Allocator&				allocator,
															  VkImage					image,
															  VkImageLayout				layout,
															  VkFormat					format,
															  VkExtent2D				codedExtent,
															  deUint32					queueFamilyIndexTransfer,
															  deUint32					queueFamilyIndexDecode);

} // video
} // vkt

#endif // _VKTVIDEOBASEDECODEUTILS_HPP
