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
 *	  http://www.apache.org/licenses/LICENSE-2.0
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

#include "vktVideoBaseDecodeUtils.hpp"

#include "tcuPlatform.hpp"
#include "vkDefs.hpp"
#include "vkRef.hpp"
#include "vkBarrierUtil.hpp"
#include "vkQueryUtil.hpp"
#include "vkMemUtil.hpp"
#include "vkCmdUtil.hpp"
#include "vkObjUtil.hpp"
#include "vkTypeUtil.hpp"
#include "vkStrUtil.hpp"
#include "deRandom.hpp"

#include <iostream>

namespace vkt
{
namespace video
{
using namespace vk;
using namespace std;
using de::MovePtr;
using de::SharedPtr;

static const uint32_t topFieldShift		   = 0;
static const uint32_t topFieldMask		   = (1 << topFieldShift);
static const uint32_t bottomFieldShift	   = 1;
static const uint32_t bottomFieldMask	   = (1 << bottomFieldShift);
static const uint32_t fieldIsReferenceMask = (topFieldMask | bottomFieldMask);

#define HEVC_MAX_DPB_SLOTS 16
#define AVC_MAX_DPB_SLOTS 17

#define NVIDIA_FRAME_RATE_NUM(rate) ((rate) >> 14)
#define NVIDIA_FRAME_RATE_DEN(rate) ((rate)&0x3fff)

bool videoLoggingEnabled()
{
    static int debuggingEnabled = -1; // -1 means it hasn't been checked yet
    if (debuggingEnabled == -1) {
        const char* s = getenv("CTS_DEBUG_VIDEO");
		debuggingEnabled = s != nullptr;
    }

    return debuggingEnabled > 0;
}

struct nvVideoH264PicParameters {
	enum { MAX_REF_PICTURES_LIST_ENTRIES = 16 };

	StdVideoDecodeH264PictureInfo stdPictureInfo;
	VkVideoDecodeH264PictureInfoKHR pictureInfo;
	VkVideoDecodeH264SessionParametersAddInfoKHR pictureParameters;
	nvVideoDecodeH264DpbSlotInfo currentDpbSlotInfo;
	nvVideoDecodeH264DpbSlotInfo dpbRefList[MAX_REF_PICTURES_LIST_ENTRIES];
};

/*******************************************************/
//! \struct nvVideoH265PicParameters
//! HEVC picture parameters
/*******************************************************/
struct nvVideoH265PicParameters {
	enum { MAX_REF_PICTURES_LIST_ENTRIES = 16 };

	StdVideoDecodeH265PictureInfo stdPictureInfo;
	VkVideoDecodeH265PictureInfoKHR pictureInfo;
	VkVideoDecodeH265SessionParametersAddInfoKHR pictureParameters;
	nvVideoDecodeH265DpbSlotInfo dpbRefList[MAX_REF_PICTURES_LIST_ENTRIES];
};

inline vkPicBuffBase *GetPic(VkPicIf *pPicBuf)
{
	return (vkPicBuffBase *) pPicBuf;
}

inline VkVideoChromaSubsamplingFlagBitsKHR ConvertStdH264ChromaFormatToVulkan(StdVideoH264ChromaFormatIdc stdFormat)
{
	switch (stdFormat)
	{
		case STD_VIDEO_H264_CHROMA_FORMAT_IDC_420:
			return VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
		case STD_VIDEO_H264_CHROMA_FORMAT_IDC_422:
			return VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
		case STD_VIDEO_H264_CHROMA_FORMAT_IDC_444:
			return VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;
		default:
			TCU_THROW(InternalError, "Invalid chroma sub-sampling format");
	}
}

VkFormat codecGetVkFormat(VkVideoChromaSubsamplingFlagBitsKHR chromaFormatIdc,
						  int bitDepthLuma,
						  bool isSemiPlanar)
{
	switch (chromaFormatIdc)
	{
		case VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR:
		{
			switch (bitDepthLuma)
			{
				case VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR:
					return VK_FORMAT_R8_UNORM;
				case VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR:
					return VK_FORMAT_R10X6_UNORM_PACK16;
				case VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR:
					return VK_FORMAT_R12X4_UNORM_PACK16;
				default:
					TCU_THROW(InternalError, "Cannot map monochrome format to VkFormat");
			}
		}
		case VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR:
		{
			switch (bitDepthLuma)
			{
				case VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G8_B8R8_2PLANE_420_UNORM : VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
				case VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 : VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16);
				case VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16 : VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16);
				default:
					TCU_THROW(InternalError, "Cannot map 420 format to VkFormat");
			}
		}
		case VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR:
		{
			switch (bitDepthLuma)
			{
				case VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G8_B8R8_2PLANE_422_UNORM : VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM);
				case VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16 : VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16);
				case VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16 : VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16);
				default:
					TCU_THROW(InternalError, "Cannot map 422 format to VkFormat");
			}
		}
		case VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR:
		{
			switch (bitDepthLuma)
			{
				case VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT : VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM);
				case VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT : VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16);
				case VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR:
					return (isSemiPlanar ? VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT : VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16);
				default:
					TCU_THROW(InternalError, "Cannot map 444 format to VkFormat");
			}
		}
		default:
			TCU_THROW(InternalError, "Unknown input idc format");
	}
}

VkVideoComponentBitDepthFlagsKHR getLumaBitDepth(deUint8 lumaBitDepthMinus8)
{
	switch (lumaBitDepthMinus8)
	{
		case 0:
			return VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		case 2:
			return VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
		case 4:
			return VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
		default:
			TCU_THROW(InternalError, "Unhandler lumaBitDepthMinus8");
	}
}

VkVideoComponentBitDepthFlagsKHR getChromaBitDepth(deUint8 chromaBitDepthMinus8)
{
	switch (chromaBitDepthMinus8)
	{
		case 0:
			return VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		case 2:
			return VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
		case 4:
			return VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
		default:
			TCU_THROW(InternalError, "Unhandler chromaBitDepthMinus8");
	}
}

void setImageLayout(const DeviceInterface &vkd,
					VkCommandBuffer cmdBuffer,
					VkImage image,
					VkImageLayout oldImageLayout,
					VkImageLayout newImageLayout,
					VkPipelineStageFlags2KHR srcStages,
					VkPipelineStageFlags2KHR dstStages,
					VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT)
{
	VkAccessFlags2KHR srcAccessMask = 0;
	VkAccessFlags2KHR dstAccessMask = 0;

	switch (static_cast<VkImageLayout>(oldImageLayout))
	{
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR:
			srcAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
			break;
		default:
			srcAccessMask = 0;
			break;
	}

	switch (static_cast<VkImageLayout>(newImageLayout))
	{
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR:
			dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
			break;
		case VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR:
			dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR;
			break;
		case VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR:
			dstAccessMask = VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR;
			break;
		case VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR:
			dstAccessMask = VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR | VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR;
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
			dstAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			break;
		default:
			dstAccessMask = 0;
			break;
	}

	const VkImageMemoryBarrier2KHR imageMemoryBarrier =
			{
					VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,    //  VkStructureType				sType;
					DE_NULL,                                        //  const void*					pNext;
					srcStages,                                        //  VkPipelineStageFlags2KHR	srcStageMask;
					srcAccessMask,                                    //  VkAccessFlags2KHR			srcAccessMask;
					dstStages,                                        //  VkPipelineStageFlags2KHR	dstStageMask;
					dstAccessMask,                                    //  VkAccessFlags2KHR			dstAccessMask;
					oldImageLayout,                                    //  VkImageLayout				oldLayout;
					newImageLayout,                                    //  VkImageLayout				newLayout;
					VK_QUEUE_FAMILY_IGNORED,                        //  deUint32					srcQueueFamilyIndex;
					VK_QUEUE_FAMILY_IGNORED,                        //  deUint32					dstQueueFamilyIndex;
					image,                                            //  VkImage						image;
					{aspectMask, 0, 1, 0, 1},                        //  VkImageSubresourceRange		subresourceRange;
			};

	const VkDependencyInfoKHR dependencyInfo =
			{
					VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,    //  VkStructureType						sType;
					DE_NULL,                                //  const void*							pNext;
					VK_DEPENDENCY_BY_REGION_BIT,            //  VkDependencyFlags					dependencyFlags;
					0,                                        //  deUint32							memoryBarrierCount;
					DE_NULL,                                //  const VkMemoryBarrier2KHR*			pMemoryBarriers;
					0,                                        //  deUint32							bufferMemoryBarrierCount;
					DE_NULL,                                //  const VkBufferMemoryBarrier2KHR*	pBufferMemoryBarriers;
					1,                                        //  deUint32							imageMemoryBarrierCount;
					&imageMemoryBarrier,                    //  const VkImageMemoryBarrier2KHR*		pImageMemoryBarriers;
			};

	vkd.cmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
}

typedef struct dpbH264Entry {
	int8_t dpbSlot;
	// bit0(used_for_reference)=1: top field used for reference,
	// bit1(used_for_reference)=1: bottom field used for reference
	uint32_t used_for_reference : 2;
	uint32_t is_long_term : 1; // 0 = short-term, 1 = long-term
	uint32_t is_non_existing : 1; // 1 = marked as non-existing
	uint32_t is_field_ref : 1; // set if unpaired field or complementary field pair
	union {
		int16_t FieldOrderCnt[2]; // h.264 : 2*32 [top/bottom].
		int32_t PicOrderCnt; // HEVC PicOrderCnt
	};
	union {
		int16_t FrameIdx; // : 16   short-term: FrameNum (16 bits), long-term:
		// LongTermFrameIdx (4 bits)
		int8_t originalDpbIndex; // Original Dpb source Index.
	};
	vkPicBuffBase* m_picBuff; // internal picture reference

	void setReferenceAndTopBottomField(
			bool isReference, bool nonExisting, bool isLongTerm, bool isFieldRef,
			bool topFieldIsReference, bool bottomFieldIsReference, int16_t frameIdx,
			const int16_t fieldOrderCntList[2], vkPicBuffBase* picBuff)
	{
		is_non_existing = nonExisting;
		is_long_term = isLongTerm;
		is_field_ref = isFieldRef;
		if (isReference && isFieldRef) {
			used_for_reference = (bottomFieldIsReference << bottomFieldShift) | (topFieldIsReference << topFieldShift);
		} else {
			used_for_reference = isReference ? 3 : 0;
		}

		FrameIdx = frameIdx;

		FieldOrderCnt[0] = fieldOrderCntList[used_for_reference == 2]; // 0: for progressive and top reference; 1: for
		// bottom reference only.
		FieldOrderCnt[1] = fieldOrderCntList[used_for_reference != 1]; // 0: for top reference only;  1: for bottom
		// reference and progressive.

		dpbSlot = -1;
		m_picBuff = picBuff;
	}

	void setReference(bool isLongTerm, int32_t picOrderCnt,
					  vkPicBuffBase* picBuff)
	{
		is_non_existing = (picBuff == NULL);
		is_long_term = isLongTerm;
		is_field_ref = false;
		used_for_reference = (picBuff != NULL) ? 3 : 0;

		PicOrderCnt = picOrderCnt;

		dpbSlot = -1;
		m_picBuff = picBuff;
		originalDpbIndex = -1;
	}

	bool isRef() { return (used_for_reference != 0); }

	StdVideoDecodeH264ReferenceInfoFlags getPictureFlag(bool currentPictureIsProgressive)
	{
		StdVideoDecodeH264ReferenceInfoFlags picFlags = StdVideoDecodeH264ReferenceInfoFlags();
		if (videoLoggingEnabled())
			std::cout << "\t\t Flags: ";

		if (used_for_reference) {
			if (videoLoggingEnabled())
				std::cout << "FRAME_IS_REFERENCE ";
			// picFlags.is_reference = true;
		}

		if (is_long_term) {
			if (videoLoggingEnabled())
				std::cout << "IS_LONG_TERM ";
			picFlags.used_for_long_term_reference = true;
		}
		if (is_non_existing) {
			if (videoLoggingEnabled())
				std::cout << "IS_NON_EXISTING ";
			picFlags.is_non_existing = true;
		}

		if (is_field_ref) {
			if (videoLoggingEnabled())
				std::cout << "IS_FIELD ";
			// picFlags.field_pic_flag = true;
		}

		if (!currentPictureIsProgressive && (used_for_reference & topFieldMask)) {
			if (videoLoggingEnabled())
				std::cout << "TOP_FIELD_IS_REF ";
			picFlags.top_field_flag = true;
		}
		if (!currentPictureIsProgressive && (used_for_reference & bottomFieldMask)) {
			if (videoLoggingEnabled())
				std::cout << "BOTTOM_FIELD_IS_REF ";
			picFlags.bottom_field_flag = true;
		}

		return picFlags;
	}

	void setH264PictureData(nvVideoDecodeH264DpbSlotInfo* pDpbRefList,
							VkVideoReferenceSlotInfoKHR* pReferenceSlots,
							uint32_t dpbEntryIdx, uint32_t dpbSlotIndex,
							bool currentPictureIsProgressive)
	{
		assert(dpbEntryIdx < AVC_MAX_DPB_SLOTS);
		assert(dpbSlotIndex < AVC_MAX_DPB_SLOTS);

		assert((dpbSlotIndex == (uint32_t)dpbSlot) || is_non_existing);
		pReferenceSlots[dpbEntryIdx].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
		pReferenceSlots[dpbEntryIdx].slotIndex = dpbSlotIndex;
		pReferenceSlots[dpbEntryIdx].pNext = pDpbRefList[dpbEntryIdx].Init(dpbSlotIndex);

		StdVideoDecodeH264ReferenceInfo* pRefPicInfo = &pDpbRefList[dpbEntryIdx].stdReferenceInfo;
		pRefPicInfo->FrameNum = FrameIdx;
		if (videoLoggingEnabled()) {
			std::cout << "\tdpbEntryIdx: " << dpbEntryIdx
					  << "dpbSlotIndex: " << dpbSlotIndex
					  << " FrameIdx: " << (int32_t)FrameIdx;
		}
		pRefPicInfo->flags = getPictureFlag(currentPictureIsProgressive);
		pRefPicInfo->PicOrderCnt[0] = FieldOrderCnt[0];
		pRefPicInfo->PicOrderCnt[1] = FieldOrderCnt[1];
		if (videoLoggingEnabled())
			std::cout << " fieldOrderCnt[0]: " << pRefPicInfo->PicOrderCnt[0]
					  << " fieldOrderCnt[1]: " << pRefPicInfo->PicOrderCnt[1]
					  << std::endl;
	}

	void setH265PictureData(nvVideoDecodeH265DpbSlotInfo* pDpbSlotInfo,
							VkVideoReferenceSlotInfoKHR* pReferenceSlots,
							uint32_t dpbEntryIdx, uint32_t dpbSlotIndex)
	{
		assert(dpbEntryIdx < HEVC_MAX_DPB_SLOTS);
		assert(dpbSlotIndex < HEVC_MAX_DPB_SLOTS);
		assert(isRef());

		assert((dpbSlotIndex == (uint32_t)dpbSlot) || is_non_existing);
		pReferenceSlots[dpbEntryIdx].sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
		pReferenceSlots[dpbEntryIdx].slotIndex = dpbSlotIndex;
		pReferenceSlots[dpbEntryIdx].pNext = pDpbSlotInfo[dpbEntryIdx].Init(dpbSlotIndex);

		StdVideoDecodeH265ReferenceInfo* pRefPicInfo = &pDpbSlotInfo[dpbEntryIdx].stdReferenceInfo;
		pRefPicInfo->PicOrderCntVal = PicOrderCnt;
		pRefPicInfo->flags.used_for_long_term_reference = is_long_term;

		if (videoLoggingEnabled()) {
			std::cout << "\tdpbIndex: " << dpbSlotIndex
					  << " picOrderCntValList: " << PicOrderCnt;

			std::cout << "\t\t Flags: ";
			std::cout << "FRAME IS REFERENCE ";
			if (pRefPicInfo->flags.used_for_long_term_reference) {
				std::cout << "IS LONG TERM ";
			}
			std::cout << std::endl;
		}
	}

} dpbH264Entry;

int8_t VideoBaseDecoder::GetPicIdx(vkPicBuffBase *pPicBuf)
{
	if (pPicBuf)
	{
		int32_t picIndex = pPicBuf->m_picIdx;

		if ((picIndex >= 0) && ((uint32_t) picIndex < m_maxNumDecodeSurfaces))
		{
			return (int8_t) picIndex;
		}
	}

	return -1;
}

int8_t VideoBaseDecoder::GetPicIdx(VkPicIf *pPicBuf)
{
	return GetPicIdx(GetPic(pPicBuf));
}

int8_t VideoBaseDecoder::GetPicDpbSlot(int8_t picIndex)
{
	return m_pictureToDpbSlotMap[picIndex];
}

bool VideoBaseDecoder::GetFieldPicFlag(int8_t picIndex)
{
	DE_ASSERT((picIndex >= 0) && ((uint32_t) picIndex < m_maxNumDecodeSurfaces));

	return !!(m_fieldPicFlagMask & (1 << (uint32_t) picIndex));
}

bool VideoBaseDecoder::SetFieldPicFlag(int8_t picIndex, bool fieldPicFlag)
{
	DE_ASSERT((picIndex >= 0) && ((uint32_t) picIndex < m_maxNumDecodeSurfaces));

	bool oldFieldPicFlag = GetFieldPicFlag(picIndex);

	if (fieldPicFlag)
	{
		m_fieldPicFlagMask |= (1 << (uint32_t) picIndex);
	}
	else
	{
		m_fieldPicFlagMask &= ~(1 << (uint32_t) picIndex);
	}

	return oldFieldPicFlag;
}

int8_t VideoBaseDecoder::SetPicDpbSlot(int8_t picIndex, int8_t dpbSlot)
{
	int8_t oldDpbSlot = m_pictureToDpbSlotMap[picIndex];

	m_pictureToDpbSlotMap[picIndex] = dpbSlot;

	if (dpbSlot >= 0)
	{
		m_dpbSlotsMask |= (1 << picIndex);
	}
	else
	{
		m_dpbSlotsMask &= ~(1 << picIndex);

		if (oldDpbSlot >= 0)
		{
			m_dpb.FreeSlot(oldDpbSlot);
		}
	}

	return oldDpbSlot;
}

uint32_t VideoBaseDecoder::ResetPicDpbSlots(uint32_t picIndexSlotValidMask)
{
	uint32_t resetSlotsMask = ~(picIndexSlotValidMask | ~m_dpbSlotsMask);

	for (uint32_t picIdx = 0; (picIdx < m_maxNumDecodeSurfaces) && resetSlotsMask; picIdx++)
	{
		if (resetSlotsMask & (1 << picIdx))
		{
			resetSlotsMask &= ~(1 << picIdx);

			SetPicDpbSlot((int8_t) picIdx, -1);
		}
	}

	return m_dpbSlotsMask;
}

VideoBaseDecoder::VideoBaseDecoder (Parameters&& params)
	: m_deviceContext							(params.context)
	, m_profile									(*params.profile)
	, m_dpb										(3)
	, m_videoFrameBuffer						(params.framebuffer)
	// TODO: interface cleanup
	, m_decodeFramesData						(params.context->getDeviceDriver(), params.context->device, params.context->decodeQueueFamilyIdx())
	, m_queryResultWithStatus					(params.queryDecodeStatus)
{
	std::fill(m_pictureToDpbSlotMap.begin(), m_pictureToDpbSlotMap.end(), -1);

	VK_CHECK(util::getVideoDecodeCapabilities(*m_deviceContext, *params.profile, m_videoCaps, m_decodeCaps));

	VK_CHECK(util::getSupportedVideoFormats(*m_deviceContext, m_profile, m_decodeCaps.flags,
											 m_outImageFormat,
											 m_dpbImageFormat));

	m_supportedVideoCodecs = util::getSupportedCodecs(*m_deviceContext,
													  m_deviceContext->decodeQueueFamilyIdx(),
													 VK_QUEUE_VIDEO_DECODE_BIT_KHR,
													  VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR | VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR);
	DE_ASSERT(m_supportedVideoCodecs != VK_VIDEO_CODEC_OPERATION_NONE_KHR);
}

void VideoBaseDecoder::Deinitialize ()
{
	const DeviceInterface&	vkd					= m_deviceContext->getDeviceDriver();
	const VkDevice			device				= m_deviceContext->device;
	const VkQueue			queueDecode			= m_deviceContext->decodeQueue;
	const VkQueue			queueTransfer		= m_deviceContext->transferQueue;

	if (queueDecode)
		vkd.queueWaitIdle(queueDecode);

	if (queueTransfer)
		vkd.queueWaitIdle(queueTransfer);

	vkd.deviceWaitIdle(device);

	m_dpb.Deinit();
	m_videoFrameBuffer = nullptr;
	m_decodeFramesData.deinit();
	m_videoSession = nullptr;
}

