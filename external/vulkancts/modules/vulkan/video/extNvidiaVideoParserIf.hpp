#ifndef _EXTNVIDIAVIDEOPARSERIF_HPP
#define _EXTNVIDIAVIDEOPARSERIF_HPP
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
 * \brief Interface glue to the NVIDIA Vulkan Video samples.
 *//*--------------------------------------------------------------------*/
/*
 * Copyright 2021 NVIDIA Corporation.
 * Copyright (c) 2021 The Khronos Group Inc.
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

#include <atomic>
#include <cstdint>
#include <cstring>

#include "vkvideo_parser/VulkanVideoParserIf.h"
#include "vkvideo_parser/VulkanVideoParserParams.h"
#include "vkvideo_parser/VulkanVideoParser.h"
#include "NvVideoParser/nvVulkanVideoParser.h"
#include "VkVideoCore/VkVideoRefCountBase.h"
#include "VkCodecUtils/VulkanVideoReferenceCountedPool.h"
#include "vktBistreamBufferImpl.hpp"

#include <iostream>

namespace vkt
{
namespace video
{
#define DEBUGLOG(X)

using namespace vk;
using namespace std;

#define NV_VULKAN_VIDEO_PARSER_API_VERSION_0_9_9 VK_MAKE_VIDEO_STD_VERSION(0, 9, 9)
#define NV_VULKAN_VIDEO_PARSER_API_VERSION   NV_VULKAN_VIDEO_PARSER_API_VERSION_0_9_9

struct VkParserPerFrameDecodeParameters
{
	enum
	{
		MAX_DPB_REF_SLOTS			= 16, // max 16 reference pictures.
		MAX_DPB_REF_AND_SETUP_SLOTS = MAX_DPB_REF_SLOTS + 1, // plus 1 for the current picture (h.264 only)
	};
	int									   currPicIdx; /** Output index of the current picture                       */
	// VPS
	const StdVideoPictureParametersSet*	   pStdVps;
	// SPS
	const StdVideoPictureParametersSet*	   pStdSps;
	// PPS
	const StdVideoPictureParametersSet*	   pStdPps;

	// inlined picture parameters that should be inserted to VkVideoBeginCodingInfo
	const void*							   beginCodingInfoPictureParametersExt;
	uint32_t							   useInlinedPictureParameters : 1;
	// Bitstream data
	uint32_t							   firstSliceIndex;
	uint32_t							   numSlices;
	size_t								   bitstreamDataOffset; // bitstream data offset in bitstreamData buffer
	size_t								   bitstreamDataLen; /** Number of bytes in bitstream data buffer                  */
	VkSharedBaseObj<VulkanBitstreamBuffer> bitstreamData; /** bitstream data for this picture (slice-layer) */
	VkVideoDecodeInfoKHR				   decodeFrameInfo;
	VkVideoPictureResourceInfoKHR		   dpbSetupPictureResource;
	int32_t								   numGopReferenceSlots;
	int8_t								   pGopReferenceImagesIndexes[MAX_DPB_REF_AND_SETUP_SLOTS];
	VkVideoPictureResourceInfoKHR		   pictureResources[MAX_DPB_REF_AND_SETUP_SLOTS];
};

} // video
} // vkt

#endif // _EXTNVIDIAVIDEOPARSERIF_HPP