int32_t VideoBaseDecoder::StartVideoSequence (const VkParserDetectedVideoFormat* pVideoFormat)
{
	VkExtent2D codedExtent = { pVideoFormat->coded_width, pVideoFormat->coded_height };

	// Width and height of the image surface
	VkExtent2D imageExtent = VkExtent2D { std::max((uint32_t)(pVideoFormat->display_area.right  - pVideoFormat->display_area.left), pVideoFormat->coded_width),
										  std::max((uint32_t)(pVideoFormat->display_area.bottom - pVideoFormat->display_area.top),  pVideoFormat->coded_height) };

	std::cout << "Video Input Information" << std::endl
			  << "\tCodec        : " << util::getVideoCodecString(pVideoFormat->codec) << std::endl
			  << "\tFrame rate   : " << pVideoFormat->frame_rate.numerator << "/" << pVideoFormat->frame_rate.denominator << " = "
			  << ((pVideoFormat->frame_rate.denominator != 0) ? (1.0 * pVideoFormat->frame_rate.numerator / pVideoFormat->frame_rate.denominator) : 0.0) << " fps" << std::endl
			  << "\tSequence     : " << (pVideoFormat->progressive_sequence ? "Progressive" : "Interlaced") << std::endl
			  << "\tCoded size   : [" << codedExtent.width << ", " << codedExtent.height << "]" << std::endl
			  << "\tDisplay area : [" << pVideoFormat->display_area.left << ", " << pVideoFormat->display_area.top << ", "
			  << pVideoFormat->display_area.right << ", " << pVideoFormat->display_area.bottom << "]" << std::endl
			  << "\tChroma       : " << util::getVideoChromaFormatString(pVideoFormat->chromaSubsampling) << std::endl
			  << "\tBit depth    : " << pVideoFormat->bit_depth_luma_minus8 + 8 << std::endl;

	m_numDecodeSurfaces = std::max(m_numDecodeSurfaces, pVideoFormat->minNumDecodeSurfaces + 8);
	VkResult result = VK_SUCCESS;

	if (videoLoggingEnabled()) {
		std::cout << "\t" << std::hex << m_supportedVideoCodecs << " HW codec types are available: " << std::dec << std::endl;
	}

	VkVideoCodecOperationFlagBitsKHR detectedVideoCodec = pVideoFormat->codec;

	if (videoLoggingEnabled()) {
		std::cout << "\tcodec " << VkVideoCoreProfile::CodecToName(detectedVideoCodec) << std::endl;
	}

	VkVideoCoreProfile videoProfile(detectedVideoCodec, pVideoFormat->chromaSubsampling, pVideoFormat->lumaBitDepth, pVideoFormat->chromaBitDepth,
									pVideoFormat->codecProfile);
	DE_ASSERT(videoProfile == m_profile);

	// Check the detected profile is the same as the specified test profile.
	DE_ASSERT(m_profile == videoProfile);

	DE_ASSERT(((detectedVideoCodec & m_supportedVideoCodecs) != 0) && (detectedVideoCodec == m_profile.GetCodecType()));

	if (m_videoFormat.coded_width && m_videoFormat.coded_height) {
		// CreateDecoder() has been called before, and now there's possible config change
		m_deviceContext->waitDecodeQueue();
		m_deviceContext->deviceWaitIdle();
	}

	std::cout << "Video Decoding Params:" << std::endl
			  << "\tNum Surfaces : " << m_numDecodeSurfaces << std::endl
			  << "\tResize       : " << codedExtent.width << " x " << codedExtent.height << std::endl;

	uint32_t maxDpbSlotCount = pVideoFormat->maxNumDpbSlots;

	assert(VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR == pVideoFormat->chromaSubsampling ||
		   VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR == pVideoFormat->chromaSubsampling ||
		   VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR == pVideoFormat->chromaSubsampling ||
		   VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR == pVideoFormat->chromaSubsampling);
	DE_ASSERT(pVideoFormat->chromaSubsampling == m_profile.GetColorSubsampling());

	imageExtent.width  = std::max(imageExtent.width, m_videoCaps.minCodedExtent.width);
	imageExtent.height = std::max(imageExtent.height, m_videoCaps.minCodedExtent.height);

	imageExtent.width = deAlign32(imageExtent.width,  m_videoCaps.pictureAccessGranularity.width);
	imageExtent.height = deAlign32(imageExtent.height,  m_videoCaps.pictureAccessGranularity.height);

	if (!m_videoSession ||
		!m_videoSession->IsCompatible( m_deviceContext->device,
									   m_deviceContext->decodeQueueFamilyIdx(),
									   &videoProfile,
									   m_outImageFormat,
									   imageExtent,
									   m_dpbImageFormat,
									   maxDpbSlotCount,
									   std::max<uint32_t>(maxDpbSlotCount, VkParserPerFrameDecodeParameters::MAX_DPB_REF_SLOTS)) ) {

		result = VulkanVideoSession::Create( *m_deviceContext,
											 m_deviceContext->decodeQueueFamilyIdx(),
											 &videoProfile,
											m_outImageFormat,
											 imageExtent,
											m_dpbImageFormat,
											 maxDpbSlotCount,
											 std::min<uint32_t>(maxDpbSlotCount, m_videoCaps.maxActiveReferencePictures),
											 m_videoSession);

		// after creating a new video session, we need codec reset.
		m_resetDecoder = true;
		assert(result == VK_SUCCESS);
	}

	VkImageUsageFlags outImageUsage = (VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
									   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
									   VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	VkImageUsageFlags dpbImageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

	if (dpbAndOutputCoincide()) {
		dpbImageUsage = outImageUsage | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
		outImageUsage &= ~VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
	} else {
		// The implementation does not support dpbAndOutputCoincide
		m_useSeparateOutputImages = true;
	}

	if(!(m_videoCaps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR)) {
		// The implementation does not support individual images for DPB and so must use arrays
		m_useImageArray = true;
		m_useImageViewArray = true;
	}

	bool useLinearOutput = false;
	int32_t ret = m_videoFrameBuffer->InitImagePool(videoProfile.GetProfile(),
													m_numDecodeSurfaces,
													m_dpbImageFormat,
													m_outImageFormat,
													codedExtent,
													imageExtent,
													dpbImageUsage,
													outImageUsage,
													m_deviceContext->decodeQueueFamilyIdx(),
													m_useImageArray, m_useImageViewArray,
													m_useSeparateOutputImages, useLinearOutput);

	assert((uint32_t)ret >= m_numDecodeSurfaces);
	if ((uint32_t)ret != m_numDecodeSurfaces) {
		fprintf(stderr, "\nERROR: InitImagePool() ret(%d) != m_numDecodeSurfaces(%d)\n", ret, m_numDecodeSurfaces);
	}

	if (videoLoggingEnabled()) {
		std::cout << "Allocating Video Device Memory" << std::endl
				  << "Allocating " << m_numDecodeSurfaces << " Num Decode Surfaces and "
				  << maxDpbSlotCount << " Video Device Memory Images for DPB " << std::endl
				  << imageExtent.width << " x " << imageExtent.height << std::endl;
	}
	m_maxDecodeFramesCount = m_numDecodeSurfaces;

	// There will be no more than 32 frames in the queue.
	m_decodeFramesData.resize(std::max<uint32_t>(m_maxDecodeFramesCount, 32));


	int32_t availableBuffers = (int32_t)m_decodeFramesData.GetBitstreamBuffersQueue().
			GetAvailableNodesNumber();
	if (availableBuffers < m_numBitstreamBuffersToPreallocate) {

		uint32_t allocateNumBuffers = std::min<uint32_t>(
				m_decodeFramesData.GetBitstreamBuffersQueue().GetMaxNodes(),
				(m_numBitstreamBuffersToPreallocate - availableBuffers));

		allocateNumBuffers = std::min<uint32_t>(allocateNumBuffers,
												m_decodeFramesData.GetBitstreamBuffersQueue().GetFreeNodesNumber());

		for (uint32_t i = 0; i < allocateNumBuffers; i++) {

			VkSharedBaseObj<VulkanBitstreamBufferImpl> bitstreamBuffer;
			VkDeviceSize allocSize = 2 * 1024 * 1024;

			result = VulkanBitstreamBufferImpl::Create(m_deviceContext,
													   m_deviceContext->decodeQueueFamilyIdx(),
													   allocSize,
													   m_videoCaps.minBitstreamBufferOffsetAlignment,
													   m_videoCaps.minBitstreamBufferSizeAlignment,
													   nullptr, 0, bitstreamBuffer,
														m_profile.GetProfileListInfo());
			assert(result == VK_SUCCESS);
			if (result != VK_SUCCESS) {
				fprintf(stderr, "\nERROR: CreateVideoBitstreamBuffer() result: 0x%x\n", result);
				break;
			}

			int32_t nodeAddedWithIndex = m_decodeFramesData.GetBitstreamBuffersQueue().
					AddNodeToPool(bitstreamBuffer, false);
			if (nodeAddedWithIndex < 0) {
				assert("Could not add the new node to the pool");
				break;
			}
		}
	}

	// Save the original config
	m_videoFormat = *pVideoFormat;
	return m_numDecodeSurfaces;
}

int32_t VideoBaseDecoder::BeginSequence (const VkParserSequenceInfo* pnvsi)
{
	bool sequenceUpdate = m_nvsi.nMaxWidth != 0 && m_nvsi.nMaxHeight != 0;

	const uint32_t maxDpbSlots =  (pnvsi->eCodec == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) ? VkParserPerFrameDecodeParameters::MAX_DPB_REF_AND_SETUP_SLOTS : VkParserPerFrameDecodeParameters::MAX_DPB_REF_SLOTS;
	uint32_t configDpbSlots = (pnvsi->nMinNumDpbSlots > 0) ? pnvsi->nMinNumDpbSlots : maxDpbSlots;
	configDpbSlots = std::min<uint32_t>(configDpbSlots, maxDpbSlots);

	bool sequenceReconfigureFormat = false;
	bool sequenceReconfigureCodedExtent = false;
	if (sequenceUpdate) {
		if ((pnvsi->eCodec != m_nvsi.eCodec) ||
			(pnvsi->nChromaFormat != m_nvsi.nChromaFormat) || (pnvsi->uBitDepthLumaMinus8 != m_nvsi.uBitDepthLumaMinus8) ||
			(pnvsi->uBitDepthChromaMinus8 != m_nvsi.uBitDepthChromaMinus8) ||
			(pnvsi->bProgSeq != m_nvsi.bProgSeq)) {
			sequenceReconfigureFormat = true;
		}

		if ((pnvsi->nCodedWidth != m_nvsi.nCodedWidth) || (pnvsi->nCodedHeight != m_nvsi.nCodedHeight)) {
			sequenceReconfigureCodedExtent = true;
		}

	}

	m_nvsi = *pnvsi;
	m_nvsi.nMaxWidth = pnvsi->nCodedWidth;
	m_nvsi.nMaxHeight = pnvsi->nCodedHeight;

	m_maxNumDecodeSurfaces = pnvsi->nMinNumDecodeSurfaces;

		VkParserDetectedVideoFormat detectedFormat;
		uint8_t raw_seqhdr_data[1024]; /* Output the sequence header data, currently
                                      not used */

		memset(&detectedFormat, 0, sizeof(detectedFormat));

		detectedFormat.sequenceUpdate = sequenceUpdate;
		detectedFormat.sequenceReconfigureFormat = sequenceReconfigureFormat;
		detectedFormat.sequenceReconfigureCodedExtent = sequenceReconfigureCodedExtent;

		detectedFormat.codec = pnvsi->eCodec;
		detectedFormat.frame_rate.numerator = NV_FRAME_RATE_NUM(pnvsi->frameRate);
		detectedFormat.frame_rate.denominator = NV_FRAME_RATE_DEN(pnvsi->frameRate);
		detectedFormat.progressive_sequence = pnvsi->bProgSeq;
		detectedFormat.coded_width = pnvsi->nCodedWidth;
		detectedFormat.coded_height = pnvsi->nCodedHeight;
		detectedFormat.display_area.right = pnvsi->nDisplayWidth;
		detectedFormat.display_area.bottom = pnvsi->nDisplayHeight;

		if ((StdChromaFormatIdc)pnvsi->nChromaFormat == chroma_format_idc_420) {
			detectedFormat.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
		} else if ((StdChromaFormatIdc)pnvsi->nChromaFormat == chroma_format_idc_422) {
			detectedFormat.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
		} else if ((StdChromaFormatIdc)pnvsi->nChromaFormat == chroma_format_idc_444) {
			detectedFormat.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;
		} else {
			assert(!"Invalid chroma sub-sampling format");
		}

		switch (pnvsi->uBitDepthLumaMinus8) {
			case 0:
				detectedFormat.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
				break;
			case 2:
				detectedFormat.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
				break;
			case 4:
				detectedFormat.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
				break;
			default:
				assert(false);
		}

		switch (pnvsi->uBitDepthChromaMinus8) {
			case 0:
				detectedFormat.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
				break;
			case 2:
				detectedFormat.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
				break;
			case 4:
				detectedFormat.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
				break;
			default:
				assert(false);
		}

		detectedFormat.bit_depth_luma_minus8 = pnvsi->uBitDepthLumaMinus8;
		detectedFormat.bit_depth_chroma_minus8 = pnvsi->uBitDepthChromaMinus8;
		detectedFormat.bitrate = pnvsi->lBitrate;
		detectedFormat.display_aspect_ratio.x = pnvsi->lDARWidth;
		detectedFormat.display_aspect_ratio.y = pnvsi->lDARHeight;
		detectedFormat.video_signal_description.video_format = pnvsi->lVideoFormat;
		detectedFormat.video_signal_description.video_full_range_flag = pnvsi->uVideoFullRange;
		detectedFormat.video_signal_description.color_primaries = pnvsi->lColorPrimaries;
		detectedFormat.video_signal_description.transfer_characteristics = pnvsi->lTransferCharacteristics;
		detectedFormat.video_signal_description.matrix_coefficients = pnvsi->lMatrixCoefficients;
		detectedFormat.seqhdr_data_length = (uint32_t)std::min((size_t)pnvsi->cbSequenceHeader, sizeof(raw_seqhdr_data));
		detectedFormat.minNumDecodeSurfaces = pnvsi->nMinNumDecodeSurfaces;
		detectedFormat.maxNumDpbSlots = configDpbSlots;
		detectedFormat.codecProfile = pnvsi->codecProfile;

		if (detectedFormat.seqhdr_data_length > 0) {
			memcpy(raw_seqhdr_data, pnvsi->SequenceHeaderData,
				   detectedFormat.seqhdr_data_length);
		}
		int32_t maxDecodeRTs = StartVideoSequence(&detectedFormat);
		// nDecodeRTs <= 0 means SequenceCallback failed
		// nDecodeRTs  = 1 means SequenceCallback succeeded
		// nDecodeRTs  > 1 means we need to overwrite the MaxNumDecodeSurfaces
		if (maxDecodeRTs <= 0) {
			return 0;
		}
		// MaxNumDecodeSurface may not be correctly calculated by the client while
		// parser creation so overwrite it with NumDecodeSurface. (only if nDecodeRT
		// > 1)
		if (maxDecodeRTs > 1) {
			m_maxNumDecodeSurfaces = maxDecodeRTs;
		}

	m_maxNumDpbSlots = m_dpb.Init(configDpbSlots, sequenceUpdate /* reconfigure the DPB size if true */);

	return m_maxNumDecodeSurfaces;
}

bool VideoBaseDecoder::AllocPictureBuffer (VkPicIf** ppNvidiaVulkanPicture)
{
	DEBUGLOG(std::cout << "VideoBaseDecoder::AllocPictureBuffer" << std::endl);
	bool result = false;

	*ppNvidiaVulkanPicture = m_videoFrameBuffer->ReservePictureBuffer();

	if (*ppNvidiaVulkanPicture)
	{
		result = true;

		DEBUGLOG(std::cout << "\tVideoBaseDecoder::AllocPictureBuffer " << (void*)*ppNvidiaVulkanPicture << std::endl);
	}

	if (!result)
	{
		*ppNvidiaVulkanPicture = (VkPicIf*)nullptr;
	}

	return result;
}

bool VideoBaseDecoder::DecodePicture (VkParserPictureData* pd)
{
	DEBUGLOG(std::cout << "VideoBaseDecoder::DecodePicture" << std::endl);
	bool							result				= false;

	if (!pd->pCurrPic)
	{
		return result;
	}

	vkPicBuffBase*	pVkPicBuff	= GetPic(pd->pCurrPic);
	const int32_t				picIdx		= pVkPicBuff ? pVkPicBuff->m_picIdx : -1;
	if (videoLoggingEnabled())
		std::cout
            << "\t ==> VulkanVideoParser::DecodePicture " << picIdx << std::endl
            << "\t\t progressive: " << (bool)pd->progressive_frame
            << // Frame is progressive
            "\t\t field: " << (bool)pd->field_pic_flag << std::endl
            << // 0 = frame picture, 1 = field picture
            "\t\t\t bottom_field: " << (bool)pd->bottom_field_flag
            << // 0 = top field, 1 = bottom field (ignored if field_pic_flag=0)
            "\t\t\t second_field: " << (bool)pd->second_field
            << // Second field of a complementary field pair
            "\t\t\t top_field: " << (bool)pd->top_field_first << std::endl
            << // Frame pictures only
            "\t\t repeat_first: " << pd->repeat_first_field
            << // For 3:2 pulldown (number of additional fields, 2 = frame
            // doubling, 4 = frame tripling)
            "\t\t ref_pic: " << (bool)pd->ref_pic_flag
            << std::endl; // Frame is a reference frame

	DE_ASSERT(picIdx < MAX_FRM_CNT);

	VkParserDecodePictureInfo	decodePictureInfo	= VkParserDecodePictureInfo();
	decodePictureInfo.pictureIndex				= picIdx;
	decodePictureInfo.flags.progressiveFrame	= pd->progressive_frame;
	decodePictureInfo.flags.fieldPic			= pd->field_pic_flag;			// 0 = frame picture, 1 = field picture
	decodePictureInfo.flags.repeatFirstField	= pd->repeat_first_field;	// For 3:2 pulldown (number of additional fields, 2 = frame doubling, 4 = frame tripling)
	decodePictureInfo.flags.refPic				= pd->ref_pic_flag;				// Frame is a reference frame

	// Mark the first field as unpaired Detect unpaired fields
	if (pd->field_pic_flag)
	{
		decodePictureInfo.flags.bottomField		= pd->bottom_field_flag;	// 0 = top field, 1 = bottom field (ignored if field_pic_flag=0)
		decodePictureInfo.flags.secondField		= pd->second_field;			// Second field of a complementary field pair
		decodePictureInfo.flags.topFieldFirst	= pd->top_field_first;		// Frame pictures only

		if (!pd->second_field)
		{
			decodePictureInfo.flags.unpairedField = true; // Incomplete (half) frame.
		}
		else
		{
			if (decodePictureInfo.flags.unpairedField)
			{
				decodePictureInfo.flags.syncToFirstField = true;
				decodePictureInfo.flags.unpairedField = false;
			}
		}
	}

	decodePictureInfo.frameSyncinfo.unpairedField		= decodePictureInfo.flags.unpairedField;
	decodePictureInfo.frameSyncinfo.syncToFirstField	= decodePictureInfo.flags.syncToFirstField;

	return DecodePicture(pd, pVkPicBuff, &decodePictureInfo);
}

bool VideoBaseDecoder::DecodePicture (VkParserPictureData* pd,
									  vkPicBuffBase* /*pVkPicBuff*/,
									  VkParserDecodePictureInfo* pDecodePictureInfo)
{
	DEBUGLOG(std::cout << "\tDecodePicture sps_sid:" << (uint32_t)pNvidiaVulkanParserPictureData->CodecSpecific.h264.pStdSps->seq_parameter_set_id << " pps_sid:" << (uint32_t)pNvidiaVulkanParserPictureData->CodecSpecific.h264.pStdPps->seq_parameter_set_id << " pps_pid:" << (uint32_t)pNvidiaVulkanParserPictureData->CodecSpecific.h264.pStdPps->pic_parameter_set_id << std::endl);
	bool bRet = false;

	nvVideoH264PicParameters h264;
	nvVideoH265PicParameters hevc;

	if (!pd->pCurrPic)
	{
		return false;
	}

	const uint32_t PicIdx = GetPicIdx(pd->pCurrPic);

	TCU_CHECK (PicIdx < MAX_FRM_CNT);

	VkParserPerFrameDecodeParameters pictureParams = VkParserPerFrameDecodeParameters();
	VkParserPerFrameDecodeParameters* pCurrFrameDecParams = &pictureParams;
	pCurrFrameDecParams->currPicIdx = PicIdx;
	pCurrFrameDecParams->numSlices = pd->numSlices;
	pCurrFrameDecParams->firstSliceIndex = pd->firstSliceIndex;
	pCurrFrameDecParams->bitstreamDataOffset = pd->bitstreamDataOffset;
	pCurrFrameDecParams->bitstreamDataLen = pd->bitstreamDataLen;
	pCurrFrameDecParams->bitstreamData = pd->bitstreamData;

	VkVideoReferenceSlotInfoKHR
			referenceSlots[VkParserPerFrameDecodeParameters::MAX_DPB_REF_AND_SETUP_SLOTS];
	VkVideoReferenceSlotInfoKHR setupReferenceSlot = {
			VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
			nullptr,
			-1, // slotIndex
			NULL // pPictureResource
	};
	VkVideoReferenceSlotInfoKHR setupReferenceSlotActivation = {
		VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
		nullptr,
		-1,
		nullptr,
	};

	pCurrFrameDecParams->decodeFrameInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
	pCurrFrameDecParams->decodeFrameInfo.dstPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
	pCurrFrameDecParams->dpbSetupPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;

	if (m_profile.GetCodecType() == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
	{
		const VkParserH264PictureData* const pin = &pd->CodecSpecific.h264;
		h264 = nvVideoH264PicParameters();
		VkVideoDecodeH264PictureInfoKHR* pPictureInfo = &h264.pictureInfo;
		nvVideoDecodeH264DpbSlotInfo* pDpbRefList = h264.dpbRefList;
		StdVideoDecodeH264PictureInfo* pStdPictureInfo = &h264.stdPictureInfo;

		pCurrFrameDecParams->pStdPps = pin->pStdPps;
		pCurrFrameDecParams->pStdSps = pin->pStdSps;
		pCurrFrameDecParams->pStdVps = nullptr;

		pDecodePictureInfo->videoFrameType = 0; // pd->CodecSpecific.h264.slice_type;
		// FIXME: If mvcext is enabled.
		pDecodePictureInfo->viewId = pd->CodecSpecific.h264.mvcext.view_id;

		pPictureInfo->pStdPictureInfo = &h264.stdPictureInfo;

		pPictureInfo->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR;
		pPictureInfo->pNext = nullptr;

		pCurrFrameDecParams->decodeFrameInfo.pNext = &h264.pictureInfo;

		pStdPictureInfo->pic_parameter_set_id = pin->pic_parameter_set_id; // PPS ID
		pStdPictureInfo->seq_parameter_set_id = pin->seq_parameter_set_id; // SPS ID;

		pStdPictureInfo->frame_num = (uint16_t)pin->frame_num;
		pPictureInfo->sliceCount = pd->numSlices;
		uint32_t maxSliceCount = 0;
		assert(pd->firstSliceIndex == 0); // No slice and MV modes are supported yet
		pPictureInfo->pSliceOffsets = pd->bitstreamData->GetStreamMarkersPtr(pd->firstSliceIndex, maxSliceCount);
		assert(maxSliceCount == pd->numSlices);

		StdVideoDecodeH264PictureInfoFlags currPicFlags = StdVideoDecodeH264PictureInfoFlags();
		currPicFlags.is_intra = (pd->intra_pic_flag != 0);
		// 0 = frame picture, 1 = field picture
		if (pd->field_pic_flag) {
			// 0 = top field, 1 = bottom field (ignored if field_pic_flag = 0)
			currPicFlags.field_pic_flag = true;
			if (pd->bottom_field_flag) {
				currPicFlags.bottom_field_flag = true;
			}
		}
		// Second field of a complementary field pair
		if (pd->second_field) {
			currPicFlags.complementary_field_pair = true;
		}
		// Frame is a reference frame
		if (pd->ref_pic_flag) {
			currPicFlags.is_reference = true;
		}
		pStdPictureInfo->flags = currPicFlags;
		if (!pd->field_pic_flag) {
			pStdPictureInfo->PicOrderCnt[0] = pin->CurrFieldOrderCnt[0];
			pStdPictureInfo->PicOrderCnt[1] = pin->CurrFieldOrderCnt[1];
		} else {
			pStdPictureInfo->PicOrderCnt[pd->bottom_field_flag] = pin->CurrFieldOrderCnt[pd->bottom_field_flag];
		}

		const uint32_t maxDpbInputSlots = sizeof(pin->dpb) / sizeof(pin->dpb[0]);
		pCurrFrameDecParams->numGopReferenceSlots = FillDpbH264State(
				pd, pin->dpb, maxDpbInputSlots, pDpbRefList,
				VkParserPerFrameDecodeParameters::MAX_DPB_REF_SLOTS, // 16 reference pictures
				referenceSlots, pCurrFrameDecParams->pGopReferenceImagesIndexes,
				h264.stdPictureInfo.flags, &setupReferenceSlot.slotIndex);

		assert(!pd->ref_pic_flag || (setupReferenceSlot.slotIndex >= 0));

		// TODO: Dummy struct to silence validation. The root problem is that the dpb map doesn't take account of the setup slot,
		// for some reason... So we can't use the existing logic to setup the picture flags and frame number from the dpbEntry
		// class.
		VkVideoDecodeH264DpbSlotInfoKHR h264SlotInfo = {};
		h264SlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
		StdVideoDecodeH264ReferenceInfo refinfo = {};
		h264SlotInfo.pStdReferenceInfo = &refinfo;

		if (setupReferenceSlot.slotIndex >= 0) {
			setupReferenceSlot.pPictureResource = &pCurrFrameDecParams->dpbSetupPictureResource;
			setupReferenceSlot.pNext = &h264SlotInfo;
			pCurrFrameDecParams->decodeFrameInfo.pSetupReferenceSlot = &setupReferenceSlot;
		}
		if (pCurrFrameDecParams->numGopReferenceSlots) {
			assert(pCurrFrameDecParams->numGopReferenceSlots <= (int32_t)VkParserPerFrameDecodeParameters::MAX_DPB_REF_SLOTS);
			for (uint32_t dpbEntryIdx = 0; dpbEntryIdx < (uint32_t)pCurrFrameDecParams->numGopReferenceSlots;
				 dpbEntryIdx++) {
				pCurrFrameDecParams->pictureResources[dpbEntryIdx].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
				referenceSlots[dpbEntryIdx].pPictureResource = &pCurrFrameDecParams->pictureResources[dpbEntryIdx];
				assert(pDpbRefList[dpbEntryIdx].IsReference());
			}

			pCurrFrameDecParams->decodeFrameInfo.pReferenceSlots = referenceSlots;
			pCurrFrameDecParams->decodeFrameInfo.referenceSlotCount = pCurrFrameDecParams->numGopReferenceSlots;
		} else {
			pCurrFrameDecParams->decodeFrameInfo.pReferenceSlots = NULL;
			pCurrFrameDecParams->decodeFrameInfo.referenceSlotCount = 0;
		}
	}
	else if (m_profile.GetCodecType() == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)
	{
		const VkParserHevcPictureData* const pin = &pd->CodecSpecific.hevc;
		hevc = nvVideoH265PicParameters();
		VkVideoDecodeH265PictureInfoKHR* pPictureInfo = &hevc.pictureInfo;
		StdVideoDecodeH265PictureInfo* pStdPictureInfo = &hevc.stdPictureInfo;
		nvVideoDecodeH265DpbSlotInfo* pDpbRefList = hevc.dpbRefList;

		pCurrFrameDecParams->pStdPps = pin->pStdPps;
		pCurrFrameDecParams->pStdSps = pin->pStdSps;
		pCurrFrameDecParams->pStdVps = pin->pStdVps;
		if (videoLoggingEnabled()) {
			std::cout << "\n\tCurrent h.265 Picture VPS update : "
					  << pin->pStdVps->GetUpdateSequenceCount() << std::endl;
			std::cout << "\n\tCurrent h.265 Picture SPS update : "
					  << pin->pStdSps->GetUpdateSequenceCount() << std::endl;
			std::cout << "\tCurrent h.265 Picture PPS update : "
					  << pin->pStdPps->GetUpdateSequenceCount() << std::endl;
		}

		pPictureInfo->sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PICTURE_INFO_KHR;
		pPictureInfo->pNext = nullptr;

		pPictureInfo->pStdPictureInfo = &hevc.stdPictureInfo;
		pCurrFrameDecParams->decodeFrameInfo.pNext = &hevc.pictureInfo;

		pDecodePictureInfo->videoFrameType = 0; // pd->CodecSpecific.hevc.SliceType;
		if (pd->CodecSpecific.hevc.mv_hevc_enable) {
			pDecodePictureInfo->viewId = pd->CodecSpecific.hevc.nuh_layer_id;
		} else {
			pDecodePictureInfo->viewId = 0;
		}

		pPictureInfo->sliceSegmentCount = pd->numSlices;
		uint32_t maxSliceCount = 0;
		assert(pd->firstSliceIndex == 0); // No slice and MV modes are supported yet
		pPictureInfo->pSliceSegmentOffsets = pd->bitstreamData->GetStreamMarkersPtr(pd->firstSliceIndex, maxSliceCount);
		assert(maxSliceCount == pd->numSlices);

		pStdPictureInfo->pps_pic_parameter_set_id   = pin->pic_parameter_set_id;       // PPS ID
		pStdPictureInfo->pps_seq_parameter_set_id   = pin->seq_parameter_set_id;       // SPS ID
		pStdPictureInfo->sps_video_parameter_set_id = pin->vps_video_parameter_set_id; // VPS ID

		// hevc->irapPicFlag = m_slh.nal_unit_type >= NUT_BLA_W_LP &&
		// m_slh.nal_unit_type <= NUT_CRA_NUT;
		pStdPictureInfo->flags.IrapPicFlag = pin->IrapPicFlag; // Intra Random Access Point for current picture.
		// hevc->idrPicFlag = m_slh.nal_unit_type == NUT_IDR_W_RADL ||
		// m_slh.nal_unit_type == NUT_IDR_N_LP;
		pStdPictureInfo->flags.IdrPicFlag = pin->IdrPicFlag; // Instantaneous Decoding Refresh for current picture.

		// NumBitsForShortTermRPSInSlice = s->sh.short_term_rps ?
		// s->sh.short_term_ref_pic_set_size : 0
		pStdPictureInfo->NumBitsForSTRefPicSetInSlice = pin->NumBitsForShortTermRPSInSlice;

		// NumDeltaPocsOfRefRpsIdx = s->sh.short_term_rps ?
		// s->sh.short_term_rps->rps_idx_num_delta_pocs : 0
		pStdPictureInfo->NumDeltaPocsOfRefRpsIdx = pin->NumDeltaPocsOfRefRpsIdx;
		pStdPictureInfo->PicOrderCntVal = pin->CurrPicOrderCntVal;

		if (videoLoggingEnabled())
			std::cout << "\tnumPocStCurrBefore: " << (int32_t)pin->NumPocStCurrBefore
					  << " numPocStCurrAfter: " << (int32_t)pin->NumPocStCurrAfter
					  << " numPocLtCurr: " << (int32_t)pin->NumPocLtCurr << std::endl;

		pCurrFrameDecParams->numGopReferenceSlots = FillDpbH265State(pd, pin, pDpbRefList, pStdPictureInfo,
																	 VkParserPerFrameDecodeParameters::MAX_DPB_REF_SLOTS, // max 16 reference pictures
																	 referenceSlots, pCurrFrameDecParams->pGopReferenceImagesIndexes,
																	 &setupReferenceSlot.slotIndex);

		assert(!pd->ref_pic_flag || (setupReferenceSlot.slotIndex >= 0));
		// TODO: Dummy struct to silence validation. The root problem is that the dpb map doesn't take account of the setup slot,
		// for some reason... So we can't use the existing logic to setup the picture flags and frame number from the dpbEntry
		// class.
		VkVideoDecodeH265DpbSlotInfoKHR h265SlotInfo = {};
		h265SlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR;
		StdVideoDecodeH265ReferenceInfo refinfo = {};
		h265SlotInfo.pStdReferenceInfo = &refinfo;

		if (setupReferenceSlot.slotIndex >= 0) {
			setupReferenceSlot.pPictureResource = &pCurrFrameDecParams->dpbSetupPictureResource;
			setupReferenceSlot.pNext = &h265SlotInfo;
			pCurrFrameDecParams->decodeFrameInfo.pSetupReferenceSlot = &setupReferenceSlot;
		}
		if (pCurrFrameDecParams->numGopReferenceSlots) {
			assert(pCurrFrameDecParams->numGopReferenceSlots <= (int32_t)VkParserPerFrameDecodeParameters::MAX_DPB_REF_SLOTS);
			for (uint32_t dpbEntryIdx = 0; dpbEntryIdx < (uint32_t)pCurrFrameDecParams->numGopReferenceSlots;
				 dpbEntryIdx++) {
				pCurrFrameDecParams->pictureResources[dpbEntryIdx].sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
				referenceSlots[dpbEntryIdx].pPictureResource = &pCurrFrameDecParams->pictureResources[dpbEntryIdx];
				assert(pDpbRefList[dpbEntryIdx].IsReference());
			}

			pCurrFrameDecParams->decodeFrameInfo.pReferenceSlots = referenceSlots;
			pCurrFrameDecParams->decodeFrameInfo.referenceSlotCount = pCurrFrameDecParams->numGopReferenceSlots;
		} else {
			pCurrFrameDecParams->decodeFrameInfo.pReferenceSlots = nullptr;
			pCurrFrameDecParams->decodeFrameInfo.referenceSlotCount = 0;
		}

		if (videoLoggingEnabled()) {
			for (int32_t i = 0; i < HEVC_MAX_DPB_SLOTS; i++) {
				std::cout << "\tdpbIndex: " << i;
				if (pDpbRefList[i]) {
					std::cout << " REFERENCE FRAME";

					std::cout << " picOrderCntValList: "
							  << (int32_t)pDpbRefList[i]
									  .dpbSlotInfo.pStdReferenceInfo->PicOrderCntVal;

					std::cout << "\t\t Flags: ";
					if (pDpbRefList[i]
							.dpbSlotInfo.pStdReferenceInfo->flags.used_for_long_term_reference) {
						std::cout << "IS LONG TERM ";
					}

				} else {
					std::cout << " NOT A REFERENCE ";
				}
				std::cout << std::endl;
			}
		}
	}

	pDecodePictureInfo->displayWidth	= m_nvsi.nDisplayWidth;
	pDecodePictureInfo->displayHeight	= m_nvsi.nDisplayHeight;

	bRet = DecodePictureWithParameters(pCurrFrameDecParams, pDecodePictureInfo) >= 0;

	assert(bRet);

	m_nCurrentPictureID++;

	return bRet;
}

int32_t VideoBaseDecoder::DecodePictureWithParameters(VkParserPerFrameDecodeParameters* pPicParams,
													  VkParserDecodePictureInfo*		pDecodePictureInfo)
{

	TCU_CHECK_MSG(m_videoSession, "Video session has not been initialized!");

	int32_t currPicIdx = pPicParams->currPicIdx;
	assert((uint32_t)currPicIdx < m_numDecodeSurfaces);

	int32_t picNumInDecodeOrder = m_decodePicCount++;
	m_videoFrameBuffer->SetPicNumInDecodeOrder(currPicIdx, picNumInDecodeOrder);

	NvVkDecodeFrameDataSlot frameDataSlot;
	int32_t					retPicIdx = GetCurrentFrameData((uint32_t)currPicIdx, frameDataSlot);
	assert(retPicIdx == currPicIdx);

	if (retPicIdx != currPicIdx)
	{
		fprintf(stderr, "\nERROR: DecodePictureWithParameters() retPicIdx(%d) != currPicIdx(%d)\n", retPicIdx, currPicIdx);
	}

	assert(pPicParams->bitstreamData->GetMaxSize() >= pPicParams->bitstreamDataLen);
	pPicParams->decodeFrameInfo.srcBuffer = pPicParams->bitstreamData->GetBuffer();
	assert(pPicParams->bitstreamDataOffset == 0);
	assert(pPicParams->firstSliceIndex == 0);
	pPicParams->decodeFrameInfo.srcBufferOffset = pPicParams->bitstreamDataOffset;
	pPicParams->decodeFrameInfo.srcBufferRange	= deAlign64(pPicParams->bitstreamDataLen, m_videoCaps.minBitstreamBufferSizeAlignment);

	// pPicParams->decodeFrameInfo.dstImageView = VkImageView();

	VkVideoBeginCodingInfoKHR decodeBeginInfo	= {VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR};
	// CmdResetQueryPool are NOT Supported yet.
	decodeBeginInfo.pNext						= pPicParams->beginCodingInfoPictureParametersExt;

	decodeBeginInfo.videoSession				= m_videoSession->GetVideoSession();

	assert(!!pPicParams->decodeFrameInfo.srcBuffer);
	const VkBufferMemoryBarrier2KHR bitstreamBufferMemoryBarrier = {
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR,
		nullptr,
		VK_PIPELINE_STAGE_2_NONE_KHR,
		0, // VK_ACCESS_2_HOST_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
		VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR,
		(uint32_t)m_deviceContext->decodeQueueFamilyIdx(),
		(uint32_t)m_deviceContext->decodeQueueFamilyIdx(),
		pPicParams->decodeFrameInfo.srcBuffer,
		pPicParams->decodeFrameInfo.srcBufferOffset,
		pPicParams->decodeFrameInfo.srcBufferRange};

	uint32_t					   baseArrayLayer	  = (m_useImageArray || m_useImageViewArray) ? pPicParams->currPicIdx : 0;
	const VkImageMemoryBarrier2KHR dpbBarrierTemplate = {
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR, // VkStructureType sType
		nullptr, // const void*     pNext
		VK_PIPELINE_STAGE_2_NONE_KHR, // VkPipelineStageFlags2KHR srcStageMask
		0, // VkAccessFlags2KHR        srcAccessMask
		VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, // VkPipelineStageFlags2KHR dstStageMask;
		VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR, // VkAccessFlags   dstAccessMask
		VK_IMAGE_LAYOUT_UNDEFINED, // VkImageLayout   oldLayout
		VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, // VkImageLayout   newLayout
		(uint32_t)m_deviceContext->decodeQueueFamilyIdx(), // uint32_t        srcQueueFamilyIndex
		(uint32_t)m_deviceContext->decodeQueueFamilyIdx(), // uint32_t   dstQueueFamilyIndex
		VkImage(), // VkImage         image;
		{
			// VkImageSubresourceRange   subresourceRange
			VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags aspectMask
			0, // uint32_t           baseMipLevel
			1, // uint32_t           levelCount
			baseArrayLayer, // uint32_t           baseArrayLayer
			1, // uint32_t           layerCount;
		},
	};

	VkImageMemoryBarrier2KHR					 imageBarriers[VkParserPerFrameDecodeParameters::MAX_DPB_REF_AND_SETUP_SLOTS];
	uint32_t									 numDpbBarriers					  = 0;
	VulkanVideoFrameBuffer::PictureResourceInfo	 currentDpbPictureResourceInfo	  = VulkanVideoFrameBuffer::PictureResourceInfo();
	VulkanVideoFrameBuffer::PictureResourceInfo	 currentOutputPictureResourceInfo = VulkanVideoFrameBuffer::PictureResourceInfo();
	VkVideoPictureResourceInfoKHR				 currentOutputPictureResource	  = {VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR, nullptr};

	VkVideoPictureResourceInfoKHR*				 pOutputPictureResource			  = nullptr;
	VulkanVideoFrameBuffer::PictureResourceInfo* pOutputPictureResourceInfo		  = nullptr;
	if (!dpbAndOutputCoincide())
	{
		// Output Distinct will use the decodeFrameInfo.dstPictureResource directly.
		pOutputPictureResource = &pPicParams->decodeFrameInfo.dstPictureResource;
	}
	else if (true) // TODO: Tidying
	{
		// Output Coincide needs the output only if we are processing linear images that we need to copy to below.
		pOutputPictureResource = &currentOutputPictureResource;
	}

	if (pOutputPictureResource)
	{
		// if the pOutputPictureResource is set then we also need the pOutputPictureResourceInfo.
		pOutputPictureResourceInfo = &currentOutputPictureResourceInfo;
	}

	if (pPicParams->currPicIdx !=
		m_videoFrameBuffer->GetCurrentImageResourceByIndex(pPicParams->currPicIdx,
														   &pPicParams->dpbSetupPictureResource,
														   &currentDpbPictureResourceInfo,
														   VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
														   pOutputPictureResource,
														   pOutputPictureResourceInfo,
														   VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR))
	{
		assert(!"GetImageResourcesByIndex has failed");
	}

	if (dpbAndOutputCoincide())
	{
		// For the Output Coincide, the DPB and destination output resources are the same.
		pPicParams->decodeFrameInfo.dstPictureResource = pPicParams->dpbSetupPictureResource;
	}
	else if (pOutputPictureResourceInfo)
	{
		// For Output Distinct transition the image to DECODE_DST
		if (pOutputPictureResourceInfo->currentImageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
		{
			imageBarriers[numDpbBarriers]				= dpbBarrierTemplate;
			imageBarriers[numDpbBarriers].oldLayout		= pOutputPictureResourceInfo->currentImageLayout;
			imageBarriers[numDpbBarriers].newLayout		= VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
			imageBarriers[numDpbBarriers].image			= pOutputPictureResourceInfo->image;
			imageBarriers[numDpbBarriers].dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
			assert(!!imageBarriers[numDpbBarriers].image);
			numDpbBarriers++;
		}
	}

	if (currentDpbPictureResourceInfo.currentImageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
	{
		imageBarriers[numDpbBarriers]				= dpbBarrierTemplate;
		imageBarriers[numDpbBarriers].oldLayout		= currentDpbPictureResourceInfo.currentImageLayout;
		imageBarriers[numDpbBarriers].newLayout		= VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
		imageBarriers[numDpbBarriers].image			= currentDpbPictureResourceInfo.image;
		imageBarriers[numDpbBarriers].dstAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
		assert(!!imageBarriers[numDpbBarriers].image);
		numDpbBarriers++;
	}

	VulkanVideoFrameBuffer::PictureResourceInfo pictureResourcesInfo[VkParserPerFrameDecodeParameters::MAX_DPB_REF_AND_SETUP_SLOTS] = {};
	const int8_t*								pGopReferenceImagesIndexes															= pPicParams->pGopReferenceImagesIndexes;
	if (pPicParams->numGopReferenceSlots)
	{
		if (pPicParams->numGopReferenceSlots != m_videoFrameBuffer->GetDpbImageResourcesByIndex(
													pPicParams->numGopReferenceSlots,
													pGopReferenceImagesIndexes,
													pPicParams->pictureResources,
													pictureResourcesInfo,
													VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR))
		{
			assert(!"GetImageResourcesByIndex has failed");
		}
		for (int32_t resId = 0; resId < pPicParams->numGopReferenceSlots; resId++)
		{
			// slotLayer requires NVIDIA specific extension VK_KHR_video_layers, not enabled, just yet.
			// pGopReferenceSlots[resId].slotLayerIndex = 0;
			// pictureResourcesInfo[resId].image can be a nullptr handle if the picture is not-existent.
			if (!!pictureResourcesInfo[resId].image && (pictureResourcesInfo[resId].currentImageLayout != VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR) && (pictureResourcesInfo[resId].currentImageLayout != VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR))
			{
				imageBarriers[numDpbBarriers]			= dpbBarrierTemplate;
				imageBarriers[numDpbBarriers].oldLayout = pictureResourcesInfo[resId].currentImageLayout;
				imageBarriers[numDpbBarriers].newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
				imageBarriers[numDpbBarriers].image		= pictureResourcesInfo[resId].image;
				assert(!!imageBarriers[numDpbBarriers].image);
				numDpbBarriers++;
			}
		}
	}

	decodeBeginInfo.referenceSlotCount = pPicParams->decodeFrameInfo.referenceSlotCount;
	decodeBeginInfo.pReferenceSlots	   = pPicParams->decodeFrameInfo.pReferenceSlots;

	// Ensure the resource for the resources associated with the
    // reference slot (if it exists) are in the bound picture
    // resources set.  See VUID-vkCmdDecodeVideoKHR-pDecodeInfo-07149.
	std::vector<VkVideoReferenceSlotInfoKHR> fullReferenceSlots;
	if (pPicParams->decodeFrameInfo.pSetupReferenceSlot != nullptr)
	{
		fullReferenceSlots.clear();
		for (deUint32 i = 0; i < decodeBeginInfo.referenceSlotCount; i++)
			fullReferenceSlots.push_back(decodeBeginInfo.pReferenceSlots[i]);
		VkVideoReferenceSlotInfoKHR setupActivationSlot = {};
		setupActivationSlot.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
		setupActivationSlot.slotIndex = -1;
		setupActivationSlot.pPictureResource = dpbAndOutputCoincide() ? &pPicParams->decodeFrameInfo.dstPictureResource : &pPicParams->pictureResources[pPicParams->numGopReferenceSlots];
		fullReferenceSlots.push_back(setupActivationSlot);
		decodeBeginInfo.referenceSlotCount++;
		decodeBeginInfo.pReferenceSlots = fullReferenceSlots.data();
	}

	if (pDecodePictureInfo->flags.unpairedField)
	{
		// assert(pFrameSyncinfo->frameCompleteSemaphore == VkSemaphore());
		pDecodePictureInfo->flags.syncFirstReady = true;
	}
	// FIXME: the below sequence for interlaced synchronization.
	pDecodePictureInfo->flags.syncToFirstField								  = false;

	VulkanVideoFrameBuffer::FrameSynchronizationInfo frameSynchronizationInfo = VulkanVideoFrameBuffer::FrameSynchronizationInfo();
	frameSynchronizationInfo.hasFrameCompleteSignalFence					  = true;
	frameSynchronizationInfo.hasFrameCompleteSignalSemaphore				  = true;

	if (pPicParams->useInlinedPictureParameters == false)
	{
		// out of band parameters
		VkSharedBaseObj<VkVideoRefCountBase> currentVkPictureParameters;
		bool								 valid = pPicParams->pStdPps->GetClientObject(currentVkPictureParameters);
		assert(currentVkPictureParameters && valid);
		if (!(currentVkPictureParameters && valid))
		{
			return -1;
		}
		VkParserVideoPictureParameters* pOwnerPictureParameters =
			VkParserVideoPictureParameters::VideoPictureParametersFromBase(currentVkPictureParameters);
		assert(pOwnerPictureParameters);
		assert(pOwnerPictureParameters->GetId() <= m_currentPictureParameters->GetId());
		int32_t ret = pOwnerPictureParameters->FlushPictureParametersQueue(m_videoSession);
		assert(ret >= 0);
		if (!(ret >= 0))
		{
			return -1;
		}
		bool	isSps = false;
		int32_t spsId = pPicParams->pStdPps->GetSpsId(isSps);
		assert(!isSps);
		assert(spsId >= 0);
		assert(pOwnerPictureParameters->HasSpsId(spsId));
		bool	isPps = false;
		int32_t ppsId = pPicParams->pStdPps->GetPpsId(isPps);
		assert(isPps);
		assert(ppsId >= 0);
		assert(pOwnerPictureParameters->HasPpsId(ppsId));

		decodeBeginInfo.videoSessionParameters = *pOwnerPictureParameters;

		if (videoLoggingEnabled())
		{
			std::cout << "Using object " << decodeBeginInfo.videoSessionParameters << " with ID: (" << pOwnerPictureParameters->GetId() << ")"
					  << " for SPS: " << spsId << ", PPS: " << ppsId << std::endl;
		}
	}
	else
	{
		decodeBeginInfo.videoSessionParameters = VK_NULL_HANDLE;
	}

	VulkanVideoFrameBuffer::ReferencedObjectsInfo referencedObjectsInfo(pPicParams->bitstreamData,
																		pPicParams->pStdPps,
																		pPicParams->pStdSps,
																		pPicParams->pStdVps);
	int32_t										  retVal = m_videoFrameBuffer->QueuePictureForDecode(currPicIdx, pDecodePictureInfo, &referencedObjectsInfo, &frameSynchronizationInfo);
	if (currPicIdx != retVal)
	{
		assert(!"QueuePictureForDecode has failed");
	}

	VkFence		frameCompleteFence		   = frameSynchronizationInfo.frameCompleteFence;
	VkFence		frameConsumerDoneFence	   = frameSynchronizationInfo.frameConsumerDoneFence;
	VkSemaphore frameCompleteSemaphore	   = frameSynchronizationInfo.frameCompleteSemaphore;
	VkSemaphore frameConsumerDoneSemaphore = frameSynchronizationInfo.frameConsumerDoneSemaphore;

	// Check here that the frame for this entry (for this command buffer) has already completed decoding.
	// Otherwise we may step over a hot command buffer by starting a new recording.
	// This fence wait should be NOP in 99.9% of the cases, because the decode queue is deep enough to
	// ensure the frame has already been completed.
	VkResult	result					   = m_deviceContext->getDeviceDriver().waitForFences(m_deviceContext->device, 1, &frameCompleteFence, true, 100 * 1000 * 1000);
	if (result != VK_SUCCESS)
	{
		std::cout << "\t *************** WARNING: frameCompleteFence is not done *************< " << currPicIdx << " >**********************" << std::endl;
		assert(!"frameCompleteFence is not signaled yet after 100 mSec wait");
	}

	result = m_deviceContext->getDeviceDriver().getFenceStatus(m_deviceContext->device, frameCompleteFence);
	if (result == VK_NOT_READY)
	{
		std::cout << "\t *************** WARNING: frameCompleteFence is not done *************< " << currPicIdx << " >**********************" << std::endl;
		assert(!"frameCompleteFence is not signaled yet");
	}

	VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginInfo.sType					   = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags					   = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo		   = nullptr;

	auto& vk						   = m_deviceContext->getDeviceDriver();
	auto  device					   = m_deviceContext->device;

	vk.beginCommandBuffer(frameDataSlot.commandBuffer, &beginInfo);

	if (m_queryResultWithStatus)
	{
		vk.cmdResetQueryPool(frameDataSlot.commandBuffer, frameSynchronizationInfo.queryPool, frameSynchronizationInfo.startQueryId, frameSynchronizationInfo.numQueries);
	}

	vk.cmdBeginVideoCodingKHR(frameDataSlot.commandBuffer, &decodeBeginInfo);

	if (m_resetDecoder != false)
	{
		VkVideoCodingControlInfoKHR codingControlInfo = {VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR,
														 nullptr,
														 VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR};

		// Video spec requires mandatory codec reset before the first frame.
		vk.cmdControlVideoCodingKHR(frameDataSlot.commandBuffer, &codingControlInfo);
		// Done with the reset
		m_resetDecoder = false;
	}

	const VkDependencyInfoKHR dependencyInfo = {
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
		nullptr,
		VK_DEPENDENCY_BY_REGION_BIT,
		0,
		nullptr,
		1,
		&bitstreamBufferMemoryBarrier,
		numDpbBarriers,
		imageBarriers,
	};
	vk.cmdPipelineBarrier2(frameDataSlot.commandBuffer, &dependencyInfo);

	if (m_queryResultWithStatus)
	{
		vk.cmdBeginQuery(frameDataSlot.commandBuffer, frameSynchronizationInfo.queryPool, frameSynchronizationInfo.startQueryId, VkQueryControlFlags());
	}

	vk.cmdDecodeVideoKHR(frameDataSlot.commandBuffer, &pPicParams->decodeFrameInfo);

	if (m_queryResultWithStatus)
	{
		vk.cmdEndQuery(frameDataSlot.commandBuffer, frameSynchronizationInfo.queryPool, frameSynchronizationInfo.startQueryId);
	}

	VkVideoEndCodingInfoKHR decodeEndInfo = {VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};
	vk.cmdEndVideoCodingKHR(frameDataSlot.commandBuffer, &decodeEndInfo);

	//	if (m_dpbAndOutputCoincide && (m_useSeparateOutputImages || m_useLinearOutput)) {
	//		CopyOptimalToLinearImage(frameDataSlot.commandBuffer,
	//								 pPicParams->decodeFrameInfo.dstPictureResource,
	//								 currentDpbPictureResourceInfo,
	//								 *pOutputPictureResource,
	//								 *pOutputPictureResourceInfo,
	//								 &frameSynchronizationInfo);
	//	}

	m_deviceContext->getDeviceDriver().endCommandBuffer(frameDataSlot.commandBuffer);

	VkSubmitInfo submitInfo							 = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submitInfo.waitSemaphoreCount					 = (frameConsumerDoneSemaphore == VkSemaphore()) ? 0 : 1;
	submitInfo.pWaitSemaphores						 = &frameConsumerDoneSemaphore;
	VkPipelineStageFlags videoDecodeSubmitWaitStages = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
	submitInfo.pWaitDstStageMask					 = &videoDecodeSubmitWaitStages;
	submitInfo.commandBufferCount					 = 1;
	submitInfo.pCommandBuffers						 = &frameDataSlot.commandBuffer;
	submitInfo.signalSemaphoreCount					 = 1;
	submitInfo.pSignalSemaphores					 = &frameCompleteSemaphore;

	result											 = VK_SUCCESS;
	if ((frameConsumerDoneSemaphore == VkSemaphore()) && (frameConsumerDoneFence != VkFence()))
	{
		result = vk.waitForFences(device, 1, &frameConsumerDoneFence, true, 100 * 1000 * 1000);
		assert(result == VK_SUCCESS);
		result = vk.getFenceStatus(device, frameConsumerDoneFence);
		assert(result == VK_SUCCESS);
	}

	result = vk.resetFences(device, 1, &frameCompleteFence);
	assert(result == VK_SUCCESS);
	result = vk.getFenceStatus(device, frameCompleteFence);
	assert(result == VK_NOT_READY);

	VkResult res = vk.queueSubmit(m_deviceContext->decodeQueue, 1, &submitInfo, frameCompleteFence);
	VK_CHECK(res);

	if (videoLoggingEnabled())
	{
		std::cout << "\t +++++++++++++++++++++++++++< " << currPicIdx << " >++++++++++++++++++++++++++++++" << std::endl;
		std::cout << "\t => Decode Submitted for CurrPicIdx: " << currPicIdx << std::endl
				  << "\t\tm_nPicNumInDecodeOrder: " << picNumInDecodeOrder << "\t\tframeCompleteFence " << frameCompleteFence
				  << "\t\tframeCompleteSemaphore " << frameCompleteSemaphore << "\t\tdstImageView "
				  << pPicParams->decodeFrameInfo.dstPictureResource.imageViewBinding << std::endl;
	}

	const bool checkDecodeIdleSync = false; // For fence/sync/idle debugging
	if (checkDecodeIdleSync)
	{ // For fence/sync debugging
		if (frameCompleteFence == VkFence())
		{
			result = vk.queueWaitIdle(m_deviceContext->decodeQueue);
			assert(result == VK_SUCCESS);
		}
		else
		{
			if (frameCompleteSemaphore == VkSemaphore())
			{
				result = vk.waitForFences(device, 1, &frameCompleteFence, true, 100 * 1000 * 1000);
				assert(result == VK_SUCCESS);
				result = vk.getFenceStatus(device, frameCompleteFence);
				assert(result == VK_SUCCESS);
			}
		}
	}

	// For fence/sync debugging
	//	if (pDecodePictureInfo->flags.fieldPic) {
	//		result = m_vkDevCtx->WaitForFences(*m_vkDevCtx, 1, &frameCompleteFence, true, gFenceTimeout);
	//		assert(result == VK_SUCCESS);
	//		result = m_vkDevCtx->GetFenceStatus(*m_vkDevCtx, frameCompleteFence);
	//		assert(result == VK_SUCCESS);
	//	}

	if (m_queryResultWithStatus)
	{
		VkQueryResultStatusKHR decodeStatus;
		result = vk.getQueryPoolResults(device,
										frameSynchronizationInfo.queryPool,
										frameSynchronizationInfo.startQueryId,
										1,
										sizeof(decodeStatus),
										&decodeStatus,
										sizeof(decodeStatus),
										VK_QUERY_RESULT_WITH_STATUS_BIT_KHR | VK_QUERY_RESULT_WAIT_BIT);
		if (true || videoLoggingEnabled())
		{
			std::cout << "\t +++++++++++++++++++++++++++< " << currPicIdx << " >++++++++++++++++++++++++++++++" << std::endl;
			std::cout << "\t => Decode Status for CurrPicIdx: " << currPicIdx << std::endl
					  << "\t\tdecodeStatus: " << decodeStatus << std::endl;
		}

		TCU_CHECK_AND_THROW(TestError, result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST, "Driver has returned an invalid query result");
		TCU_CHECK_AND_THROW(TestError, decodeStatus != VK_QUERY_RESULT_STATUS_ERROR_KHR, "Decode query returned an unexpected error");
	}

	return currPicIdx;
}

bool VideoBaseDecoder::UpdatePictureParameters(VkSharedBaseObj<StdVideoPictureParametersSet>& pictureParametersObject, /* in */
											   VkSharedBaseObj<VkVideoRefCountBase>&		  client)
{
	VkResult result = VkParserVideoPictureParameters::AddPictureParameters(*m_deviceContext,
																		   m_videoSession,
																		   pictureParametersObject,
																		   m_currentPictureParameters);
	client			= m_currentPictureParameters;
	return (result == VK_SUCCESS);
}

bool VideoBaseDecoder::DisplayPicture (VkPicIf*	pNvidiaVulkanPicture,
									   int64_t					/*llPTS*/)
{
	bool result = false;

	vkPicBuffBase* pVkPicBuff = GetPic(pNvidiaVulkanPicture);

	DE_ASSERT(pVkPicBuff != DE_NULL);

	int32_t picIdx = pVkPicBuff ? pVkPicBuff->m_picIdx : -1;

	if (videoLoggingEnabled())
	{
		std::cout << "\t ======================< " << picIdx << " >============================" << std::endl;
		std::cout << "\t ==> VulkanVideoParser::DisplayPicture " << picIdx << std::endl;
	}
	DE_ASSERT(picIdx != -1);

	if (m_videoFrameBuffer != DE_NULL && picIdx != -1)
	{
		VulkanVideoDisplayPictureInfo dispInfo = VulkanVideoDisplayPictureInfo();

		dispInfo.timestamp = 0; // NOTE: we ignore PTS in the CTS

		const int32_t retVal = m_videoFrameBuffer->QueueDecodedPictureForDisplay((int8_t)picIdx, &dispInfo);

		DE_ASSERT(picIdx == retVal);
		DE_UNREF(retVal);

		result = true;
	}

	return result;
}

int32_t VideoBaseDecoder::ReleaseDisplayedFrame (DecodedFrame* pDisplayedFrame)
{
	if (pDisplayedFrame->pictureIndex != -1)
	{
		DecodedFrameRelease		decodedFramesRelease	= { pDisplayedFrame->pictureIndex, 0, 0, 0, 0, 0 };
		DecodedFrameRelease*	decodedFramesReleasePtr	= &decodedFramesRelease;

		pDisplayedFrame->pictureIndex = -1;

		decodedFramesRelease.decodeOrder = pDisplayedFrame->decodeOrder;
		decodedFramesRelease.displayOrder = pDisplayedFrame->displayOrder;

		decodedFramesRelease.hasConsummerSignalFence = pDisplayedFrame->hasConsummerSignalFence;
		decodedFramesRelease.hasConsummerSignalSemaphore = pDisplayedFrame->hasConsummerSignalSemaphore;
		decodedFramesRelease.timestamp = 0;

		return m_videoFrameBuffer->ReleaseDisplayedPicture(&decodedFramesReleasePtr, 1);
	}

	return -1;
}


VkDeviceSize VideoBaseDecoder::GetBitstreamBuffer(size_t size, size_t minBitstreamBufferOffsetAlignment, size_t minBitstreamBufferSizeAlignment, const uint8_t *pInitializeBufferMemory,
												  size_t initializeBufferMemorySize, VkSharedBaseObj<VulkanBitstreamBuffer> &bitstreamBuffer)
{
	assert(initializeBufferMemorySize <= size);
	// size_t newSize = 4 * 1024 * 1024;
	size_t newSize = size;

	VkSharedBaseObj<VulkanBitstreamBufferImpl> newBitstreamBuffer;

	const bool enablePool = true;
	int32_t availablePoolNode = -1;
	if (enablePool) {
		availablePoolNode = m_decodeFramesData.GetBitstreamBuffersQueue().GetAvailableNodeFromPool(newBitstreamBuffer);
	}
	if (!(availablePoolNode >= 0)) {
		VkResult result = VulkanBitstreamBufferImpl::Create(m_deviceContext,
															m_deviceContext->decodeQueueFamilyIdx(),
															newSize, minBitstreamBufferOffsetAlignment,
															minBitstreamBufferSizeAlignment,
															pInitializeBufferMemory, initializeBufferMemorySize, newBitstreamBuffer,
															m_profile.GetProfileListInfo());
		assert(result == VK_SUCCESS);
		if (result != VK_SUCCESS) {
			tcu::print("ERROR: CreateVideoBitstreamBuffer() result: 0x%x\n", result);
			return 0;
		}
		if (videoLoggingEnabled()) {
			std::cout << "\tAllocated bitstream buffer with size " << newSize << " B, " <<
					  newSize/1024 << " KB, " << newSize/1024/1024 << " MB" << std::endl;
		}
		if (enablePool) {
			int32_t nodeAddedWithIndex = m_decodeFramesData.GetBitstreamBuffersQueue().AddNodeToPool(newBitstreamBuffer, true);
			if (nodeAddedWithIndex < 0) {
				assert("Could not add the new node to the pool");
			}
		}

	} else {

		assert(newBitstreamBuffer);
		newSize = newBitstreamBuffer->GetMaxSize();
		assert(initializeBufferMemorySize <= newSize);

		size_t copySize = std::min(initializeBufferMemorySize, newSize);
		newBitstreamBuffer->CopyDataFromBuffer((const uint8_t*)pInitializeBufferMemory,
											   0, // srcOffset
											   0, // dstOffset
											   copySize);

		newBitstreamBuffer->MemsetData(0x0, copySize, newSize - copySize);

		if (videoLoggingEnabled()) {
			std::cout << "\t\tFrom bitstream buffer pool with size " << newSize << " B, " <<
					  newSize/1024 << " KB, " << newSize/1024/1024 << " MB" << std::endl;

			std::cout << "\t\t\t FreeNodes " << m_decodeFramesData.GetBitstreamBuffersQueue().GetFreeNodesNumber();
			std::cout << " of MaxNodes " << m_decodeFramesData.GetBitstreamBuffersQueue().GetMaxNodes();
			std::cout << ", AvailableNodes " << m_decodeFramesData.GetBitstreamBuffersQueue().GetAvailableNodesNumber();
			std::cout << std::endl;
		}
	}
	bitstreamBuffer = newBitstreamBuffer;
	if (newSize > m_maxStreamBufferSize) {
		std::cout << "\tAllocated bitstream buffer with size " << newSize << " B, " <<
				  newSize/1024 << " KB, " << newSize/1024/1024 << " MB" << std::endl;
		m_maxStreamBufferSize = newSize;
	}
	return bitstreamBuffer->GetMaxSize();
}


void VideoBaseDecoder::UnhandledNALU (const uint8_t*	pbData,
									  size_t			cbData)
{
	const vector<uint8_t> data (pbData, pbData + cbData);
	ostringstream css;

	css << "UnhandledNALU=";

	for (const auto& i: data)
		css << std::hex << std::setw(2) << std::setfill('0') << (deUint32)i << ' ';

	TCU_THROW(InternalError, css.str());
}

uint32_t VideoBaseDecoder::FillDpbH264State (const VkParserPictureData *	pd,
											 const VkParserH264DpbEntry*	dpbIn,
											 uint32_t								maxDpbInSlotsInUse,
											 nvVideoDecodeH264DpbSlotInfo*		pDpbRefList,
											 uint32_t			/*maxRefPictures*/,
											 VkVideoReferenceSlotInfoKHR*			pReferenceSlots,
											 int8_t*								pGopReferenceImagesIndexes,
											 StdVideoDecodeH264PictureInfoFlags		currPicFlags,
											 int32_t*								pCurrAllocatedSlotIndex)
{
	// #### Update m_dpb based on dpb parameters ####
	// Create unordered DPB and generate a bitmask of all render targets present
	// in DPB
	uint32_t num_ref_frames = pd->CodecSpecific.h264.pStdSps->GetStdH264Sps()->max_num_ref_frames;
	assert(num_ref_frames <= HEVC_MAX_DPB_SLOTS);
	assert(num_ref_frames <= m_maxNumDpbSlots);
	dpbH264Entry refOnlyDpbIn[AVC_MAX_DPB_SLOTS]; // max number of Dpb
	// surfaces
	memset(&refOnlyDpbIn, 0, m_maxNumDpbSlots * sizeof(refOnlyDpbIn[0]));
	uint32_t refDpbUsedAndValidMask = 0;
	uint32_t numUsedRef = 0;
	for (int32_t inIdx = 0; (uint32_t)inIdx < maxDpbInSlotsInUse; inIdx++) {
		// used_for_reference: 0 = unused, 1 = top_field, 2 = bottom_field, 3 =
		// both_fields
		const uint32_t used_for_reference = dpbIn[inIdx].used_for_reference & fieldIsReferenceMask;
		if (used_for_reference) {
			int8_t picIdx = (!dpbIn[inIdx].not_existing && dpbIn[inIdx].pPicBuf)
							? GetPicIdx(dpbIn[inIdx].pPicBuf)
							: -1;
			const bool isFieldRef = (picIdx >= 0) ? GetFieldPicFlag(picIdx)
												  : (used_for_reference && (used_for_reference != fieldIsReferenceMask));
			const int16_t fieldOrderCntList[2] = {
					(int16_t)dpbIn[inIdx].FieldOrderCnt[0],
					(int16_t)dpbIn[inIdx].FieldOrderCnt[1]
			};
			refOnlyDpbIn[numUsedRef].setReferenceAndTopBottomField(
					!!used_for_reference,
					(picIdx < 0), /* not_existing is frame inferred by the decoding
                           process for gaps in frame_num */
					!!dpbIn[inIdx].is_long_term, isFieldRef,
					!!(used_for_reference & topFieldMask),
					!!(used_for_reference & bottomFieldMask), dpbIn[inIdx].FrameIdx,
					fieldOrderCntList, GetPic(dpbIn[inIdx].pPicBuf));
			if (picIdx >= 0) {
				refDpbUsedAndValidMask |= (1 << picIdx);
			}
			numUsedRef++;
		}
		// Invalidate all slots.
		pReferenceSlots[inIdx].slotIndex = -1;
		pGopReferenceImagesIndexes[inIdx] = -1;
	}

	assert(numUsedRef <= HEVC_MAX_DPB_SLOTS);
	assert(numUsedRef <= m_maxNumDpbSlots);
	assert(numUsedRef <= num_ref_frames);

	if (videoLoggingEnabled()) {
		std::cout << " =>>> ********************* picIdx: "
				  << (int32_t)GetPicIdx(pd->pCurrPic)
				  << " *************************" << std::endl;
		std::cout << "\tRef frames data in for picIdx: "
				  << (int32_t)GetPicIdx(pd->pCurrPic) << std::endl
				  << "\tSlot Index:\t\t";
		if (numUsedRef == 0)
			std::cout << "(none)" << std::endl;
		else
		{
			for (uint32_t slot = 0; slot < numUsedRef; slot++)
			{
				if (!refOnlyDpbIn[slot].is_non_existing)
				{
					std::cout << slot << ",\t";
				}
				else
				{
					std::cout << 'X' << ",\t";
				}
			}
			std::cout << std::endl;
		}
		std::cout << "\tPict Index:\t\t";
		if (numUsedRef == 0)
			std::cout << "(none)" << std::endl;
		else
		{
			for (uint32_t slot = 0; slot < numUsedRef; slot++)
			{
				if (!refOnlyDpbIn[slot].is_non_existing)
				{
					std::cout << refOnlyDpbIn[slot].m_picBuff->m_picIdx << ",\t";
				}
				else
				{
					std::cout << 'X' << ",\t";
				}
			}
		}
		std::cout << "\n\tTotal Ref frames for picIdx: "
				  << (int32_t)GetPicIdx(pd->pCurrPic) << " : " << numUsedRef
				  << " out of " << num_ref_frames << " MAX(" << m_maxNumDpbSlots
				  << ")" << std::endl
				  << std::endl;

		std::cout << std::flush;
	}

	// Map all frames not present in DPB as non-reference, and generate a mask of
	// all used DPB entries
	/* uint32_t destUsedDpbMask = */ ResetPicDpbSlots(refDpbUsedAndValidMask);

	// Now, map DPB render target indices to internal frame buffer index,
	// assign each reference a unique DPB entry, and create the ordered DPB
	// This is an undocumented MV restriction: the position in the DPB is stored
	// along with the co-located data, so once a reference frame is assigned a DPB
	// entry, it can no longer change.

	// Find or allocate slots for existing dpb items.
	// Take into account the reference picture now.
	int8_t currPicIdx = GetPicIdx(pd->pCurrPic);
	assert(currPicIdx >= 0);
	int8_t bestNonExistingPicIdx = currPicIdx;
	if (refDpbUsedAndValidMask) {
		int32_t minFrameNumDiff = 0x10000;
		for (int32_t dpbIdx = 0; (uint32_t)dpbIdx < numUsedRef; dpbIdx++) {
			if (!refOnlyDpbIn[dpbIdx].is_non_existing) {
				vkPicBuffBase* picBuff = refOnlyDpbIn[dpbIdx].m_picBuff;
				int8_t picIdx = GetPicIdx(picBuff); // should always be valid at this point
				assert(picIdx >= 0);
				// We have up to 17 internal frame buffers, but only MAX_DPB_SIZE dpb
				// entries, so we need to re-map the index from the [0..MAX_DPB_SIZE]
				// range to [0..15]
				int8_t dpbSlot = GetPicDpbSlot(picIdx);
				if (dpbSlot < 0) {
					dpbSlot = m_dpb.AllocateSlot();
					assert((dpbSlot >= 0) && ((uint32_t)dpbSlot < m_maxNumDpbSlots));
					SetPicDpbSlot(picIdx, dpbSlot);
					m_dpb[dpbSlot].setPictureResource(picBuff, m_nCurrentPictureID);
				}
				m_dpb[dpbSlot].MarkInUse(m_nCurrentPictureID);
				assert(dpbSlot >= 0);

				if (dpbSlot >= 0) {
					refOnlyDpbIn[dpbIdx].dpbSlot = dpbSlot;
				} else {
					// This should never happen
					printf("DPB mapping logic broken!\n");
					assert(0);
				}

				int32_t frameNumDiff = ((int32_t)pd->CodecSpecific.h264.frame_num - refOnlyDpbIn[dpbIdx].FrameIdx);
				if (frameNumDiff <= 0) {
					frameNumDiff = 0xffff;
				}
				if (frameNumDiff < minFrameNumDiff) {
					bestNonExistingPicIdx = picIdx;
					minFrameNumDiff = frameNumDiff;
				} else if (bestNonExistingPicIdx == currPicIdx) {
					bestNonExistingPicIdx = picIdx;
				}
			}
		}
	}
	// In Vulkan, we always allocate a Dbp slot for the current picture,
	// regardless if it is going to become a reference or not. Non-reference slots
	// get freed right after usage. if (pd->ref_pic_flag) {
	int8_t currPicDpbSlot = AllocateDpbSlotForCurrentH264(GetPic(pd->pCurrPic),
														  currPicFlags, pd->current_dpb_id);
	assert(currPicDpbSlot >= 0);
	*pCurrAllocatedSlotIndex = currPicDpbSlot;

	if (refDpbUsedAndValidMask) {
		// Find or allocate slots for non existing dpb items and populate the slots.
		uint32_t dpbInUseMask = m_dpb.getSlotInUseMask();
		int8_t firstNonExistingDpbSlot = 0;
		for (uint32_t dpbIdx = 0; dpbIdx < numUsedRef; dpbIdx++) {
			int8_t dpbSlot = -1;
			int8_t picIdx = -1;
			if (refOnlyDpbIn[dpbIdx].is_non_existing) {
				assert(refOnlyDpbIn[dpbIdx].m_picBuff == NULL);
				while (((uint32_t)firstNonExistingDpbSlot < m_maxNumDpbSlots) && (dpbSlot == -1)) {
					if (!(dpbInUseMask & (1 << firstNonExistingDpbSlot))) {
						dpbSlot = firstNonExistingDpbSlot;
					}
					firstNonExistingDpbSlot++;
				}
				assert((dpbSlot >= 0) && ((uint32_t)dpbSlot < m_maxNumDpbSlots));
				picIdx = bestNonExistingPicIdx;
				// Find the closest valid refpic already in the DPB
				uint32_t minDiffPOC = 0x7fff;
				for (uint32_t j = 0; j < numUsedRef; j++) {
					if (!refOnlyDpbIn[j].is_non_existing && (refOnlyDpbIn[j].used_for_reference & refOnlyDpbIn[dpbIdx].used_for_reference) == refOnlyDpbIn[dpbIdx].used_for_reference) {
						uint32_t diffPOC = abs((int32_t)(refOnlyDpbIn[j].FieldOrderCnt[0] - refOnlyDpbIn[dpbIdx].FieldOrderCnt[0]));
						if (diffPOC <= minDiffPOC) {
							minDiffPOC = diffPOC;
							picIdx = GetPicIdx(refOnlyDpbIn[j].m_picBuff);
						}
					}
				}
			} else {
				assert(refOnlyDpbIn[dpbIdx].m_picBuff != NULL);
				dpbSlot = refOnlyDpbIn[dpbIdx].dpbSlot;
				picIdx = GetPicIdx(refOnlyDpbIn[dpbIdx].m_picBuff);
			}
			assert((dpbSlot >= 0) && ((uint32_t)dpbSlot < m_maxNumDpbSlots));
			refOnlyDpbIn[dpbIdx].setH264PictureData(pDpbRefList, pReferenceSlots,
													dpbIdx, dpbSlot, pd->progressive_frame);
			pGopReferenceImagesIndexes[dpbIdx] = picIdx;
		}
	}

	if (videoLoggingEnabled()) {
		uint32_t slotInUseMask = m_dpb.getSlotInUseMask();
		uint32_t slotsInUseCount = 0;
		std::cout << "\tAllocated DPB slot " << (int32_t)currPicDpbSlot << " for "
				  << (pd->ref_pic_flag ? "REFERENCE" : "NON-REFERENCE")
				  << " picIdx: " << (int32_t)currPicIdx << std::endl;
		std::cout << "\tDPB frames map for picIdx: " << (int32_t)currPicIdx
				  << std::endl
				  << "\tSlot Index:\t\t";
		for (uint32_t slot = 0; slot < m_dpb.getMaxSize(); slot++) {
			if (slotInUseMask & (1 << slot)) {
				std::cout << slot << ",\t";
				slotsInUseCount++;
			} else {
				std::cout << 'X' << ",\t";
			}
		}
		std::cout << std::endl
				  << "\tPict Index:\t\t";
		for (uint32_t slot = 0; slot < m_dpb.getMaxSize(); slot++) {
			if (slotInUseMask & (1 << slot)) {
				if (m_dpb[slot].getPictureResource()) {
					std::cout << m_dpb[slot].getPictureResource()->m_picIdx << ",\t";
				} else {
					std::cout << "non existent"
							  << ",\t";
				}
			} else {
				std::cout << 'X' << ",\t";
			}
		}
		std::cout << "\n\tTotal slots in use for picIdx: " << (int32_t)currPicIdx
				  << " : " << slotsInUseCount << " out of " << m_dpb.getMaxSize()
				  << std::endl;
		std::cout << " <<<= ********************* picIdx: "
				  << (int32_t)GetPicIdx(pd->pCurrPic)
				  << " *************************" << std::endl
				  << std::endl;
		std::cout << std::flush;
	}
	return refDpbUsedAndValidMask ? numUsedRef : 0;}

uint32_t VideoBaseDecoder::FillDpbH265State (const VkParserPictureData* pd,
											 const VkParserHevcPictureData* pin,
											 nvVideoDecodeH265DpbSlotInfo* pDpbSlotInfo,
											 StdVideoDecodeH265PictureInfo* pStdPictureInfo,
											 uint32_t /*maxRefPictures*/,
											 VkVideoReferenceSlotInfoKHR* pReferenceSlots,
											 int8_t* pGopReferenceImagesIndexes,
											 int32_t* pCurrAllocatedSlotIndex)
{
	// #### Update m_dpb based on dpb parameters ####
	// Create unordered DPB and generate a bitmask of all render targets present
	// in DPB
	dpbH264Entry refOnlyDpbIn[HEVC_MAX_DPB_SLOTS];
	assert(m_maxNumDpbSlots <= HEVC_MAX_DPB_SLOTS);
	memset(&refOnlyDpbIn, 0, m_maxNumDpbSlots * sizeof(refOnlyDpbIn[0]));
	uint32_t refDpbUsedAndValidMask = 0;
	uint32_t numUsedRef = 0;
	if (videoLoggingEnabled())
		std::cout << "Ref frames data: " << std::endl;
	for (int32_t inIdx = 0; inIdx < HEVC_MAX_DPB_SLOTS; inIdx++) {
		// used_for_reference: 0 = unused, 1 = top_field, 2 = bottom_field, 3 =
		// both_fields
		int8_t picIdx = GetPicIdx(pin->RefPics[inIdx]);
		if (picIdx >= 0) {
			assert(numUsedRef < HEVC_MAX_DPB_SLOTS);
			refOnlyDpbIn[numUsedRef].setReference((pin->IsLongTerm[inIdx] == 1),
												  pin->PicOrderCntVal[inIdx],
												  GetPic(pin->RefPics[inIdx]));
			if (picIdx >= 0) {
				refDpbUsedAndValidMask |= (1 << picIdx);
			}
			refOnlyDpbIn[numUsedRef].originalDpbIndex = inIdx;
			numUsedRef++;
		}
		// Invalidate all slots.
		pReferenceSlots[inIdx].slotIndex = -1;
		pGopReferenceImagesIndexes[inIdx] = -1;
	}

	if (videoLoggingEnabled())
		std::cout << "Total Ref frames: " << numUsedRef << std::endl;

	assert(numUsedRef <= m_maxNumDpbSlots);
	assert(numUsedRef <= HEVC_MAX_DPB_SLOTS);

	// Take into account the reference picture now.
	int8_t currPicIdx = GetPicIdx(pd->pCurrPic);
	assert(currPicIdx >= 0);
	if (currPicIdx >= 0) {
		refDpbUsedAndValidMask |= (1 << currPicIdx);
	}

	// Map all frames not present in DPB as non-reference, and generate a mask of
	// all used DPB entries
	/* uint32_t destUsedDpbMask = */ ResetPicDpbSlots(refDpbUsedAndValidMask);

	// Now, map DPB render target indices to internal frame buffer index,
	// assign each reference a unique DPB entry, and create the ordered DPB
	// This is an undocumented MV restriction: the position in the DPB is stored
	// along with the co-located data, so once a reference frame is assigned a DPB
	// entry, it can no longer change.

	int8_t frmListToDpb[HEVC_MAX_DPB_SLOTS];
	// TODO change to -1 for invalid indexes.
	memset(&frmListToDpb, 0, sizeof(frmListToDpb));
	// Find or allocate slots for existing dpb items.
	for (int32_t dpbIdx = 0; (uint32_t)dpbIdx < numUsedRef; dpbIdx++) {
		if (!refOnlyDpbIn[dpbIdx].is_non_existing) {
			vkPicBuffBase* picBuff = refOnlyDpbIn[dpbIdx].m_picBuff;
			int32_t picIdx = GetPicIdx(picBuff); // should always be valid at this point
			assert(picIdx >= 0);
			// We have up to 17 internal frame buffers, but only HEVC_MAX_DPB_SLOTS
			// dpb entries, so we need to re-map the index from the
			// [0..HEVC_MAX_DPB_SLOTS] range to [0..15]
			int8_t dpbSlot = GetPicDpbSlot(picIdx);
			if (dpbSlot < 0) {
				dpbSlot = m_dpb.AllocateSlot();
				assert(dpbSlot >= 0);
				SetPicDpbSlot(picIdx, dpbSlot);
				m_dpb[dpbSlot].setPictureResource(picBuff, m_nCurrentPictureID);
			}
			m_dpb[dpbSlot].MarkInUse(m_nCurrentPictureID);
			assert(dpbSlot >= 0);

			if (dpbSlot >= 0) {
				refOnlyDpbIn[dpbIdx].dpbSlot = dpbSlot;
				uint32_t originalDpbIndex = refOnlyDpbIn[dpbIdx].originalDpbIndex;
				assert(originalDpbIndex < HEVC_MAX_DPB_SLOTS);
				frmListToDpb[originalDpbIndex] = dpbSlot;
			} else {
				// This should never happen
				printf("DPB mapping logic broken!\n");
				assert(0);
			}
		}
	}

	// Find or allocate slots for non existing dpb items and populate the slots.
	uint32_t dpbInUseMask = m_dpb.getSlotInUseMask();
	int8_t firstNonExistingDpbSlot = 0;
	for (uint32_t dpbIdx = 0; dpbIdx < numUsedRef; dpbIdx++) {
		int8_t dpbSlot = -1;
		if (refOnlyDpbIn[dpbIdx].is_non_existing) {
			// There shouldn't be  not_existing in h.265
			assert(0);
			assert(refOnlyDpbIn[dpbIdx].m_picBuff == NULL);
			while (((uint32_t)firstNonExistingDpbSlot < m_maxNumDpbSlots) && (dpbSlot == -1)) {
				if (!(dpbInUseMask & (1 << firstNonExistingDpbSlot))) {
					dpbSlot = firstNonExistingDpbSlot;
				}
				firstNonExistingDpbSlot++;
			}
			assert((dpbSlot >= 0) && ((uint32_t)dpbSlot < m_maxNumDpbSlots));
		} else {
			assert(refOnlyDpbIn[dpbIdx].m_picBuff != NULL);
			dpbSlot = refOnlyDpbIn[dpbIdx].dpbSlot;
		}
		assert((dpbSlot >= 0) && (dpbSlot < HEVC_MAX_DPB_SLOTS));
		refOnlyDpbIn[dpbIdx].setH265PictureData(pDpbSlotInfo, pReferenceSlots,
												dpbIdx, dpbSlot);
		pGopReferenceImagesIndexes[dpbIdx] = GetPicIdx(refOnlyDpbIn[dpbIdx].m_picBuff);
	}

	if (videoLoggingEnabled()) {
		std::cout << "frmListToDpb:" << std::endl;
		for (int8_t dpbResIdx = 0; dpbResIdx < HEVC_MAX_DPB_SLOTS; dpbResIdx++) {
			std::cout << "\tfrmListToDpb[" << (int32_t)dpbResIdx << "] is "
					  << (int32_t)frmListToDpb[dpbResIdx] << std::endl;
		}
	}

	int32_t numPocTotalCurr = 0;
	int32_t numPocStCurrBefore = 0;
	const size_t maxNumPocStCurrBefore = sizeof(pStdPictureInfo->RefPicSetStCurrBefore) / sizeof(pStdPictureInfo->RefPicSetStCurrBefore[0]);
	assert((size_t)pin->NumPocStCurrBefore <= maxNumPocStCurrBefore);
	if ((size_t)pin->NumPocStCurrBefore > maxNumPocStCurrBefore) {
		tcu::print("\nERROR: FillDpbH265State() pin->NumPocStCurrBefore(%d) must be smaller than maxNumPocStCurrBefore(%zd)\n", pin->NumPocStCurrBefore, maxNumPocStCurrBefore);
	}
	for (int32_t i = 0; i < pin->NumPocStCurrBefore; i++) {
		uint8_t idx = (uint8_t)pin->RefPicSetStCurrBefore[i];
		if (idx < HEVC_MAX_DPB_SLOTS) {
			if (videoLoggingEnabled())
				std::cout << "\trefPicSetStCurrBefore[" << i << "] is " << (int32_t)idx
						  << " -> " << (int32_t)frmListToDpb[idx] << std::endl;
			pStdPictureInfo->RefPicSetStCurrBefore[numPocStCurrBefore++] = frmListToDpb[idx] & 0xf;
			numPocTotalCurr++;
		}
	}
	while (numPocStCurrBefore < 8) {
		pStdPictureInfo->RefPicSetStCurrBefore[numPocStCurrBefore++] = 0xff;
	}

	int32_t numPocStCurrAfter = 0;
	const size_t maxNumPocStCurrAfter = sizeof(pStdPictureInfo->RefPicSetStCurrAfter) / sizeof(pStdPictureInfo->RefPicSetStCurrAfter[0]);
	assert((size_t)pin->NumPocStCurrAfter <= maxNumPocStCurrAfter);
	if ((size_t)pin->NumPocStCurrAfter > maxNumPocStCurrAfter) {
		fprintf(stderr, "\nERROR: FillDpbH265State() pin->NumPocStCurrAfter(%d) must be smaller than maxNumPocStCurrAfter(%zd)\n", pin->NumPocStCurrAfter, maxNumPocStCurrAfter);
	}
	for (int32_t i = 0; i < pin->NumPocStCurrAfter; i++) {
		uint8_t idx = (uint8_t)pin->RefPicSetStCurrAfter[i];
		if (idx < HEVC_MAX_DPB_SLOTS) {
			if (videoLoggingEnabled())
				std::cout << "\trefPicSetStCurrAfter[" << i << "] is " << (int32_t)idx
						  << " -> " << (int32_t)frmListToDpb[idx] << std::endl;
			pStdPictureInfo->RefPicSetStCurrAfter[numPocStCurrAfter++] = frmListToDpb[idx] & 0xf;
			numPocTotalCurr++;
		}
	}
	while (numPocStCurrAfter < 8) {
		pStdPictureInfo->RefPicSetStCurrAfter[numPocStCurrAfter++] = 0xff;
	}

	int32_t numPocLtCurr = 0;
	const size_t maxNumPocLtCurr = sizeof(pStdPictureInfo->RefPicSetLtCurr) / sizeof(pStdPictureInfo->RefPicSetLtCurr[0]);
	assert((size_t)pin->NumPocLtCurr <= maxNumPocLtCurr);
	if ((size_t)pin->NumPocLtCurr > maxNumPocLtCurr) {
		fprintf(stderr, "\nERROR: FillDpbH265State() pin->NumPocLtCurr(%d) must be smaller than maxNumPocLtCurr(%zd)\n", pin->NumPocLtCurr, maxNumPocLtCurr);
	}
	for (int32_t i = 0; i < pin->NumPocLtCurr; i++) {
		uint8_t idx = (uint8_t)pin->RefPicSetLtCurr[i];
		if (idx < HEVC_MAX_DPB_SLOTS) {
			if (videoLoggingEnabled())
				std::cout << "\trefPicSetLtCurr[" << i << "] is " << (int32_t)idx
						  << " -> " << (int32_t)frmListToDpb[idx] << std::endl;
			pStdPictureInfo->RefPicSetLtCurr[numPocLtCurr++] = frmListToDpb[idx] & 0xf;
			numPocTotalCurr++;
		}
	}
	while (numPocLtCurr < 8) {
		pStdPictureInfo->RefPicSetLtCurr[numPocLtCurr++] = 0xff;
	}

	for (int32_t i = 0; i < 8; i++) {
		if (videoLoggingEnabled())
			std::cout << "\tlist indx " << i << ": "
					  << " refPicSetStCurrBefore: "
					  << (int32_t)pStdPictureInfo->RefPicSetStCurrBefore[i]
					  << " refPicSetStCurrAfter: "
					  << (int32_t)pStdPictureInfo->RefPicSetStCurrAfter[i]
					  << " refPicSetLtCurr: "
					  << (int32_t)pStdPictureInfo->RefPicSetLtCurr[i] << std::endl;
	}

	int8_t dpbSlot = AllocateDpbSlotForCurrentH265(GetPic(pd->pCurrPic),
												   true /* isReference */, pd->current_dpb_id);
	*pCurrAllocatedSlotIndex = dpbSlot;
	assert(!(dpbSlot < 0));
	if (dpbSlot >= 0) {
		assert(pd->ref_pic_flag);
	}

	return numUsedRef;
}

int8_t VideoBaseDecoder::AllocateDpbSlotForCurrentH264 (vkPicBuffBase* pPic, StdVideoDecodeH264PictureInfoFlags currPicFlags,
														int8_t /*presetDpbSlot*/)
{
	// Now, map the current render target
	int8_t dpbSlot = -1;
	int8_t currPicIdx = GetPicIdx(pPic);
	assert(currPicIdx >= 0);
	SetFieldPicFlag(currPicIdx, currPicFlags.field_pic_flag);
	// In Vulkan we always allocate reference slot for the current picture.
	if (true /* currPicFlags.is_reference */) {
		dpbSlot = GetPicDpbSlot(currPicIdx);
		if (dpbSlot < 0) {
			dpbSlot = m_dpb.AllocateSlot();
			assert(dpbSlot >= 0);
			SetPicDpbSlot(currPicIdx, dpbSlot);
			m_dpb[dpbSlot].setPictureResource(pPic, m_nCurrentPictureID);
		}
		assert(dpbSlot >= 0);
	}
	return dpbSlot;
}

int8_t VideoBaseDecoder::AllocateDpbSlotForCurrentH265 (vkPicBuffBase* pPic,
														bool isReference, int8_t /*presetDpbSlot*/)
{
	// Now, map the current render target
	int8_t dpbSlot = -1;
	int8_t currPicIdx = GetPicIdx(pPic);
	assert(currPicIdx >= 0);
	assert(isReference);
	if (isReference) {
		dpbSlot = GetPicDpbSlot(currPicIdx);
		if (dpbSlot < 0) {
			dpbSlot = m_dpb.AllocateSlot();
			assert(dpbSlot >= 0);
			SetPicDpbSlot(currPicIdx, dpbSlot);
			m_dpb[dpbSlot].setPictureResource(pPic, m_nCurrentPictureID);
		}
		assert(dpbSlot >= 0);
	}
	return dpbSlot;
}

VkFormat getRecommendedFormat (const vector<VkFormat>& formats, VkFormat recommendedFormat)
{
	if (formats.empty())
		return VK_FORMAT_UNDEFINED;
	else if (recommendedFormat != VK_FORMAT_UNDEFINED && std::find(formats.begin(), formats.end(), recommendedFormat) != formats.end())
		return recommendedFormat;
	else
		return formats[0];
}

vector<pair<VkFormat, VkImageUsageFlags>> getImageFormatAndUsageForOutputAndDPB (const InstanceInterface&	vk,
																				 const VkPhysicalDevice		physicalDevice,
																				 const VkVideoProfileListInfoKHR*	videoProfileList,
																				 const VkFormat				recommendedFormat,
																				 const bool					distinctDstDpbImages)
{
	const VkImageUsageFlags						dstFormatUsages		= VK_IMAGE_USAGE_TRANSFER_SRC_BIT
																	| VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
	const VkImageUsageFlags						dpbFormatUsages		= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
	const VkImageUsageFlags						bothImageUsages		= dstFormatUsages | dpbFormatUsages;
	VkFormat									dstFormat			= VK_FORMAT_UNDEFINED;
	VkFormat									dpbFormat			= VK_FORMAT_UNDEFINED;
	vector<pair<VkFormat, VkImageUsageFlags>>	result;

	// Check if both image usages are not supported on this platform
	if (!distinctDstDpbImages)
	{
		const MovePtr<vector<VkFormat>>				bothUsageFormats = getSupportedFormats(vk, physicalDevice, bothImageUsages, videoProfileList);
		VkFormat pickedFormat = getRecommendedFormat(*bothUsageFormats, recommendedFormat);

		result.push_back(pair<VkFormat, VkImageUsageFlags>(pickedFormat, bothImageUsages));
		result.push_back(pair<VkFormat, VkImageUsageFlags>(pickedFormat, VkImageUsageFlags()));
	}
	else
	{
		{
			const MovePtr<vector<VkFormat>>	dstUsageFormats = getSupportedFormats(vk, physicalDevice, dstFormatUsages, videoProfileList);

			if (dstUsageFormats == DE_NULL)
				TCU_FAIL("Implementation must report format for VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR");

			dstFormat = getRecommendedFormat(*dstUsageFormats, recommendedFormat);

			if (dstFormat == VK_FORMAT_UNDEFINED)
				TCU_FAIL("Implementation must report format for VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR");

			result.push_back(pair<VkFormat, VkImageUsageFlags>(dstFormat, dstFormatUsages));
		}

		{
			const MovePtr<vector<VkFormat>>	dpbUsageFormats = getSupportedFormats(vk, physicalDevice, dpbFormatUsages, videoProfileList);

			if (dpbUsageFormats == DE_NULL)
				TCU_FAIL("Implementation must report format for VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR");

			dpbFormat = getRecommendedFormat(*dpbUsageFormats, recommendedFormat);

			result.push_back(pair<VkFormat, VkImageUsageFlags>(dpbFormat, dpbFormatUsages));
		}
	}

	DE_ASSERT(result.size() == 2);

	return result;
}


VkResult VulkanVideoSession::Create(DeviceContext& vkDevCtx,
								   uint32_t            videoQueueFamily,
								   VkVideoCoreProfile* pVideoProfile,
								   VkFormat            pictureFormat,
								   const VkExtent2D&   maxCodedExtent,
								   VkFormat            referencePicturesFormat,
								   uint32_t            maxDpbSlots,
								   uint32_t            maxActiveReferencePictures,
								   VkSharedBaseObj<VulkanVideoSession>& videoSession)
{
	auto& vk = vkDevCtx.getDeviceDriver();
	auto device = vkDevCtx.device;

	VulkanVideoSession* pNewVideoSession = new VulkanVideoSession(vkDevCtx, pVideoProfile);

	static const VkExtensionProperties h264DecodeStdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };
	static const VkExtensionProperties h265DecodeStdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION };
	static const VkExtensionProperties h264EncodeStdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION };
	static const VkExtensionProperties h265EncodeStdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_SPEC_VERSION };

	VkVideoSessionCreateInfoKHR& createInfo = pNewVideoSession->m_createInfo;
	createInfo.flags = 0;
	createInfo.pVideoProfile = pVideoProfile->GetProfile();
	createInfo.queueFamilyIndex = videoQueueFamily;
	createInfo.pictureFormat = pictureFormat;
	createInfo.maxCodedExtent = maxCodedExtent;
	createInfo.maxDpbSlots = maxDpbSlots;
	createInfo.maxActiveReferencePictures = maxActiveReferencePictures;
	createInfo.referencePictureFormat = referencePicturesFormat;

	switch ((int32_t)pVideoProfile->GetCodecType()) {
		case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
			createInfo.pStdHeaderVersion = &h264DecodeStdExtensionVersion;
			break;
		case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
			createInfo.pStdHeaderVersion = &h265DecodeStdExtensionVersion;
			break;
		case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_EXT:
			createInfo.pStdHeaderVersion = &h264EncodeStdExtensionVersion;
			break;
		case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_EXT:
			createInfo.pStdHeaderVersion = &h265EncodeStdExtensionVersion;
			break;
		default:
			assert(0);
	}
	VkResult result = vk.createVideoSessionKHR(device, &createInfo, NULL, &pNewVideoSession->m_videoSession);
	if (result != VK_SUCCESS) {
		return result;
	}

	uint32_t videoSessionMemoryRequirementsCount = 0;
	VkVideoSessionMemoryRequirementsKHR decodeSessionMemoryRequirements[MAX_BOUND_MEMORY];
	// Get the count first
	result = vk.getVideoSessionMemoryRequirementsKHR(device, pNewVideoSession->m_videoSession,
															&videoSessionMemoryRequirementsCount, NULL);
	assert(result == VK_SUCCESS);
	assert(videoSessionMemoryRequirementsCount <= MAX_BOUND_MEMORY);

	memset(decodeSessionMemoryRequirements, 0x00, sizeof(decodeSessionMemoryRequirements));
	for (uint32_t i = 0; i < videoSessionMemoryRequirementsCount; i++) {
		decodeSessionMemoryRequirements[i].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
	}

	result = vk.getVideoSessionMemoryRequirementsKHR(device, pNewVideoSession->m_videoSession,
															&videoSessionMemoryRequirementsCount,
															decodeSessionMemoryRequirements);
	if (result != VK_SUCCESS) {
		return result;
	}

	uint32_t decodeSessionBindMemoryCount = videoSessionMemoryRequirementsCount;
	VkBindVideoSessionMemoryInfoKHR decodeSessionBindMemory[MAX_BOUND_MEMORY];

	for (uint32_t memIdx = 0; memIdx < decodeSessionBindMemoryCount; memIdx++) {

		uint32_t memoryTypeIndex = 0;
		uint32_t memoryTypeBits = decodeSessionMemoryRequirements[memIdx].memoryRequirements.memoryTypeBits;
		if (memoryTypeBits == 0) {
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		// Find an available memory type that satisfies the requested properties.
		for (; !(memoryTypeBits & 1); memoryTypeIndex++  ) {
			memoryTypeBits >>= 1;
		}

		VkMemoryAllocateInfo memInfo = {
				VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,                          // sType
				NULL,                                                            // pNext
				decodeSessionMemoryRequirements[memIdx].memoryRequirements.size, // allocationSize
				memoryTypeIndex,                                                 // memoryTypeIndex
		};

		result = vk.allocateMemory(device, &memInfo, 0,
										  &pNewVideoSession->m_memoryBound[memIdx]);
		if (result != VK_SUCCESS) {
			return result;
		}

		assert(result == VK_SUCCESS);
		decodeSessionBindMemory[memIdx].pNext = NULL;
		decodeSessionBindMemory[memIdx].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
		decodeSessionBindMemory[memIdx].memory = pNewVideoSession->m_memoryBound[memIdx];

		decodeSessionBindMemory[memIdx].memoryBindIndex = decodeSessionMemoryRequirements[memIdx].memoryBindIndex;
		decodeSessionBindMemory[memIdx].memoryOffset = 0;
		decodeSessionBindMemory[memIdx].memorySize = decodeSessionMemoryRequirements[memIdx].memoryRequirements.size;
	}

	result = vk.bindVideoSessionMemoryKHR(device, pNewVideoSession->m_videoSession, decodeSessionBindMemoryCount,
												 decodeSessionBindMemory);
	assert(result == VK_SUCCESS);

	videoSession = pNewVideoSession;

	// Make sure we do not use dangling (on the stack) pointers
	createInfo.pNext = nullptr;

	return result;
}


#if 0
void VideoBaseDecoder::SubmitQueue (VkCommandBufferSubmitInfoKHR*				commandBufferSubmitInfo,
									VkSubmitInfo2KHR*							submitInfo,
									VideoFrameBuffer::FrameSynchronizationInfo*	frameSynchronizationInfo,
									VkSemaphoreSubmitInfoKHR*					frameConsumerDoneSemaphore,
									VkSemaphoreSubmitInfoKHR*					frameCompleteSemaphore)
{
	const DeviceInterface&	vkd							= getDeviceDriver();
	const VkDevice			device						= getDevice();
	const VkQueue			queue						= getQueueDecode();
	const deUint32			waitSemaphoreCount			= (frameSynchronizationInfo->frameConsumerDoneSemaphore == DE_NULL) ? 0u : 1u;
	const deUint32			signalSemaphoreInfoCount	= (frameSynchronizationInfo->frameCompleteSemaphore == DE_NULL) ? 0u : 1u;

	*frameConsumerDoneSemaphore	= makeSemaphoreSubmitInfo(frameSynchronizationInfo->frameConsumerDoneSemaphore, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR);
	*frameCompleteSemaphore		= makeSemaphoreSubmitInfo(frameSynchronizationInfo->frameCompleteSemaphore, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR);

	*submitInfo =
	{
		VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,	//  VkStructureType						sType;
		DE_NULL,								//  const void*							pNext;
		0u,										//  VkSubmitFlagsKHR					flags;
		waitSemaphoreCount,						//  uint32_t							waitSemaphoreInfoCount;
		frameConsumerDoneSemaphore,				//  const VkSemaphoreSubmitInfoKHR*		pWaitSemaphoreInfos;
		1u,										//  uint32_t							commandBufferInfoCount;
		commandBufferSubmitInfo,				//  const VkCommandBufferSubmitInfoKHR*	pCommandBufferInfos;
		signalSemaphoreInfoCount,				//  uint32_t							signalSemaphoreInfoCount;
		frameCompleteSemaphore,					//  const VkSemaphoreSubmitInfoKHR*		pSignalSemaphoreInfos;
	};

	VkResult result = VK_SUCCESS;

	if ((frameSynchronizationInfo->frameConsumerDoneSemaphore == DE_NULL) && (frameSynchronizationInfo->frameConsumerDoneFence != DE_NULL))
		VK_CHECK(vkd.waitForFences(device, 1, &frameSynchronizationInfo->frameConsumerDoneFence, true, ~0ull));

	VK_CHECK(vkd.getFenceStatus(device, frameSynchronizationInfo->frameCompleteFence));

	VK_CHECK(vkd.resetFences(device, 1, &frameSynchronizationInfo->frameCompleteFence));

	result = vkd.getFenceStatus(device, frameSynchronizationInfo->frameCompleteFence);
	DE_ASSERT(result == VK_NOT_READY);
	DE_UNREF(result);

	vkd.queueSubmit2(queue, 1, submitInfo, frameSynchronizationInfo->frameCompleteFence);
}

void VideoBaseDecoder::SubmitQueue (vector<VkCommandBufferSubmitInfoKHR>&	commandBufferSubmitInfos,
									VkSubmitInfo2KHR*						submitInfo,
									const VkFence							frameCompleteFence,
									const vector<VkFence>&					frameConsumerDoneFence,
									const vector<VkSemaphoreSubmitInfoKHR>&	frameConsumerDoneSemaphores,
									const vector<VkSemaphoreSubmitInfoKHR>&	frameCompleteSemaphores)
{
	const DeviceInterface&	vkd		= getDeviceDriver();
	const VkDevice			device	= getDevice();
	const VkQueue			queue	= getQueueDecode();

	for (uint32_t ndx = 0; ndx < frameConsumerDoneSemaphores.size(); ++ndx)
		if ((frameConsumerDoneSemaphores[ndx].semaphore == DE_NULL) && (frameConsumerDoneFence[ndx] != DE_NULL))
			VK_CHECK(vkd.waitForFences(device, 1, &frameConsumerDoneFence[ndx], true, ~0ull));

	VK_CHECK(vkd.getFenceStatus(device, frameCompleteFence));
	VK_CHECK(vkd.resetFences(device, 1, &frameCompleteFence));
	DE_ASSERT(vkd.getFenceStatus(device, frameCompleteFence) == VK_NOT_READY);

	*submitInfo =
	{
		VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,			//  VkStructureType						sType;
		DE_NULL,										//  const void*							pNext;
		0,												//  VkSubmitFlagsKHR					flags;
		(deUint32)frameCompleteSemaphores.size(),		//  uint32_t							waitSemaphoreInfoCount;
		de::dataOrNull(frameCompleteSemaphores),		//  const VkSemaphoreSubmitInfoKHR*		pWaitSemaphoreInfos;
		(uint32_t)commandBufferSubmitInfos.size(),		//  uint32_t							commandBufferInfoCount;
		dataOrNullPtr(commandBufferSubmitInfos),		//  const VkCommandBufferSubmitInfoKHR*	pCommandBufferInfos;
		(deUint32)frameConsumerDoneSemaphores.size(),	//  uint32_t							signalSemaphoreInfoCount;
		de::dataOrNull(frameConsumerDoneSemaphores),	//  const VkSemaphoreSubmitInfoKHR*		pSignalSemaphoreInfos;
	};

	vkd.queueSubmit2(queue, 1, submitInfo, frameCompleteFence);
}
#endif

static VkSharedBaseObj<VkImageResourceView> emptyImageView;

class NvPerFrameDecodeResources : public vkPicBuffBase {
public:
	NvPerFrameDecodeResources()
			: m_picDispInfo()
			  , m_frameCompleteFence()
			  , m_frameCompleteSemaphore()
			  , m_frameConsumerDoneFence()
			  , m_frameConsumerDoneSemaphore()
			  , m_hasFrameCompleteSignalFence(false)
			  , m_hasFrameCompleteSignalSemaphore(false)
			  , m_hasConsummerSignalFence(false)
			  , m_hasConsummerSignalSemaphore(false)
			  , m_inDecodeQueue(false)
			  , m_inDisplayQueue(false)
			  , m_ownedByDisplay(false)
			  , m_recreateImage(false)
			  , m_currentDpbImageLayerLayout(VK_IMAGE_LAYOUT_UNDEFINED)
			  , m_currentOutputImageLayout(VK_IMAGE_LAYOUT_UNDEFINED)
			  , m_vkDevCtx()
			  , m_frameDpbImageView()
			  , m_outImageView()
	{
	}

	VkResult CreateImage( DeviceContext& vkDevCtx,
						  const VkImageCreateInfo* pDpbImageCreateInfo,
						  const VkImageCreateInfo* pOutImageCreateInfo,
						  VkMemoryPropertyFlags    dpbRequiredMemProps,
						  VkMemoryPropertyFlags    outRequiredMemProps,
						  uint32_t imageIndex,
						  VkSharedBaseObj<VkImageResource>&  imageArrayParent,
						  VkSharedBaseObj<VkImageResourceView>& imageViewArrayParent,
						  bool useSeparateOutputImage = false,
						  bool useLinearOutput = false);

	VkResult init(DeviceContext& vkDevCtx);

	void Deinit();

	NvPerFrameDecodeResources (const NvPerFrameDecodeResources &srcObj) = delete;
	NvPerFrameDecodeResources (NvPerFrameDecodeResources &&srcObj) = delete;

	~NvPerFrameDecodeResources()
	{
		Deinit();
	}

	VkSharedBaseObj<VkImageResourceView>& GetFrameImageView() {
		if (ImageExist()) {
			return m_frameDpbImageView;
		} else {
			return emptyImageView;
		}
	}

	VkSharedBaseObj<VkImageResourceView>& GetDisplayImageView() {
		if (ImageExist()) {
			return m_outImageView;
		} else {
			return emptyImageView;
		}
	}

	bool ImageExist() {

		return (!!m_frameDpbImageView && (m_frameDpbImageView->GetImageView() != VK_NULL_HANDLE));
	}

	bool GetImageSetNewLayout(VkImageLayout newDpbImageLayout,
							  VkVideoPictureResourceInfoKHR* pDpbPictureResource,
							  VulkanVideoFrameBuffer::PictureResourceInfo* pDpbPictureResourceInfo,
							  VkImageLayout newOutputImageLayout = VK_IMAGE_LAYOUT_MAX_ENUM,
							  VkVideoPictureResourceInfoKHR* pOutputPictureResource = nullptr,
							  VulkanVideoFrameBuffer::PictureResourceInfo* pOutputPictureResourceInfo = nullptr) {


		if (m_recreateImage || !ImageExist()) {
			return false;
		}

		if (pDpbPictureResourceInfo) {
			pDpbPictureResourceInfo->image = m_frameDpbImageView->GetImageResource()->GetImage();
			pDpbPictureResourceInfo->imageFormat = m_frameDpbImageView->GetImageResource()->GetImageCreateInfo().format;
			pDpbPictureResourceInfo->currentImageLayout = m_currentDpbImageLayerLayout;
		}

		if (VK_IMAGE_LAYOUT_MAX_ENUM != newDpbImageLayout) {
			m_currentDpbImageLayerLayout = newDpbImageLayout;
		}

		if (pDpbPictureResource) {
			pDpbPictureResource->imageViewBinding = m_frameDpbImageView->GetImageView();
		}

		if (pOutputPictureResourceInfo) {
			pOutputPictureResourceInfo->image = m_outImageView->GetImageResource()->GetImage();
			pOutputPictureResourceInfo->imageFormat = m_outImageView->GetImageResource()->GetImageCreateInfo().format;
			pOutputPictureResourceInfo->currentImageLayout = m_currentOutputImageLayout;
		}

		if (VK_IMAGE_LAYOUT_MAX_ENUM != newOutputImageLayout) {
			m_currentOutputImageLayout = newOutputImageLayout;
		}

		if (pOutputPictureResource) {
			pOutputPictureResource->imageViewBinding = m_outImageView->GetImageView();
		}

		return true;
	}

	VkParserDecodePictureInfo m_picDispInfo;
	VkFence m_frameCompleteFence;
	VkSemaphore m_frameCompleteSemaphore;
	VkFence m_frameConsumerDoneFence;
	VkSemaphore m_frameConsumerDoneSemaphore;
	uint32_t m_hasFrameCompleteSignalFence : 1;
	uint32_t m_hasFrameCompleteSignalSemaphore : 1;
	uint32_t m_hasConsummerSignalFence : 1;
	uint32_t m_hasConsummerSignalSemaphore : 1;
	uint32_t m_inDecodeQueue : 1;
	uint32_t m_inDisplayQueue : 1;
	uint32_t m_ownedByDisplay : 1;
	uint32_t m_recreateImage : 1;
	// VPS
	VkSharedBaseObj<VkVideoRefCountBase>  stdVps;
	// SPS
	VkSharedBaseObj<VkVideoRefCountBase>  stdSps;
	// PPS
	VkSharedBaseObj<VkVideoRefCountBase>  stdPps;
	// The bitstream Buffer
	VkSharedBaseObj<VkVideoRefCountBase>  bitstreamData;

private:
	VkImageLayout                        m_currentDpbImageLayerLayout;
	VkImageLayout                        m_currentOutputImageLayout;
	DeviceContext*						 m_vkDevCtx;
	VkSharedBaseObj<VkImageResourceView> m_frameDpbImageView;
	VkSharedBaseObj<VkImageResourceView> m_outImageView;
};

class NvPerFrameDecodeImageSet {
public:

	static constexpr size_t maxImages = 32;

	NvPerFrameDecodeImageSet()
			: m_queueFamilyIndex((uint32_t)-1)
			  , m_dpbImageCreateInfo()
			  , m_outImageCreateInfo()
			  , m_dpbRequiredMemProps(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			  , m_outRequiredMemProps(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			  , m_numImages(0)
			  , m_usesImageArray(false)
			  , m_usesImageViewArray(false)
			  , m_usesSeparateOutputImage(false)
			  , m_usesLinearOutput(false)
			  , m_perFrameDecodeResources(maxImages)
			  , m_imageArray()
			  , m_imageViewArray()
	{
	}

	int32_t init(DeviceContext& vkDevCtx,
				 const VkVideoProfileInfoKHR* pDecodeProfile,
				 uint32_t              numImages,
				 VkFormat              dpbImageFormat,
				 VkFormat              outImageFormat,
				 const VkExtent2D&     maxImageExtent,
				 VkImageUsageFlags     dpbImageUsage,
				 VkImageUsageFlags     outImageUsage,
				 uint32_t              queueFamilyIndex,
				 VkMemoryPropertyFlags dpbRequiredMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				 VkMemoryPropertyFlags outRequiredMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				 bool useImageArray = false,
				 bool useImageViewArray = false,
				 bool useSeparateOutputImages = false,
				 bool useLinearOutput = false);

	void Deinit();

	~NvPerFrameDecodeImageSet()
	{
		Deinit();
	}

	NvPerFrameDecodeResources& operator[](unsigned int index)
	{
		assert(index < m_perFrameDecodeResources.size());
		return m_perFrameDecodeResources[index];
	}

	size_t size()
	{
		return m_numImages;
	}

	VkResult GetImageSetNewLayout(DeviceContext& vkDevCtx,
								  uint32_t imageIndex,
								  VkImageLayout newDpbImageLayout,
								  VkVideoPictureResourceInfoKHR* pDpbPictureResource = nullptr,
								  VulkanVideoFrameBuffer::PictureResourceInfo* pDpbPictureResourceInfo = nullptr,
								  VkImageLayout newOutputImageLayout = VK_IMAGE_LAYOUT_MAX_ENUM,
								  VkVideoPictureResourceInfoKHR* pOutputPictureResource = nullptr,
								  VulkanVideoFrameBuffer::PictureResourceInfo* pOutputPictureResourceInfo = nullptr) {

		VkResult result = VK_SUCCESS;
		if (pDpbPictureResource) {
			if (m_imageViewArray) {
				// We have an image view that has the same number of layers as the image.
				// In that scenario, while specifying the resource, the API must specifically choose the image layer.
				pDpbPictureResource->baseArrayLayer = imageIndex;
			} else {
				// Let the image view sub-resource specify the image layer.
				pDpbPictureResource->baseArrayLayer = 0;
			}
		}

		if(pOutputPictureResource) {
			// Output pictures currently are only allocated as discrete
			// Let the image view sub-resource specify the image layer.
			pOutputPictureResource->baseArrayLayer = 0;
		}

		bool validImage = m_perFrameDecodeResources[imageIndex].GetImageSetNewLayout(
				newDpbImageLayout,
				pDpbPictureResource,
				pDpbPictureResourceInfo,
				newOutputImageLayout,
				pOutputPictureResource,
				pOutputPictureResourceInfo);

		if (!validImage) {
			result = m_perFrameDecodeResources[imageIndex].CreateImage(
					vkDevCtx,
					&m_dpbImageCreateInfo,
					&m_outImageCreateInfo,
					m_dpbRequiredMemProps,
					m_outRequiredMemProps,
					imageIndex,
					m_imageArray,
					m_imageViewArray,
					m_usesSeparateOutputImage,
					m_usesLinearOutput);

			if (result == VK_SUCCESS) {
				validImage = m_perFrameDecodeResources[imageIndex].GetImageSetNewLayout(
						newDpbImageLayout,
						pDpbPictureResource,
						pDpbPictureResourceInfo,
						newOutputImageLayout,
						pOutputPictureResource,
						pOutputPictureResourceInfo);

				assert(validImage);
			}
		}

		return result;
	}

private:
	uint32_t                             m_queueFamilyIndex;
	VkVideoCoreProfile                   m_videoProfile;
	VkImageCreateInfo                    m_dpbImageCreateInfo;
	VkImageCreateInfo                    m_outImageCreateInfo;
	VkMemoryPropertyFlags                m_dpbRequiredMemProps;
	VkMemoryPropertyFlags                m_outRequiredMemProps;
	uint32_t                             m_numImages;
	uint32_t                             m_usesImageArray:1;
	uint32_t                             m_usesImageViewArray:1;
	uint32_t                             m_usesSeparateOutputImage:1;
	uint32_t                             m_usesLinearOutput:1;
	std::vector<NvPerFrameDecodeResources> m_perFrameDecodeResources;
	VkSharedBaseObj<VkImageResource>     m_imageArray;     // must be valid if m_usesImageArray is true
	VkSharedBaseObj<VkImageResourceView> m_imageViewArray; // must be valid if m_usesImageViewArray is true
};

class VkVideoFrameBuffer : public VulkanVideoFrameBuffer {
public:
	static constexpr size_t maxFramebufferImages = 32;

	VkVideoFrameBuffer(DeviceContext& vkDevCtx, bool supportsQueries)
			: m_vkDevCtx(vkDevCtx),
			m_refCount(0),
			m_displayQueueMutex(),
			m_perFrameDecodeImageSet(),
			m_displayFrames(),
			m_supportsQueries(supportsQueries),
			m_queryPool(),
			m_ownedByDisplayMask(0),
			m_frameNumInDecodeOrder(0),
			m_frameNumInDisplayOrder(0),
			m_codedExtent{0, 0},
			m_numberParameterUpdates(0)
	{ }

	virtual int32_t AddRef();
	virtual int32_t Release();

	VkResult CreateVideoQueries(uint32_t numSlots, DeviceContext& vkDevCtx,
								const VkVideoProfileInfoKHR* pDecodeProfile) {
		assert(numSlots <= maxFramebufferImages);

		auto& vk = vkDevCtx.context->getDeviceInterface();

		if (m_queryPool == VkQueryPool()) {
			// It would be difficult to resize a query pool, so allocate the maximum possible slot.
			numSlots = maxFramebufferImages;
			VkQueryPoolCreateInfo queryPoolCreateInfo = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
			queryPoolCreateInfo.pNext = pDecodeProfile;
			queryPoolCreateInfo.queryType = VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR;
			queryPoolCreateInfo.queryCount = numSlots;  // m_numDecodeSurfaces frames worth

			return vk.createQueryPool(vkDevCtx.device, &queryPoolCreateInfo, NULL, &m_queryPool);
		}

		return VK_SUCCESS;
	}

	void DestroyVideoQueries() {
		if (m_queryPool != VkQueryPool()) {
			m_vkDevCtx.getDeviceDriver().destroyQueryPool(m_vkDevCtx.device, m_queryPool, NULL);
			m_queryPool = VkQueryPool();
		}
	}

	uint32_t FlushDisplayQueue() {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);

		uint32_t flushedImages = 0;
		while (!m_displayFrames.empty()) {
			int8_t pictureIndex = m_displayFrames.front();
			assert((pictureIndex >= 0) && ((uint32_t)pictureIndex < m_perFrameDecodeImageSet.size()));
			m_displayFrames.pop();
			if (m_perFrameDecodeImageSet[(uint32_t)pictureIndex].IsAvailable()) {
				// The frame is not released yet - force release it.
				m_perFrameDecodeImageSet[(uint32_t)pictureIndex].Release();
			}
			flushedImages++;
		}

		return flushedImages;
	}

	virtual int32_t InitImagePool(const VkVideoProfileInfoKHR* pDecodeProfile, uint32_t numImages, VkFormat dpbImageFormat,
								  VkFormat outImageFormat, const VkExtent2D& codedExtent, const VkExtent2D& maxImageExtent,
								  VkImageUsageFlags dpbImageUsage, VkImageUsageFlags outImageUsage, uint32_t queueFamilyIndex,
								  bool useImageArray = false, bool useImageViewArray = false,
								  bool useSeparateOutputImage = false, bool useLinearOutput = false) {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);

		assert(numImages && (numImages <= maxFramebufferImages) && pDecodeProfile);

		if (m_supportsQueries)
			VK_CHECK(CreateVideoQueries(numImages, m_vkDevCtx, pDecodeProfile));

		// m_extent is for the codedExtent, not the max image resolution
		m_codedExtent = codedExtent;

		int32_t imageSetCreateResult = m_perFrameDecodeImageSet.init(
				m_vkDevCtx, pDecodeProfile, numImages, dpbImageFormat, outImageFormat, maxImageExtent, dpbImageUsage, outImageUsage,
				queueFamilyIndex, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				useLinearOutput
				? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
				: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				useImageArray, useImageViewArray, useSeparateOutputImage, useLinearOutput);
		m_numberParameterUpdates++;

		return imageSetCreateResult;
	}

	void Deinitialize() {
		FlushDisplayQueue();

		if (m_supportsQueries)
			DestroyVideoQueries();

		m_ownedByDisplayMask = 0;
		m_frameNumInDecodeOrder = 0;
		m_frameNumInDisplayOrder = 0;

		m_perFrameDecodeImageSet.Deinit();

		if (m_queryPool != VkQueryPool()) {
			m_vkDevCtx.getDeviceDriver().destroyQueryPool(m_vkDevCtx.device, m_queryPool, NULL);
			m_queryPool = VkQueryPool();
		}
	};

	virtual int32_t QueueDecodedPictureForDisplay(int8_t picId, VulkanVideoDisplayPictureInfo* pDispInfo) {
		assert((uint32_t)picId < m_perFrameDecodeImageSet.size());

		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		m_perFrameDecodeImageSet[picId].m_displayOrder = m_frameNumInDisplayOrder++;
		m_perFrameDecodeImageSet[picId].m_timestamp = pDispInfo->timestamp;
		m_perFrameDecodeImageSet[picId].m_inDisplayQueue = true;
		m_perFrameDecodeImageSet[picId].AddRef();

		m_displayFrames.push((uint8_t)picId);

		if (videoLoggingEnabled()) {
			std::cout << "==> Queue Display Picture picIdx: " << (uint32_t)picId
					  << "\t\tdisplayOrder: " << m_perFrameDecodeImageSet[picId].m_displayOrder
					  << "\tdecodeOrder: " << m_perFrameDecodeImageSet[picId].m_decodeOrder << "\ttimestamp "
					  << m_perFrameDecodeImageSet[picId].m_timestamp << std::endl;
		}
		return picId;
	}

	virtual int32_t QueuePictureForDecode(int8_t picId, VkParserDecodePictureInfo* pDecodePictureInfo,
										  ReferencedObjectsInfo* pReferencedObjectsInfo,
										  FrameSynchronizationInfo* pFrameSynchronizationInfo) {
		assert((uint32_t)picId < m_perFrameDecodeImageSet.size());

		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		m_perFrameDecodeImageSet[picId].m_picDispInfo = *pDecodePictureInfo;
		m_perFrameDecodeImageSet[picId].m_decodeOrder = m_frameNumInDecodeOrder++;
		m_perFrameDecodeImageSet[picId].m_inDecodeQueue = true;
		m_perFrameDecodeImageSet[picId].stdPps = const_cast<VkVideoRefCountBase*>(pReferencedObjectsInfo->pStdPps);
		m_perFrameDecodeImageSet[picId].stdSps = const_cast<VkVideoRefCountBase*>(pReferencedObjectsInfo->pStdSps);
		m_perFrameDecodeImageSet[picId].stdVps = const_cast<VkVideoRefCountBase*>(pReferencedObjectsInfo->pStdVps);
		m_perFrameDecodeImageSet[picId].bitstreamData = const_cast<VkVideoRefCountBase*>(pReferencedObjectsInfo->pBitstreamData);

		if (videoLoggingEnabled()) {
			std::cout << std::dec << "==> Queue Decode Picture picIdx: " << (uint32_t)picId
					  << "\t\tdisplayOrder: " << m_perFrameDecodeImageSet[picId].m_displayOrder
					  << "\tdecodeOrder: " << m_perFrameDecodeImageSet[picId].m_decodeOrder << "\tFrameType "
					  << m_perFrameDecodeImageSet[picId].m_picDispInfo.videoFrameType << std::endl;
		}

		if (pFrameSynchronizationInfo->hasFrameCompleteSignalFence) {
			pFrameSynchronizationInfo->frameCompleteFence = m_perFrameDecodeImageSet[picId].m_frameCompleteFence;
			if (!!pFrameSynchronizationInfo->frameCompleteFence) {
				m_perFrameDecodeImageSet[picId].m_hasFrameCompleteSignalFence = true;
			}
		}

		if (m_perFrameDecodeImageSet[picId].m_hasConsummerSignalFence) {
			pFrameSynchronizationInfo->frameConsumerDoneFence = m_perFrameDecodeImageSet[picId].m_frameConsumerDoneFence;
			m_perFrameDecodeImageSet[picId].m_hasConsummerSignalFence = false;
		}

		if (pFrameSynchronizationInfo->hasFrameCompleteSignalSemaphore) {
			pFrameSynchronizationInfo->frameCompleteSemaphore = m_perFrameDecodeImageSet[picId].m_frameCompleteSemaphore;
			if (!!pFrameSynchronizationInfo->frameCompleteSemaphore) {
				m_perFrameDecodeImageSet[picId].m_hasFrameCompleteSignalSemaphore = true;
			}
		}

		if (m_perFrameDecodeImageSet[picId].m_hasConsummerSignalSemaphore) {
			pFrameSynchronizationInfo->frameConsumerDoneSemaphore = m_perFrameDecodeImageSet[picId].m_frameConsumerDoneSemaphore;
			m_perFrameDecodeImageSet[picId].m_hasConsummerSignalSemaphore = false;
		}

		pFrameSynchronizationInfo->queryPool = m_queryPool;
		pFrameSynchronizationInfo->startQueryId = picId;
		pFrameSynchronizationInfo->numQueries = 1;

		return picId;
	}

	virtual size_t GetDisplayedFrameCount() const { return m_displayFrames.size(); }

	// dequeue
	virtual int32_t DequeueDecodedPicture(DecodedFrame* pDecodedFrame) {
		int numberofPendingFrames = 0;
		int pictureIndex = -1;
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		if (!m_displayFrames.empty()) {
			numberofPendingFrames = (int)m_displayFrames.size();
			pictureIndex = m_displayFrames.front();
			assert((pictureIndex >= 0) && ((uint32_t)pictureIndex < m_perFrameDecodeImageSet.size()));
			assert(!(m_ownedByDisplayMask & (1 << pictureIndex)));
			m_ownedByDisplayMask |= (1 << pictureIndex);
			m_displayFrames.pop();
			m_perFrameDecodeImageSet[pictureIndex].m_inDisplayQueue = false;
			m_perFrameDecodeImageSet[pictureIndex].m_ownedByDisplay = true;
		}

		if ((uint32_t)pictureIndex < m_perFrameDecodeImageSet.size()) {
			pDecodedFrame->pictureIndex = pictureIndex;

			pDecodedFrame->decodedImageView = m_perFrameDecodeImageSet[pictureIndex].GetFrameImageView();
			pDecodedFrame->outputImageView = m_perFrameDecodeImageSet[pictureIndex].GetDisplayImageView();

			pDecodedFrame->displayWidth = m_perFrameDecodeImageSet[pictureIndex].m_picDispInfo.displayWidth;
			pDecodedFrame->displayHeight = m_perFrameDecodeImageSet[pictureIndex].m_picDispInfo.displayHeight;

			if (m_perFrameDecodeImageSet[pictureIndex].m_hasFrameCompleteSignalFence) {
				pDecodedFrame->frameCompleteFence = m_perFrameDecodeImageSet[pictureIndex].m_frameCompleteFence;
				m_perFrameDecodeImageSet[pictureIndex].m_hasFrameCompleteSignalFence = false;
			} else {
				pDecodedFrame->frameCompleteFence = VkFence();
			}

			if (m_perFrameDecodeImageSet[pictureIndex].m_hasFrameCompleteSignalSemaphore) {
				pDecodedFrame->frameCompleteSemaphore = m_perFrameDecodeImageSet[pictureIndex].m_frameCompleteSemaphore;
				m_perFrameDecodeImageSet[pictureIndex].m_hasFrameCompleteSignalSemaphore = false;
			} else {
				pDecodedFrame->frameCompleteSemaphore = VkSemaphore();
			}

			pDecodedFrame->frameConsumerDoneFence = m_perFrameDecodeImageSet[pictureIndex].m_frameConsumerDoneFence;
			pDecodedFrame->frameConsumerDoneSemaphore = m_perFrameDecodeImageSet[pictureIndex].m_frameConsumerDoneSemaphore;

			pDecodedFrame->timestamp = m_perFrameDecodeImageSet[pictureIndex].m_timestamp;
			pDecodedFrame->decodeOrder = m_perFrameDecodeImageSet[pictureIndex].m_decodeOrder;
			pDecodedFrame->displayOrder = m_perFrameDecodeImageSet[pictureIndex].m_displayOrder;

			pDecodedFrame->queryPool = m_queryPool;
			pDecodedFrame->startQueryId = pictureIndex;
			pDecodedFrame->numQueries = 1;
		}

		if (videoLoggingEnabled()) {
			std::cout << "<<<<<<<<<<< Dequeue from Display: " << pictureIndex << " out of " << numberofPendingFrames
					  << " ===========" << std::endl;
		}
		return numberofPendingFrames;
	}

	virtual int32_t ReleaseDisplayedPicture(DecodedFrameRelease** pDecodedFramesRelease, uint32_t numFramesToRelease) {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		for (uint32_t i = 0; i < numFramesToRelease; i++) {
			const DecodedFrameRelease* pDecodedFrameRelease = pDecodedFramesRelease[i];
			int picId = pDecodedFrameRelease->pictureIndex;
			assert((picId >= 0) && ((uint32_t)picId < m_perFrameDecodeImageSet.size()));

			assert(m_perFrameDecodeImageSet[picId].m_decodeOrder == pDecodedFrameRelease->decodeOrder);
			assert(m_perFrameDecodeImageSet[picId].m_displayOrder == pDecodedFrameRelease->displayOrder);

			assert(m_ownedByDisplayMask & (1 << picId));
			m_ownedByDisplayMask &= ~(1 << picId);
			m_perFrameDecodeImageSet[picId].m_inDecodeQueue = false;
			m_perFrameDecodeImageSet[picId].bitstreamData = nullptr;
			m_perFrameDecodeImageSet[picId].stdPps = nullptr;
			m_perFrameDecodeImageSet[picId].stdSps = nullptr;
			m_perFrameDecodeImageSet[picId].stdVps = nullptr;
			m_perFrameDecodeImageSet[picId].m_ownedByDisplay = false;
			m_perFrameDecodeImageSet[picId].Release();

			m_perFrameDecodeImageSet[picId].m_hasConsummerSignalFence = pDecodedFrameRelease->hasConsummerSignalFence;
			m_perFrameDecodeImageSet[picId].m_hasConsummerSignalSemaphore = pDecodedFrameRelease->hasConsummerSignalSemaphore;
		}
		return 0;
	}

	virtual int32_t GetDpbImageResourcesByIndex(uint32_t numResources, const int8_t* referenceSlotIndexes,
												VkVideoPictureResourceInfoKHR* dpbPictureResources,
												PictureResourceInfo* dpbPictureResourcesInfo,
												VkImageLayout newDpbImageLayerLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR) {
		assert(dpbPictureResources);
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		for (unsigned int resId = 0; resId < numResources; resId++) {
			if ((uint32_t)referenceSlotIndexes[resId] < m_perFrameDecodeImageSet.size()) {
				VkResult result =
						m_perFrameDecodeImageSet.GetImageSetNewLayout(m_vkDevCtx, referenceSlotIndexes[resId], newDpbImageLayerLayout,
																	  &dpbPictureResources[resId], &dpbPictureResourcesInfo[resId]);

				assert(result == VK_SUCCESS);
				if (result != VK_SUCCESS) {
					return -1;
				}

				assert(dpbPictureResources[resId].sType == VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR);
				dpbPictureResources[resId].codedOffset = {
						0, 0};  // FIXME: This parameter must to be adjusted based on the interlaced mode.
				dpbPictureResources[resId].codedExtent = m_codedExtent;
			}
		}
		return numResources;
	}

	virtual int32_t GetCurrentImageResourceByIndex(int8_t referenceSlotIndex, VkVideoPictureResourceInfoKHR* dpbPictureResource,
												   PictureResourceInfo* dpbPictureResourceInfo,
												   VkImageLayout newDpbImageLayerLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
												   VkVideoPictureResourceInfoKHR* outputPictureResource = nullptr,
												   PictureResourceInfo* outputPictureResourceInfo = nullptr,
												   VkImageLayout newOutputImageLayerLayout = VK_IMAGE_LAYOUT_MAX_ENUM) {
		assert(dpbPictureResource);
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		if ((uint32_t)referenceSlotIndex < m_perFrameDecodeImageSet.size()) {
			VkResult result = m_perFrameDecodeImageSet.GetImageSetNewLayout(
					m_vkDevCtx, referenceSlotIndex, newDpbImageLayerLayout, dpbPictureResource, dpbPictureResourceInfo,
					newOutputImageLayerLayout, outputPictureResource, outputPictureResourceInfo);
			assert(result == VK_SUCCESS);
			if (result != VK_SUCCESS) {
				return -1;
			}

			assert(dpbPictureResource->sType == VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR);
			dpbPictureResource->codedOffset = {0, 0};  // FIXME: This parameter must to be adjusted based on the interlaced mode.
			dpbPictureResource->codedExtent = m_codedExtent;

			if (outputPictureResource) {
				assert(outputPictureResource->sType == VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR);
				outputPictureResource->codedOffset = {
						0, 0};  // FIXME: This parameter must to be adjusted based on the interlaced mode.
				outputPictureResource->codedExtent = m_codedExtent;
			}
		}
		return referenceSlotIndex;
	}

	virtual int32_t ReleaseImageResources(uint32_t numResources, const uint32_t* indexes) {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		for (unsigned int resId = 0; resId < numResources; resId++) {
			if ((uint32_t)indexes[resId] < m_perFrameDecodeImageSet.size()) {
				m_perFrameDecodeImageSet[indexes[resId]].Deinit();
			}
		}
		return (int32_t)m_perFrameDecodeImageSet.size();
	}

	virtual int32_t SetPicNumInDecodeOrder(int32_t picId, int32_t picNumInDecodeOrder) {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		if ((uint32_t)picId < m_perFrameDecodeImageSet.size()) {
			int32_t oldPicNumInDecodeOrder = m_perFrameDecodeImageSet[picId].m_decodeOrder;
			m_perFrameDecodeImageSet[picId].m_decodeOrder = picNumInDecodeOrder;
			return oldPicNumInDecodeOrder;
		}
		assert(false);
		return -1;
	}

	virtual int32_t SetPicNumInDisplayOrder(int32_t picId, int32_t picNumInDisplayOrder) {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		if ((uint32_t)picId < m_perFrameDecodeImageSet.size()) {
			int32_t oldPicNumInDisplayOrder = m_perFrameDecodeImageSet[picId].m_displayOrder;
			m_perFrameDecodeImageSet[picId].m_displayOrder = picNumInDisplayOrder;
			return oldPicNumInDisplayOrder;
		}
		assert(false);
		return -1;
	}

	virtual const VkSharedBaseObj<VkImageResourceView>& GetImageResourceByIndex(int8_t picId) {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		if ((uint32_t)picId < m_perFrameDecodeImageSet.size()) {
			return m_perFrameDecodeImageSet[picId].GetFrameImageView();
		}
		assert(false);
		return emptyImageView;
	}

	virtual vkPicBuffBase* ReservePictureBuffer() {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		int32_t foundPicId = -1;
		for (uint32_t picId = 0; picId < m_perFrameDecodeImageSet.size(); picId++) {
			if (m_perFrameDecodeImageSet[picId].IsAvailable()) {
				foundPicId = picId;
				break;
			}
		}

		if (foundPicId >= 0) {
			m_perFrameDecodeImageSet[foundPicId].Reset();
			m_perFrameDecodeImageSet[foundPicId].AddRef();
			m_perFrameDecodeImageSet[foundPicId].m_picIdx = foundPicId;
			return &m_perFrameDecodeImageSet[foundPicId];
		}

		assert(foundPicId >= 0);
		return NULL;
	}

	virtual size_t GetSize() {
		std::lock_guard<std::mutex> lock(m_displayQueueMutex);
		return m_perFrameDecodeImageSet.size();
	}

	virtual ~VkVideoFrameBuffer() { Deinitialize(); }

private:
	DeviceContext& m_vkDevCtx;
	std::atomic<int32_t> m_refCount;
	std::mutex m_displayQueueMutex;
	NvPerFrameDecodeImageSet m_perFrameDecodeImageSet;
	std::queue<uint8_t> m_displayFrames;
	bool m_supportsQueries;
	VkQueryPool m_queryPool;
	uint32_t m_ownedByDisplayMask;
	int32_t m_frameNumInDecodeOrder;
	int32_t m_frameNumInDisplayOrder;
	VkExtent2D m_codedExtent;  // for the codedExtent, not the max image resolution
	uint32_t m_numberParameterUpdates;
};

VkResult VulkanVideoFrameBuffer::Create(DeviceContext* vkDevCtx,
										bool supportsQueries,
										VkSharedBaseObj<VulkanVideoFrameBuffer>& vkVideoFrameBuffer)
{
	VkSharedBaseObj<VkVideoFrameBuffer> videoFrameBuffer(new VkVideoFrameBuffer(*vkDevCtx, supportsQueries));
	if (videoFrameBuffer) {
		vkVideoFrameBuffer = videoFrameBuffer;
		return VK_SUCCESS;
	}
	return VK_ERROR_OUT_OF_HOST_MEMORY;
}

int32_t VkVideoFrameBuffer::AddRef()
{
	return ++m_refCount;
}

int32_t VkVideoFrameBuffer::Release()
{
	uint32_t ret;
	ret = --m_refCount;
	// Destroy the device if refcount reaches zero
	if (ret == 0) {
		delete this;
	}
	return ret;
}

VkResult VkImageResource::Create(DeviceContext& vkDevCtx,
								 const VkImageCreateInfo* pImageCreateInfo,
								 VkMemoryPropertyFlags memoryPropertyFlags,
								 VkSharedBaseObj<VkImageResource>& imageResource)
{
	VkResult result = VK_ERROR_INITIALIZATION_FAILED;

	auto& vk = vkDevCtx.getDeviceDriver();
	VkDevice device = vkDevCtx.device;
	VkImage image = VK_NULL_HANDLE;

	do {

		result = vk.createImage(device, pImageCreateInfo, nullptr, &image);
		if (result != VK_SUCCESS) {
			assert(!"CreateImage Failed!");
			break;
		}

		VkMemoryRequirements memoryRequirements = { };
		vk.getImageMemoryRequirements(device, image, &memoryRequirements);

		// Allocate memory for the image
		VkSharedBaseObj<VulkanDeviceMemoryImpl> vkDeviceMemory;
		result = VulkanDeviceMemoryImpl::Create(vkDevCtx.context->getInstanceInterface(),
												vkDevCtx.context->getDeviceInterface(),
												vkDevCtx.device,
												vkDevCtx.phys,
												memoryRequirements,
												memoryPropertyFlags,
												nullptr, // pInitializeMemory
												0ULL,     // initializeMemorySize
												false,   // clearMemory
												vkDeviceMemory);
		if (result != VK_SUCCESS) {
			assert(!"Create Memory Failed!");
			break;
		}

		VkDeviceSize imageOffset = 0;
		result = vk.bindImageMemory(device, image, *vkDeviceMemory, imageOffset);
		if (result != VK_SUCCESS) {
			assert(!"BindImageMemory Failed!");
			break;
		}

		imageResource = new VkImageResource(vkDevCtx,
											pImageCreateInfo,
											image,
											imageOffset,
											memoryRequirements.size,
											vkDeviceMemory);
		if (imageResource == nullptr) {
			break;
		}
		return result;

	} while (0);

	if (device != VK_NULL_HANDLE) {

		if (image != VK_NULL_HANDLE) {
			vk.destroyImage(device, image, nullptr);
		}
	}

	return result;
}


void VkImageResource::Destroy()
{
	auto& vk = m_vkDevCtx.getDeviceDriver();
	auto device = m_vkDevCtx.device;

	if (m_image != VK_NULL_HANDLE) {
		vk.destroyImage(device, m_image, nullptr);
		m_image = VK_NULL_HANDLE;
	}

	m_vulkanDeviceMemory = nullptr;
}

VkResult VkImageResourceView::Create(DeviceContext& vkDevCtx,
									 VkSharedBaseObj<VkImageResource>& imageResource,
									 VkImageSubresourceRange &imageSubresourceRange,
									 VkSharedBaseObj<VkImageResourceView>& imageResourceView)
{
	auto& vk = vkDevCtx.getDeviceDriver();
	VkDevice device = vkDevCtx.device;
	VkImageView  imageView;
	VkImageViewCreateInfo viewInfo = VkImageViewCreateInfo();
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.pNext = nullptr;
	viewInfo.image = imageResource->GetImage();
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = imageResource->GetImageCreateInfo().format;
	viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
							VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
	viewInfo.subresourceRange = imageSubresourceRange;
	viewInfo.flags = 0;
	VkResult result = vk.createImageView(device, &viewInfo, nullptr, &imageView);
	if (result != VK_SUCCESS) {
		return result;
	}

	imageResourceView = new VkImageResourceView(vkDevCtx, imageResource,
												imageView, imageSubresourceRange);

	return result;
}

VkImageResourceView::~VkImageResourceView()
{
	auto& vk = m_vkDevCtx.getDeviceDriver();
	auto device = m_vkDevCtx.device;

	if (m_imageView != VK_NULL_HANDLE) {
		vk.destroyImageView(device, m_imageView, nullptr);
		m_imageView = VK_NULL_HANDLE;
	}

	m_imageResource = nullptr;
}

VkResult NvPerFrameDecodeResources::CreateImage( DeviceContext& vkDevCtx,
												 const VkImageCreateInfo* pDpbImageCreateInfo,
												 const VkImageCreateInfo* pOutImageCreateInfo,
												 VkMemoryPropertyFlags    dpbRequiredMemProps,
												 VkMemoryPropertyFlags    outRequiredMemProps,
												 uint32_t imageIndex,
												 VkSharedBaseObj<VkImageResource>& imageArrayParent,
												 VkSharedBaseObj<VkImageResourceView>& imageViewArrayParent,
												 bool useSeparateOutputImage,
												 bool useLinearOutput)
{
	VkResult result = VK_SUCCESS;

	if (!ImageExist() || m_recreateImage) {

		assert(m_vkDevCtx != nullptr);

		m_currentDpbImageLayerLayout = pDpbImageCreateInfo->initialLayout;
		m_currentOutputImageLayout   = pOutImageCreateInfo->initialLayout;

		VkSharedBaseObj<VkImageResource> imageResource;
		if (!imageArrayParent) {
			result = VkImageResource::Create(vkDevCtx,
											 pDpbImageCreateInfo,
											 dpbRequiredMemProps,
											 imageResource);
			if (result != VK_SUCCESS) {
				return result;
			}
		} else {
			// We are using a parent array image
			imageResource = imageArrayParent;
		}

		if (!imageViewArrayParent) {

			uint32_t baseArrayLayer = imageArrayParent ? imageIndex : 0;
			VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, baseArrayLayer, 1 };
			result = VkImageResourceView::Create(vkDevCtx, imageResource,
												 subresourceRange,
												 m_frameDpbImageView);

			if (result != VK_SUCCESS) {
				return result;
			}

			if (!(useSeparateOutputImage || useLinearOutput)) {
				m_outImageView = m_frameDpbImageView;
			}

		} else {

			m_frameDpbImageView = imageViewArrayParent;

			if (!(useSeparateOutputImage || useLinearOutput)) {
				VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, imageIndex, 1 };
				result = VkImageResourceView::Create(vkDevCtx, imageResource,
													 subresourceRange,
													 m_outImageView);
				if (result != VK_SUCCESS) {
					return result;
				}
			}
		}

		if (useSeparateOutputImage || useLinearOutput) {

			VkSharedBaseObj<VkImageResource> displayImageResource;
			result = VkImageResource::Create(vkDevCtx,
											 pOutImageCreateInfo,
											 outRequiredMemProps,
											 displayImageResource);
			if (result != VK_SUCCESS) {
				return result;
			}

			VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			result = VkImageResourceView::Create(vkDevCtx, displayImageResource,
												 subresourceRange,
												 m_outImageView);
			if (result != VK_SUCCESS) {
				return result;
			}
		}
	}

	m_currentDpbImageLayerLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_currentOutputImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_recreateImage = false;

	return result;
}

VkResult NvPerFrameDecodeResources::init(DeviceContext& vkDevCtx)
{
	m_vkDevCtx = &vkDevCtx;
	auto& vk = vkDevCtx.getDeviceDriver();
	auto device = vkDevCtx.device;

	// The fence waited on for the first frame should be signaled.
	const VkFenceCreateInfo fenceFrameCompleteInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
													   VK_FENCE_CREATE_SIGNALED_BIT };
	VkResult result = vk.createFence(device, &fenceFrameCompleteInfo, nullptr, &m_frameCompleteFence);

	const VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	result = vk.createFence(device, &fenceInfo, nullptr, &m_frameConsumerDoneFence);
	assert(result == VK_SUCCESS);

	const VkSemaphoreCreateInfo semInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	result = vk.createSemaphore(device, &semInfo, nullptr, &m_frameCompleteSemaphore);
	assert(result == VK_SUCCESS);
	result = vk.createSemaphore(device, &semInfo, nullptr, &m_frameConsumerDoneSemaphore);
	assert(result == VK_SUCCESS);

	Reset();

	return result;
}

void NvPerFrameDecodeResources::Deinit()
{
	bitstreamData = nullptr;
	stdPps = nullptr;
	stdSps = nullptr;
	stdVps = nullptr;

	if (m_vkDevCtx == nullptr) {
		assert ((m_frameCompleteFence == VK_NULL_HANDLE) &&
				(m_frameConsumerDoneFence == VK_NULL_HANDLE) &&
				(m_frameCompleteSemaphore == VK_NULL_HANDLE) &&
				(m_frameConsumerDoneSemaphore == VK_NULL_HANDLE) &&
				!m_frameDpbImageView &&
				!m_outImageView);
		return;
	}

	assert(m_vkDevCtx);
	auto& vk = m_vkDevCtx->getDeviceDriver();
	auto device = m_vkDevCtx->device;

	if (m_frameCompleteFence != VkFence()) {
		vk.destroyFence(device, m_frameCompleteFence, nullptr);
		m_frameCompleteFence = VkFence();
	}

	if (m_frameConsumerDoneFence != VkFence()) {
		vk.destroyFence(device, m_frameConsumerDoneFence, nullptr);
		m_frameConsumerDoneFence = VkFence();
	}

	if (m_frameCompleteSemaphore != VkSemaphore()) {
		vk.destroySemaphore(device, m_frameCompleteSemaphore, nullptr);
		m_frameCompleteSemaphore = VkSemaphore();
	}

	if (m_frameConsumerDoneSemaphore != VkSemaphore()) {
		vk.destroySemaphore(device, m_frameConsumerDoneSemaphore, nullptr);
		m_frameConsumerDoneSemaphore = VkSemaphore();
	}

	m_frameDpbImageView = nullptr;
	m_outImageView = nullptr;

	m_vkDevCtx = nullptr;

	Reset();
}

int32_t NvPerFrameDecodeImageSet::init(DeviceContext& vkDevCtx,
									   const VkVideoProfileInfoKHR* pDecodeProfile,
									   uint32_t                 numImages,
									   VkFormat                 dpbImageFormat,
									   VkFormat                 outImageFormat,
									   const VkExtent2D&        maxImageExtent,
									   VkImageUsageFlags        dpbImageUsage,
									   VkImageUsageFlags        outImageUsage,
									   uint32_t                 queueFamilyIndex,
									   VkMemoryPropertyFlags    dpbRequiredMemProps,
									   VkMemoryPropertyFlags    outRequiredMemProps,
									   bool                     useImageArray,
									   bool                     useImageViewArray,
									   bool                     useSeparateOutputImage,
									   bool                     useLinearOutput)
{
	if (numImages > m_perFrameDecodeResources.size()) {
		assert(!"Number of requested images exceeds the max size of the image array");
		return -1;
	}

	const bool reconfigureImages = (m_numImages &&
									(m_dpbImageCreateInfo.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO)) &&
								   ((m_dpbImageCreateInfo.format != dpbImageFormat) ||
									(m_dpbImageCreateInfo.extent.width < maxImageExtent.width) ||
									(m_dpbImageCreateInfo.extent.height < maxImageExtent.height));

	for (uint32_t imageIndex = m_numImages; imageIndex < numImages; imageIndex++) {
		VkResult result = m_perFrameDecodeResources[imageIndex].init(vkDevCtx);
		assert(result == VK_SUCCESS);
		if (result != VK_SUCCESS) {
			return -1;
		}
	}

	if (useImageViewArray) {
		useImageArray = true;
	}

	m_videoProfile.InitFromProfile(pDecodeProfile);

	m_queueFamilyIndex = queueFamilyIndex;
	m_dpbRequiredMemProps = dpbRequiredMemProps;
	m_outRequiredMemProps = outRequiredMemProps;

	// Image create info for the DPBs
	m_dpbImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	// m_imageCreateInfo.pNext = m_videoProfile.GetProfile();
	m_dpbImageCreateInfo.pNext = m_videoProfile.GetProfileListInfo();
	m_dpbImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	m_dpbImageCreateInfo.format = dpbImageFormat;
	m_dpbImageCreateInfo.extent = { maxImageExtent.width, maxImageExtent.height, 1 };
	m_dpbImageCreateInfo.mipLevels = 1;
	m_dpbImageCreateInfo.arrayLayers = useImageArray ? numImages : 1;
	m_dpbImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	m_dpbImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	m_dpbImageCreateInfo.usage = dpbImageUsage;
	m_dpbImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	m_dpbImageCreateInfo.queueFamilyIndexCount = 1;
	m_dpbImageCreateInfo.pQueueFamilyIndices = &m_queueFamilyIndex;
	m_dpbImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_dpbImageCreateInfo.flags = 0;

	// Image create info for the output
	if (useSeparateOutputImage || useLinearOutput) {
		m_outImageCreateInfo = m_dpbImageCreateInfo;
		m_outImageCreateInfo.format = outImageFormat;
		m_outImageCreateInfo.arrayLayers = 1;
		m_outImageCreateInfo.tiling = useLinearOutput ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
		m_outImageCreateInfo.usage = outImageUsage;

		if ((outImageUsage & VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR) == 0) {
			// A simple output image not directly used by the decoder
			m_outImageCreateInfo.pNext = nullptr;
		}
	}

	if (useImageArray) {
		// Create an image that has the same number of layers as the DPB images required.
		VkResult result = VkImageResource::Create(vkDevCtx,
												  &m_dpbImageCreateInfo,
												  m_dpbRequiredMemProps,
												  m_imageArray);
		if (result != VK_SUCCESS) {
			return -1;
		}
	} else {
		m_imageArray = nullptr;
	}

	if (useImageViewArray) {
		assert(m_imageArray);
		// Create an image view that has the same number of layers as the image.
		// In that scenario, while specifying the resource, the API must specifically choose the image layer.
		VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, numImages };
		VkResult result = VkImageResourceView::Create(vkDevCtx, m_imageArray,
													  subresourceRange,
													  m_imageViewArray);

		if (result != VK_SUCCESS) {
			return -1;
		}
	}

	uint32_t firstIndex = reconfigureImages ? 0 : m_numImages;
	uint32_t maxNumImages = std::max(m_numImages, numImages);
	for (uint32_t imageIndex = firstIndex; imageIndex < maxNumImages; imageIndex++) {

		if (m_perFrameDecodeResources[imageIndex].ImageExist() && reconfigureImages) {

			m_perFrameDecodeResources[imageIndex].m_recreateImage = true;

		} else if (!m_perFrameDecodeResources[imageIndex].ImageExist()) {

			VkResult result =
					m_perFrameDecodeResources[imageIndex].CreateImage(vkDevCtx,
																	  &m_dpbImageCreateInfo,
																	  &m_outImageCreateInfo,
																	  m_dpbRequiredMemProps,
																	  m_outRequiredMemProps,
																	  imageIndex,
																	  m_imageArray,
																	  m_imageViewArray,
																	  useSeparateOutputImage,
																	  useLinearOutput);

			assert(result == VK_SUCCESS);
			if (result != VK_SUCCESS) {
				return -1;
			}
		}
	}

	m_numImages               = numImages;
	m_usesImageArray          = useImageArray;
	m_usesImageViewArray      = useImageViewArray;
	m_usesSeparateOutputImage = useSeparateOutputImage;
	m_usesLinearOutput        = useLinearOutput;

	return (int32_t)numImages;
}

void NvPerFrameDecodeImageSet::Deinit()
{
	for (size_t ndx = 0; ndx < m_numImages; ndx++) {
		m_perFrameDecodeResources[ndx].Deinit();
	}

	m_numImages = 0;
}

const char* VkParserVideoPictureParameters::m_refClassId = "VkParserVideoPictureParameters";
int32_t VkParserVideoPictureParameters::m_currentId = 0;

int32_t VkParserVideoPictureParameters::PopulateH264UpdateFields(const StdVideoPictureParametersSet* pStdPictureParametersSet,
																 VkVideoDecodeH264SessionParametersAddInfoKHR& h264SessionParametersAddInfo)
{
	int32_t currentId = -1;
	if (pStdPictureParametersSet == nullptr) {
		return currentId;
	}

	assert( (pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H264_SPS) ||
			(pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H264_PPS));

	assert(h264SessionParametersAddInfo.sType == VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR);

	if (pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H264_SPS) {
		h264SessionParametersAddInfo.stdSPSCount = 1;
		h264SessionParametersAddInfo.pStdSPSs = pStdPictureParametersSet->GetStdH264Sps();
		bool isSps = false;
		currentId = pStdPictureParametersSet->GetSpsId(isSps);
		assert(isSps);
	} else if (pStdPictureParametersSet->GetStdType() ==  StdVideoPictureParametersSet::TYPE_H264_PPS ) {
		h264SessionParametersAddInfo.stdPPSCount = 1;
		h264SessionParametersAddInfo.pStdPPSs = pStdPictureParametersSet->GetStdH264Pps();
		bool isPps = false;
		currentId = pStdPictureParametersSet->GetPpsId(isPps);
		assert(isPps);
	} else {
		assert(!"Incorrect h.264 type");
	}

	return currentId;
}

int32_t VkParserVideoPictureParameters::PopulateH265UpdateFields(const StdVideoPictureParametersSet* pStdPictureParametersSet,
																 VkVideoDecodeH265SessionParametersAddInfoKHR& h265SessionParametersAddInfo)
{
	int32_t currentId = -1;
	if (pStdPictureParametersSet == nullptr) {
		return currentId;
	}

	assert( (pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H265_VPS) ||
			(pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H265_SPS) ||
			(pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H265_PPS));

	assert(h265SessionParametersAddInfo.sType == VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR);

	if (pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H265_VPS) {
		h265SessionParametersAddInfo.stdVPSCount = 1;
		h265SessionParametersAddInfo.pStdVPSs = pStdPictureParametersSet->GetStdH265Vps();
		bool isVps = false;
		currentId = pStdPictureParametersSet->GetVpsId(isVps);
		assert(isVps);
	} else if (pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H265_SPS) {
		h265SessionParametersAddInfo.stdSPSCount = 1;
		h265SessionParametersAddInfo.pStdSPSs = pStdPictureParametersSet->GetStdH265Sps();
		bool isSps = false;
		currentId = pStdPictureParametersSet->GetSpsId(isSps);
		assert(isSps);
	} else if (pStdPictureParametersSet->GetStdType() == StdVideoPictureParametersSet::TYPE_H265_PPS) {
		h265SessionParametersAddInfo.stdPPSCount = 1;
		h265SessionParametersAddInfo.pStdPPSs = pStdPictureParametersSet->GetStdH265Pps();
		bool isPps = false;
		currentId = pStdPictureParametersSet->GetPpsId(isPps);
		assert(isPps);
	} else {
		assert(!"Incorrect h.265 type");
	}

	return currentId;
}

VkResult
VkParserVideoPictureParameters::Create(DeviceContext& deviceContext,
									   VkSharedBaseObj<VkParserVideoPictureParameters>& templatePictureParameters,
									   VkSharedBaseObj<VkParserVideoPictureParameters>& videoPictureParameters)
{
	VkSharedBaseObj<VkParserVideoPictureParameters> newVideoPictureParameters(
			new VkParserVideoPictureParameters(deviceContext, templatePictureParameters));
	if (!newVideoPictureParameters) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	videoPictureParameters = newVideoPictureParameters;
	return VK_SUCCESS;
}

VkResult VkParserVideoPictureParameters::CreateParametersObject(VkSharedBaseObj<VulkanVideoSession>& videoSession,
																const StdVideoPictureParametersSet* pStdVideoPictureParametersSet,
																VkParserVideoPictureParameters* pTemplatePictureParameters)
{
	int32_t currentId = -1;

	VkVideoSessionParametersCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR };

	VkVideoDecodeH264SessionParametersCreateInfoKHR h264SessionParametersCreateInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR};
	VkVideoDecodeH264SessionParametersAddInfoKHR h264SessionParametersAddInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR };

	VkVideoDecodeH265SessionParametersCreateInfoKHR h265SessionParametersCreateInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR };
	VkVideoDecodeH265SessionParametersAddInfoKHR h265SessionParametersAddInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR};

	StdVideoPictureParametersSet::StdType updateType = pStdVideoPictureParametersSet->GetStdType();
	switch (updateType)
	{
		case StdVideoPictureParametersSet::TYPE_H264_SPS:
		case StdVideoPictureParametersSet::TYPE_H264_PPS:
		{
			createInfo.pNext =  &h264SessionParametersCreateInfo;
			h264SessionParametersCreateInfo.maxStdSPSCount = MAX_SPS_IDS;
			h264SessionParametersCreateInfo.maxStdPPSCount = MAX_PPS_IDS;
			h264SessionParametersCreateInfo.pParametersAddInfo = &h264SessionParametersAddInfo;

			currentId = PopulateH264UpdateFields(pStdVideoPictureParametersSet, h264SessionParametersAddInfo);

		}
			break;
		case StdVideoPictureParametersSet::TYPE_H265_VPS:
		case StdVideoPictureParametersSet::TYPE_H265_SPS:
		case StdVideoPictureParametersSet::TYPE_H265_PPS:
		{
			createInfo.pNext =  &h265SessionParametersCreateInfo;
			h265SessionParametersCreateInfo.maxStdVPSCount = MAX_VPS_IDS;
			h265SessionParametersCreateInfo.maxStdSPSCount = MAX_SPS_IDS;
			h265SessionParametersCreateInfo.maxStdPPSCount = MAX_PPS_IDS;
			h265SessionParametersCreateInfo.pParametersAddInfo = &h265SessionParametersAddInfo;

			currentId = PopulateH265UpdateFields(pStdVideoPictureParametersSet, h265SessionParametersAddInfo);
		}
			break;
		default:
			assert(!"Invalid Parser format");
			return VK_ERROR_INITIALIZATION_FAILED;
	}

	createInfo.videoSessionParametersTemplate = pTemplatePictureParameters ? VkVideoSessionParametersKHR(*pTemplatePictureParameters) : VkVideoSessionParametersKHR();
	createInfo.videoSession = videoSession->GetVideoSession();
	VkResult result = m_deviceContext.getDeviceDriver().createVideoSessionParametersKHR(m_deviceContext.device,
																&createInfo,
																nullptr,
																&m_sessionParameters);

	if (result != VK_SUCCESS) {

		assert(!"Could not create Session Parameters Object");
		return result;

	} else {

		m_videoSession = videoSession;

		if (pTemplatePictureParameters) {
			m_vpsIdsUsed = pTemplatePictureParameters->m_vpsIdsUsed;
			m_spsIdsUsed = pTemplatePictureParameters->m_spsIdsUsed;
			m_ppsIdsUsed = pTemplatePictureParameters->m_ppsIdsUsed;
		}

		assert (currentId >= 0);
		switch (pStdVideoPictureParametersSet->GetParameterType()) {
			case StdVideoPictureParametersSet::PPS_TYPE:
				m_ppsIdsUsed.set(currentId, true);
				break;

			case StdVideoPictureParametersSet::SPS_TYPE:
				m_spsIdsUsed.set(currentId, true);
				break;

			case StdVideoPictureParametersSet::VPS_TYPE:
				m_vpsIdsUsed.set(currentId, true);
				break;
			default:
				assert(!"Invalid StdVideoPictureParametersSet Parameter Type!");
		}
		m_Id = ++m_currentId;
	}

	return result;
}

VkResult VkParserVideoPictureParameters::UpdateParametersObject(StdVideoPictureParametersSet* pStdVideoPictureParametersSet)
{
	if (pStdVideoPictureParametersSet == nullptr) {
		return VK_SUCCESS;
	}

	int32_t currentId = -1;
	VkVideoSessionParametersUpdateInfoKHR updateInfo = { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_UPDATE_INFO_KHR };
	VkVideoDecodeH264SessionParametersAddInfoKHR h264SessionParametersAddInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR };
	VkVideoDecodeH265SessionParametersAddInfoKHR h265SessionParametersAddInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR};

	StdVideoPictureParametersSet::StdType updateType = pStdVideoPictureParametersSet->GetStdType();
	switch (updateType)
	{
		case StdVideoPictureParametersSet::TYPE_H264_SPS:
		case StdVideoPictureParametersSet::TYPE_H264_PPS:
		{
			updateInfo.pNext = &h264SessionParametersAddInfo;
			currentId = PopulateH264UpdateFields(pStdVideoPictureParametersSet, h264SessionParametersAddInfo);
		}
			break;
		case StdVideoPictureParametersSet::TYPE_H265_VPS:
		case StdVideoPictureParametersSet::TYPE_H265_SPS:
		case StdVideoPictureParametersSet::TYPE_H265_PPS:
		{
			updateInfo.pNext = &h265SessionParametersAddInfo;
			currentId = PopulateH265UpdateFields(pStdVideoPictureParametersSet, h265SessionParametersAddInfo);
		}
			break;
		default:
			assert(!"Invalid Parser format");
			return VK_ERROR_INITIALIZATION_FAILED;
	}

	tcu::print("%p %d\n", pStdVideoPictureParametersSet, m_updateCount);
	updateInfo.updateSequenceCount = ++m_updateCount;
	VK_CHECK(m_deviceContext.getDeviceDriver().updateVideoSessionParametersKHR(m_deviceContext.device,
																			   m_sessionParameters,
																			   &updateInfo));


	DE_ASSERT(currentId >= 0);
	switch (pStdVideoPictureParametersSet->GetParameterType()) {
		case StdVideoPictureParametersSet::PPS_TYPE:
			m_ppsIdsUsed.set(currentId, true);
			break;

		case StdVideoPictureParametersSet::SPS_TYPE:
			m_spsIdsUsed.set(currentId, true);
			break;

		case StdVideoPictureParametersSet::VPS_TYPE:
			m_vpsIdsUsed.set(currentId, true);
			break;
		default:
			assert(!"Invalid StdVideoPictureParametersSet Parameter Type!");
	}

	return VK_SUCCESS;
}

VkParserVideoPictureParameters::~VkParserVideoPictureParameters()
{
	if (!!m_sessionParameters) {
		m_deviceContext.getDeviceDriver().destroyVideoSessionParametersKHR(m_deviceContext.device, m_sessionParameters, nullptr);
		m_sessionParameters = VkVideoSessionParametersKHR();
	}
	m_videoSession = nullptr;
}

bool VkParserVideoPictureParameters::UpdatePictureParametersHierarchy(
		VkSharedBaseObj<StdVideoPictureParametersSet>& pictureParametersObject)
{
	int32_t nodeId = -1;
	bool isNodeId = false;
	StdVideoPictureParametersSet::ParameterType nodeParent = StdVideoPictureParametersSet::INVALID_TYPE;
	StdVideoPictureParametersSet::ParameterType nodeChild = StdVideoPictureParametersSet::INVALID_TYPE;
	switch (pictureParametersObject->GetParameterType()) {
		case StdVideoPictureParametersSet::PPS_TYPE:
			nodeParent = StdVideoPictureParametersSet::SPS_TYPE;
			nodeId = pictureParametersObject->GetPpsId(isNodeId);
			if (!((uint32_t)nodeId < VkParserVideoPictureParameters::MAX_PPS_IDS)) {
				assert(!"PPS ID is out of bounds");
				return false;
			}
			assert(isNodeId);
			if (m_lastPictParamsQueue[nodeParent]) {
				bool isParentId = false;
				const int32_t spsParentId = pictureParametersObject->GetSpsId(isParentId);
				assert(!isParentId);
				if (spsParentId == m_lastPictParamsQueue[nodeParent]->GetSpsId(isParentId)) {
					assert(isParentId);
					pictureParametersObject->m_parent = m_lastPictParamsQueue[nodeParent];
				}
			}
			break;
		case StdVideoPictureParametersSet::SPS_TYPE:
			nodeParent = StdVideoPictureParametersSet::VPS_TYPE;
			nodeChild = StdVideoPictureParametersSet::PPS_TYPE;
			nodeId = pictureParametersObject->GetSpsId(isNodeId);
			if (!((uint32_t)nodeId < VkParserVideoPictureParameters::MAX_SPS_IDS)) {
				assert(!"SPS ID is out of bounds");
				return false;
			}
			assert(isNodeId);
			if (m_lastPictParamsQueue[nodeChild]) {
				const int32_t spsChildId = m_lastPictParamsQueue[nodeChild]->GetSpsId(isNodeId);
				assert(!isNodeId);
				if (spsChildId == nodeId) {
					m_lastPictParamsQueue[nodeChild]->m_parent = pictureParametersObject;
				}
			}
			if (m_lastPictParamsQueue[nodeParent]) {
				const int32_t vpsParentId = pictureParametersObject->GetVpsId(isNodeId);
				assert(!isNodeId);
				if (vpsParentId == m_lastPictParamsQueue[nodeParent]->GetVpsId(isNodeId)) {
					pictureParametersObject->m_parent = m_lastPictParamsQueue[nodeParent];
					assert(isNodeId);
				}
			}
			break;
		case StdVideoPictureParametersSet::VPS_TYPE:
			nodeChild = StdVideoPictureParametersSet::SPS_TYPE;
			nodeId = pictureParametersObject->GetVpsId(isNodeId);
			if (!((uint32_t)nodeId < VkParserVideoPictureParameters::MAX_VPS_IDS)) {
				assert(!"VPS ID is out of bounds");
				return false;
			}
			assert(isNodeId);
			if (m_lastPictParamsQueue[nodeChild]) {
				const int32_t vpsParentId = m_lastPictParamsQueue[nodeChild]->GetVpsId(isNodeId);
				assert(!isNodeId);
				if (vpsParentId == nodeId) {
					m_lastPictParamsQueue[nodeChild]->m_parent = pictureParametersObject;
				}
			}
			break;
		default:
			assert("!Invalid STD type");
			return false;
	}
	m_lastPictParamsQueue[pictureParametersObject->GetParameterType()] = pictureParametersObject;

	return true;
}

VkResult VkParserVideoPictureParameters::AddPictureParametersToQueue(VkSharedBaseObj<StdVideoPictureParametersSet>& pictureParametersSet)
{
	m_pictureParametersQueue.push(pictureParametersSet);
	return VK_SUCCESS;
}

VkResult VkParserVideoPictureParameters::HandleNewPictureParametersSet(VkSharedBaseObj<VulkanVideoSession>& videoSession,
																	   StdVideoPictureParametersSet* pStdVideoPictureParametersSet)
{
	VkResult result;
	if (m_sessionParameters == VK_NULL_HANDLE) {
		assert(videoSession != VK_NULL_HANDLE);
		assert(m_videoSession == VK_NULL_HANDLE);
		if (m_templatePictureParameters) {
			m_templatePictureParameters->FlushPictureParametersQueue(videoSession);
		}
		result = CreateParametersObject(videoSession, pStdVideoPictureParametersSet,
										m_templatePictureParameters);
		assert(result == VK_SUCCESS);
		m_templatePictureParameters = nullptr; // the template object is not needed anymore
		m_videoSession = videoSession;

	} else {

		assert(m_videoSession != VK_NULL_HANDLE);
		assert(m_sessionParameters != VK_NULL_HANDLE);
		result = UpdateParametersObject(pStdVideoPictureParametersSet);
		assert(result == VK_SUCCESS);
	}

	return result;
}


int32_t VkParserVideoPictureParameters::FlushPictureParametersQueue(VkSharedBaseObj<VulkanVideoSession>& videoSession)
{
	if (!videoSession) {
		return -1;
	}
	uint32_t numQueueItems = 0;
	while (!m_pictureParametersQueue.empty()) {
		VkSharedBaseObj<StdVideoPictureParametersSet>& stdVideoPictureParametersSet = m_pictureParametersQueue.front();

		VkResult result =  HandleNewPictureParametersSet(videoSession, stdVideoPictureParametersSet);
		if (result != VK_SUCCESS) {
			return -1;
		}

		m_pictureParametersQueue.pop();
		numQueueItems++;
	}

	return numQueueItems;
}

bool VkParserVideoPictureParameters::CheckStdObjectBeforeUpdate(VkSharedBaseObj<StdVideoPictureParametersSet>& stdPictureParametersSet,
																VkSharedBaseObj<VkParserVideoPictureParameters>& currentVideoPictureParameters)
{
	if (!stdPictureParametersSet) {
		return false;
	}

	bool stdObjectUpdate = (stdPictureParametersSet->GetUpdateSequenceCount() > 0);

	if (!currentVideoPictureParameters || stdObjectUpdate) {

		// Create new Vulkan Picture Parameters object
		return true;

	} else { // existing VkParserVideoPictureParameters object
		assert(currentVideoPictureParameters);
		// Update with the existing Vulkan Picture Parameters object
	}

	VkSharedBaseObj<VkVideoRefCountBase> clientObject;
	stdPictureParametersSet->GetClientObject(clientObject);
	assert(!clientObject);

	return false;
}

VkResult
VkParserVideoPictureParameters::AddPictureParameters(DeviceContext& deviceContext,
													 VkSharedBaseObj<VulkanVideoSession>& videoSession,
													 VkSharedBaseObj<StdVideoPictureParametersSet>& stdPictureParametersSet,
													 VkSharedBaseObj<VkParserVideoPictureParameters>& currentVideoPictureParameters)
{
	if (!stdPictureParametersSet) {
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	if (currentVideoPictureParameters) {
		currentVideoPictureParameters->FlushPictureParametersQueue(videoSession);
	}

	VkResult result;
	if (CheckStdObjectBeforeUpdate(stdPictureParametersSet, currentVideoPictureParameters)) {
		result = VkParserVideoPictureParameters::Create(deviceContext,
														currentVideoPictureParameters,
														currentVideoPictureParameters);
	}

	if (videoSession) {
		result = currentVideoPictureParameters->HandleNewPictureParametersSet(videoSession, stdPictureParametersSet);
	} else {
		result = currentVideoPictureParameters->AddPictureParametersToQueue(stdPictureParametersSet);
	}

	return result;
}


int32_t VkParserVideoPictureParameters::AddRef()
{
	return ++m_refCount;
}

int32_t VkParserVideoPictureParameters::Release()
{
	uint32_t ret;
	ret = --m_refCount;
	// Destroy the device if refcount reaches zero
	if (ret == 0) {
		delete this;
	}
	return ret;
}

MovePtr<vkt::ycbcr::MultiPlaneImageData> getDecodedImage(const DeviceInterface& vkd,
														 VkDevice				device,
														 Allocator&				allocator,
														 VkImage				image,
														 VkImageLayout			layout,
														 VkFormat				format,
														 VkExtent2D				codedExtent,
														 VkSemaphore			frameCompleteSem,
														 deUint32				queueFamilyIndexTransfer,
														 deUint32				queueFamilyIndexDecode)
{
	MovePtr<vkt::ycbcr::MultiPlaneImageData> multiPlaneImageData(new vkt::ycbcr::MultiPlaneImageData(format, tcu::UVec2(codedExtent.width, codedExtent.height)));
	const VkQueue							 queueDecode				   = getDeviceQueue(vkd, device, queueFamilyIndexDecode, 0u);
	const VkQueue							 queueTransfer				   = getDeviceQueue(vkd, device, queueFamilyIndexTransfer, 0u);
	const VkImageSubresourceRange			 imageSubresourceRange		   = makeImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
	const VkImageMemoryBarrier2KHR			 imageBarrierDecode			   = makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
																				 VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
																				 VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR,
																				 VK_ACCESS_NONE_KHR,
																				 layout,
																				 VK_IMAGE_LAYOUT_GENERAL,
																				 image,
																				 imageSubresourceRange);
	const VkImageMemoryBarrier2KHR			 imageBarrierOwnershipDecode   = makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR,
																						 VK_ACCESS_NONE_KHR,
																						 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
																						 VK_ACCESS_NONE_KHR,
																						 VK_IMAGE_LAYOUT_GENERAL,
																						 VK_IMAGE_LAYOUT_GENERAL,
																						 image,
																						 imageSubresourceRange,
																						 queueFamilyIndexDecode,
																						 queueFamilyIndexTransfer);
	const VkImageMemoryBarrier2KHR			 imageBarrierOwnershipTransfer = makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
																							 VK_ACCESS_NONE_KHR,
																							 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
																							 VK_ACCESS_NONE_KHR,
																							 VK_IMAGE_LAYOUT_GENERAL,
																							 VK_IMAGE_LAYOUT_GENERAL,
																							 image,
																							 imageSubresourceRange,
																							 queueFamilyIndexDecode,
																							 queueFamilyIndexTransfer);
	const VkImageMemoryBarrier2KHR			 imageBarrierTransfer		   = makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
																					 VK_ACCESS_2_TRANSFER_READ_BIT_KHR,
																					 VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR,
																					 VK_ACCESS_NONE_KHR,
																					 VK_IMAGE_LAYOUT_GENERAL,
																					 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
																					 image,
																					 imageSubresourceRange);
	const Move<VkCommandPool>				 cmdDecodePool(makeCommandPool(vkd, device, queueFamilyIndexDecode));
	const Move<VkCommandBuffer>				 cmdDecodeBuffer(allocateCommandBuffer(vkd, device, *cmdDecodePool, VK_COMMAND_BUFFER_LEVEL_PRIMARY));
	const Move<VkCommandPool>				 cmdTransferPool(makeCommandPool(vkd, device, queueFamilyIndexTransfer));
	const Move<VkCommandBuffer>				 cmdTransferBuffer(allocateCommandBuffer(vkd, device, *cmdTransferPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY));
	Move<VkSemaphore>						 semaphore		  = createSemaphore(vkd, device);
	Move<VkFence>							 decodeFence	  = createFence(vkd, device);
	Move<VkFence>							 transferFence	  = createFence(vkd, device);
	VkFence									 fences[]		  = {*decodeFence, *transferFence};
	const VkPipelineStageFlags				 waitDstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	VkSubmitInfo							 decodeSubmitInfo{
		VK_STRUCTURE_TYPE_SUBMIT_INFO, // VkStructureType                              sType;
		DE_NULL, // const void*                                  pNext;
		0u, // deUint32                                             waitSemaphoreCount;
		DE_NULL, // const VkSemaphore*                   pWaitSemaphores;
		DE_NULL, // const VkPipelineStageFlags*  pWaitDstStageMask;
		1u, // deUint32                                             commandBufferCount;
		&*cmdDecodeBuffer, // const VkCommandBuffer*               pCommandBuffers;
		1u, // deUint32                                             signalSemaphoreCount;
		&*semaphore, // const VkSemaphore*                   pSignalSemaphores;
	};
	if (frameCompleteSem != VkSemaphore())
	{
		decodeSubmitInfo.waitSemaphoreCount = 1;
		decodeSubmitInfo.pWaitSemaphores	= &frameCompleteSem;
		decodeSubmitInfo.pWaitDstStageMask	= &waitDstStageMask;
	}
	const VkSubmitInfo transferSubmitInfo{
		VK_STRUCTURE_TYPE_SUBMIT_INFO, // VkStructureType                              sType;
		DE_NULL, // const void*                                  pNext;
		1u, // deUint32                                             waitSemaphoreCount;
		&*semaphore, // const VkSemaphore*                   pWaitSemaphores;
		&waitDstStageMask, // const VkPipelineStageFlags*  pWaitDstStageMask;
		1u, // deUint32                                             commandBufferCount;
		&*cmdTransferBuffer, // const VkCommandBuffer*               pCommandBuffers;
		0u, // deUint32                                             signalSemaphoreCount;
		DE_NULL, // const VkSemaphore*                   pSignalSemaphores;
	};

	DEBUGLOG(std::cout << "getDecodedImage: " << image << " " << layout << std::endl);

	beginCommandBuffer(vkd, *cmdDecodeBuffer, 0u);
	cmdPipelineImageMemoryBarrier2(vkd, *cmdDecodeBuffer, &imageBarrierDecode);
	cmdPipelineImageMemoryBarrier2(vkd, *cmdDecodeBuffer, &imageBarrierOwnershipDecode);
	endCommandBuffer(vkd, *cmdDecodeBuffer);

	beginCommandBuffer(vkd, *cmdTransferBuffer, 0u);
	cmdPipelineImageMemoryBarrier2(vkd, *cmdTransferBuffer, &imageBarrierOwnershipTransfer);
	cmdPipelineImageMemoryBarrier2(vkd, *cmdTransferBuffer, &imageBarrierTransfer);
	endCommandBuffer(vkd, *cmdTransferBuffer);

	VK_CHECK(vkd.queueSubmit(queueDecode, 1u, &decodeSubmitInfo, *decodeFence));
	VK_CHECK(vkd.queueSubmit(queueTransfer, 1u, &transferSubmitInfo, *transferFence));

	VK_CHECK(vkd.waitForFences(device, DE_LENGTH_OF_ARRAY(fences), fences, DE_TRUE, ~0ull));

	vkt::ycbcr::downloadImage(vkd, device, queueFamilyIndexTransfer, allocator, image, multiPlaneImageData.get(), 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	const VkImageMemoryBarrier2KHR			 imageBarrierTransfer2		   = makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
																				  VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
																				  VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR,
																				  VK_ACCESS_NONE_KHR,
																				   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
																				   VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
																				  image,
																				  imageSubresourceRange);


	vkd.resetCommandBuffer(*cmdTransferBuffer, 0u);
	vkd.resetFences(device, 1, &*transferFence);
	beginCommandBuffer(vkd, *cmdTransferBuffer, 0u);
	cmdPipelineImageMemoryBarrier2(vkd, *cmdTransferBuffer, &imageBarrierTransfer2);
	endCommandBuffer(vkd, *cmdTransferBuffer);

	const VkSubmitInfo transferSubmitInfo2{
		VK_STRUCTURE_TYPE_SUBMIT_INFO, // VkStructureType                              sType;
		DE_NULL, // const void*                                  pNext;
		0u, // deUint32                                             waitSemaphoreCount;
		DE_NULL, // const VkSemaphore*                   pWaitSemaphores;
		DE_NULL, // const VkPipelineStageFlags*  pWaitDstStageMask;
		1u, // deUint32                                             commandBufferCount;
		&*cmdTransferBuffer, // const VkCommandBuffer*               pCommandBuffers;
		0u, // deUint32                                             signalSemaphoreCount;
		DE_NULL, // const VkSemaphore*                   pSignalSemaphores;
	};

	VK_CHECK(vkd.queueSubmit(queueTransfer, 1u, &transferSubmitInfo2, *transferFence));
	VK_CHECK(vkd.waitForFences(device, 1, &*transferFence, DE_TRUE, ~0ull));

	return multiPlaneImageData;
}

}	// video
}	// vkt
