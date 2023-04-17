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
 * \brief Video Decoding Session tests
 *//*--------------------------------------------------------------------*/

#include "vktVideoDecodeTests.hpp"
#include "vktVideoTestUtils.hpp"
#include "vktTestCase.hpp"
#include "vktVideoPictureUtils.hpp"

#include "tcuTextureUtil.hpp"
#include "tcuVectorUtil.hpp"
#include "tcuTestLog.hpp"
#include "tcuPlatform.hpp"
#include "tcuFunctionLibrary.hpp"
#include "tcuImageCompare.hpp"

#include <deDefs.h>
#include "vkDefs.hpp"
#include "vkBufferWithMemory.hpp"
#include "vkImageWithMemory.hpp"
#include "vkImageUtil.hpp"
#include "vkBarrierUtil.hpp"
#include "vkObjUtil.hpp"
#include "vkCmdUtil.hpp"
#include "vkTypeUtil.hpp"

#ifdef DE_BUILD_VIDEO
	#include "vktVideoSessionNvUtils.hpp"
	#include "extESExtractor.hpp"
	#include "vktVideoBaseDecodeUtils.hpp"
    #include "vktVideoReferenceChecksums.hpp"
#endif

#include <atomic>

namespace vkt
{
namespace video
{
namespace
{
using namespace vk;
using namespace std;

using de::MovePtr;
using vkt::ycbcr::MultiPlaneImageData;


enum TestType
{
	TEST_TYPE_H264_DECODE_I,							// Case 6
	TEST_TYPE_H264_DECODE_I_P,							// Case 7
	TEST_TYPE_H264_DECODE_I_P_B_13,						// Case 7a
	TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER,		// Case 8
	TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER,	// Case 8a
	TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS,		// Case 9
	TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE,			// Case 17
	TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB,		// Case 18
	TEST_TYPE_H264_DECODE_INTERLEAVED,					// Case 21
	TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED,		// Case 23
	TEST_TYPE_H264_H265_DECODE_INTERLEAVED,				// Case 24

	TEST_TYPE_H265_DECODE_I,							// Case 15
	TEST_TYPE_H265_DECODE_I_P,							// Case 16
	TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER,		// Case 16-2
	TEST_TYPE_H265_DECODE_I_P_B_13				,		// Case 16-3
	TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER,	// Case 16-4

	TEST_TYPE_LAST
};

struct CaseDef
{
	TestType	testType;
};

// Vulkan video is not supported on android platform
// all external libraries, helper functions and test instances has been excluded
#ifdef DE_BUILD_VIDEO
DecodedFrame initDecodeFrame (void)
{
	DecodedFrame							frameTemplate =
	{
		-1,							//  int32_t				pictureIndex;
		DE_NULL,					//  const ImageObject*	pDecodedImage;
		VK_IMAGE_LAYOUT_UNDEFINED,	//  VkImageLayout		decodedImageLayout;
		DE_NULL,					//  VkFence				frameCompleteFence;
		DE_NULL,					//  VkFence				frameConsumerDoneFence;
		DE_NULL,					//  VkSemaphore			frameCompleteSemaphore;
		DE_NULL,					//  VkSemaphore			frameConsumerDoneSemaphore;
		DE_NULL,					//  VkQueryPool			queryPool;
		0,							//  int32_t				startQueryId;
		0,							//  uint32_t			numQueries;
		0,							//  uint64_t			timestamp;
		0,							//  uint32_t			hasConsummerSignalFence : 1;
		0,							//  uint32_t			hasConsummerSignalSemaphore : 1;
		0,							//  int32_t				decodeOrder;
		0,							//  int32_t				displayOrder;
	};

	return frameTemplate;
}

MovePtr<MultiPlaneImageData> getDecodedImage (const DeviceInterface&	vkd,
											  VkDevice					device,
											  Allocator&				allocator,
											  VkImage					image,
											  VkImageLayout				layout,
											  VkFormat					format,
											  VkExtent2D				codedExtent,
											  deUint32					queueFamilyIndexTransfer,
											  deUint32					queueFamilyIndexDecode)
{
	MovePtr<MultiPlaneImageData>	multiPlaneImageData				(new MultiPlaneImageData(format, tcu::UVec2(codedExtent.width, codedExtent.height)));
	const VkQueue					queueDecode						= getDeviceQueue(vkd, device, queueFamilyIndexDecode, 0u);
	const VkQueue					queueTransfer					= getDeviceQueue(vkd, device, queueFamilyIndexTransfer, 0u);
	const VkImageSubresourceRange	imageSubresourceRange			= makeImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
	const VkImageMemoryBarrier2KHR	imageBarrierDecode				= makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
																							  VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
																							  VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR,
																							  VK_ACCESS_NONE_KHR,
																							  layout,
																							  VK_IMAGE_LAYOUT_GENERAL,
																							  image,
																							  imageSubresourceRange);
	const VkImageMemoryBarrier2KHR	imageBarrierOwnershipDecode		= makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR,
																							  VK_ACCESS_NONE_KHR,
																							  VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
																							  VK_ACCESS_NONE_KHR,
																							  VK_IMAGE_LAYOUT_GENERAL,
																							  VK_IMAGE_LAYOUT_GENERAL,
																							  image,
																							  imageSubresourceRange,
																							  queueFamilyIndexDecode,
																							  queueFamilyIndexTransfer);
	const VkImageMemoryBarrier2KHR	imageBarrierOwnershipTransfer	= makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
																							  VK_ACCESS_NONE_KHR,
																							  VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
																							  VK_ACCESS_NONE_KHR,
																							  VK_IMAGE_LAYOUT_GENERAL,
																							  VK_IMAGE_LAYOUT_GENERAL,
																							  image,
																							  imageSubresourceRange,
																							  queueFamilyIndexDecode,
																							  queueFamilyIndexTransfer);
	const VkImageMemoryBarrier2KHR	imageBarrierTransfer			= makeImageMemoryBarrier2(VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
																							  VK_ACCESS_2_TRANSFER_READ_BIT_KHR,
																							  VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR,
																							  VK_ACCESS_NONE_KHR,
																							  VK_IMAGE_LAYOUT_GENERAL,
																							  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
																							  image,
																							  imageSubresourceRange);
	const Move<VkCommandPool>		cmdDecodePool					(makeCommandPool(vkd, device, queueFamilyIndexDecode));
	const Move<VkCommandBuffer>		cmdDecodeBuffer					(allocateCommandBuffer(vkd, device, *cmdDecodePool, VK_COMMAND_BUFFER_LEVEL_PRIMARY));
	const Move<VkCommandPool>		cmdTransferPool					(makeCommandPool(vkd, device, queueFamilyIndexTransfer));
	const Move<VkCommandBuffer>		cmdTransferBuffer				(allocateCommandBuffer(vkd, device, *cmdTransferPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY));
	Move<VkSemaphore>				semaphore						= createSemaphore(vkd, device);
	Move<VkFence>					decodeFence						= createFence(vkd, device);
	Move<VkFence>					transferFence					= createFence(vkd, device);
	VkFence							fences[]						= { *decodeFence, *transferFence };
	const VkPipelineStageFlags		waitDstStageMask				= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	const VkSubmitInfo				decodeSubmitInfo
	{
		VK_STRUCTURE_TYPE_SUBMIT_INFO,						// VkStructureType				sType;
		DE_NULL,											// const void*					pNext;
		0u,													// deUint32						waitSemaphoreCount;
		DE_NULL,											// const VkSemaphore*			pWaitSemaphores;
		DE_NULL,											// const VkPipelineStageFlags*	pWaitDstStageMask;
		1u,													// deUint32						commandBufferCount;
		&*cmdDecodeBuffer,									// const VkCommandBuffer*		pCommandBuffers;
		1u,													// deUint32						signalSemaphoreCount;
		&*semaphore,										// const VkSemaphore*			pSignalSemaphores;
	};
	const VkSubmitInfo				transferSubmitInfo
	{
		VK_STRUCTURE_TYPE_SUBMIT_INFO,						// VkStructureType				sType;
		DE_NULL,											// const void*					pNext;
		1u,													// deUint32						waitSemaphoreCount;
		&*semaphore,										// const VkSemaphore*			pWaitSemaphores;
		&waitDstStageMask,									// const VkPipelineStageFlags*	pWaitDstStageMask;
		1u,													// deUint32						commandBufferCount;
		&*cmdTransferBuffer,								// const VkCommandBuffer*		pCommandBuffers;
		0u,													// deUint32						signalSemaphoreCount;
		DE_NULL,											// const VkSemaphore*			pSignalSemaphores;
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

	return multiPlaneImageData;
}

class VideoDecodeTestInstance : public VideoBaseTestInstance
{
public:
	typedef std::pair<tcu::IVec3, tcu::IVec3> ReferencePixel;

										VideoDecodeTestInstance					(Context&						context,
																				 const CaseDef&					data);
										~VideoDecodeTestInstance				(void);

	std::string							getTestVideoData						(void);

	tcu::TestStatus						iterate									(void);
	tcu::TestStatus						iterateSingleFrame						(void);
	tcu::TestStatus						iterateDoubleFrame						(void);
	tcu::TestStatus						iterateMultipleFrame					(void);

protected:
	CaseDef								m_caseDef;
	MovePtr<VideoBaseDecoder>			m_decoder;
	VkVideoCodecOperationFlagBitsKHR	m_videoCodecOperation;
	int32_t								m_frameCountTrigger;
	bool								m_queryWithStatusRequired;
};

VideoDecodeTestInstance::VideoDecodeTestInstance (Context& context, const CaseDef& data)
	: VideoBaseTestInstance			(context)
	, m_caseDef						(data)
	, m_decoder						(new VideoBaseDecoder(context))
	, m_videoCodecOperation			(VK_VIDEO_CODEC_OPERATION_NONE_KHR)
	, m_frameCountTrigger			(0)
	, m_queryWithStatusRequired		(false)
{
	const bool		queryResultWithStatus		= m_caseDef.testType == TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS;
	const bool		twoCachedPicturesSwapped	=  queryResultWithStatus
												|| m_caseDef.testType == TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER
												|| m_caseDef.testType == TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER;
	const bool		randomOrSwapped				=  twoCachedPicturesSwapped
												|| m_caseDef.testType == TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER
												|| m_caseDef.testType == TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER;
	const uint32_t	gopSize						= m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE ? 15
												: m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB ? 15
												: 0;
	const uint32_t	gopCount					= m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE ? 2
												: m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB ? 1
												: 0;
	const bool		submitDuringRecord			=  m_caseDef.testType == TEST_TYPE_H264_DECODE_I
												|| m_caseDef.testType == TEST_TYPE_H264_DECODE_I_P
												|| m_caseDef.testType == TEST_TYPE_H265_DECODE_I
												|| m_caseDef.testType == TEST_TYPE_H265_DECODE_I_P;
	const bool		submitAfter					= !submitDuringRecord;

	m_frameCountTrigger		= m_caseDef.testType == TEST_TYPE_H264_DECODE_I									? 1
							: m_caseDef.testType == TEST_TYPE_H264_DECODE_I_P								? 2
							: m_caseDef.testType == TEST_TYPE_H264_DECODE_I_P_B_13							? 13 * 2
							: m_caseDef.testType == TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER			? 2
							: m_caseDef.testType == TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER		? 13 * 2
							: m_caseDef.testType == TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS			? 2
							: m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE					? 15 * 2
							: m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB				? 15 * 2
							: m_caseDef.testType == TEST_TYPE_H265_DECODE_I									? 1
							: m_caseDef.testType == TEST_TYPE_H265_DECODE_I_P								? 2
							: m_caseDef.testType == TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER			? 2
							: m_caseDef.testType == TEST_TYPE_H265_DECODE_I_P_B_13							? 13 * 2
							: m_caseDef.testType == TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER		? 13 * 2
							: 0;

	m_decoder->setDecodeParameters(randomOrSwapped, queryResultWithStatus, m_frameCountTrigger, submitAfter, gopSize, gopCount);

	m_videoCodecOperation	= de::inBounds(m_caseDef.testType, TEST_TYPE_H264_DECODE_I, TEST_TYPE_H265_DECODE_I) ? VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR
							: de::inBounds(m_caseDef.testType, TEST_TYPE_H265_DECODE_I, TEST_TYPE_LAST) ? VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR
							: VK_VIDEO_CODEC_OPERATION_NONE_KHR;

	DE_ASSERT(m_videoCodecOperation != VK_VIDEO_CODEC_OPERATION_NONE_KHR);

	m_queryWithStatusRequired = (m_caseDef.testType == TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS);
}

VideoDecodeTestInstance::~VideoDecodeTestInstance (void)
{
}

static const std::string& frameReferenceChecksum (TestType test, int frameNumber)
{
	switch (test) {
		case TEST_TYPE_H264_DECODE_I:
		case TEST_TYPE_H264_DECODE_I_P:
		case TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER:
		case TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS:
			return TestReferenceChecksums::clipA.at(frameNumber);
		case TEST_TYPE_H264_DECODE_I_P_B_13:
		case TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER:
			return TestReferenceChecksums::jellyfishAVC.at(frameNumber);
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE:
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB:
			return TestReferenceChecksums::clipC.at(frameNumber);
		case TEST_TYPE_H265_DECODE_I:
		case TEST_TYPE_H265_DECODE_I_P:
		case TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER:
			// Clip A and clip D have the same reference checksums.
			return TestReferenceChecksums::clipA.at(frameNumber);
		case TEST_TYPE_H265_DECODE_I_P_B_13:
		case TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER:
			return TestReferenceChecksums::jellyfishHEVC.at(frameNumber);
		default: TCU_THROW(InternalError, "Unknown test type");
	}
}

std::string VideoDecodeTestInstance::getTestVideoData (void)
{
	switch (m_caseDef.testType)
	{
		case TEST_TYPE_H264_DECODE_I:
		case TEST_TYPE_H264_DECODE_I_P:
		case TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER:
		case TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS:	return getVideoDataClipA();
		case TEST_TYPE_H264_DECODE_I_P_B_13:
		case TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER:	return getVideoDataClipH264G13();
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE:
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB:		return getVideoDataClipC();
		case TEST_TYPE_H265_DECODE_I:
		case TEST_TYPE_H265_DECODE_I_P:
		case TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER:		return getVideoDataClipD();
		case TEST_TYPE_H265_DECODE_I_P_B_13:
		case TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER:	return getVideoDataClipH265G13();

		default:												TCU_THROW(InternalError, "Unknown testType");
	}
}

tcu::TestStatus VideoDecodeTestInstance::iterate (void)
{
	if (m_frameCountTrigger == 1)
		return iterateSingleFrame();
	else if (m_frameCountTrigger == 2)
		return iterateDoubleFrame();
	else
		return iterateMultipleFrame();
}

vk::VkExtensionProperties getExtensionVersion (VkVideoCodecOperationFlagBitsKHR videoCodecOperation)
{
	static const vk::VkExtensionProperties h264StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };
	static const vk::VkExtensionProperties h265StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION };

	if (videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
        return h264StdExtensionVersion;
    } else if (videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
        return h265StdExtensionVersion;
    }

    TCU_THROW(InternalError, "Unsupported Codec Type");
}


tcu::TestStatus VideoDecodeTestInstance::iterateSingleFrame (void)
{
	tcu::TestLog&							log							= m_context.getTestContext().getLog();
	const VideoDevice::VideoDeviceFlags		videoDeviceFlags			= VideoDevice::VIDEO_DEVICE_FLAG_REQUIRE_SYNC2_OR_NOT_SUPPORTED
																		| (m_queryWithStatusRequired ? VideoDevice::VIDEO_DEVICE_FLAG_QUERY_WITH_STATUS_FOR_DECODE_SUPPORT : 0);
	const VkDevice							device						= getDeviceSupportingQueue(VK_QUEUE_VIDEO_DECODE_BIT_KHR | VK_QUEUE_TRANSFER_BIT, m_videoCodecOperation, videoDeviceFlags);
	const DeviceInterface&					vkd							= getDeviceDriver();
	const deUint32							queueFamilyIndexDecode		= getQueueFamilyIndexDecode();
	const deUint32							queueFamilyIndexTransfer	= getQueueFamilyIndexTransfer();
	Allocator&								allocator					= getAllocator();
	std::string								videoData					= getTestVideoData();
	VkExtensionProperties					stdExtensionVersion			= getExtensionVersion(m_videoCodecOperation);

	MovePtr<IfcVulkanVideoDecodeParser>		vulkanVideoDecodeParser		(m_decoder->GetNvFuncs()->createIfcVulkanVideoDecodeParser(m_videoCodecOperation, &stdExtensionVersion));
	bool									videoStreamHasEnded			= false;
	int32_t									framesInQueue				= 0;
	int32_t									frameNumber					= 0;
	int32_t									framesCorrect				= 0;
	DecodedFrame							frame						= initDecodeFrame();
	ESEDemuxer								demuxer(videoData, log);

	m_decoder->initialize(m_videoCodecOperation, vkd, device, queueFamilyIndexTransfer, queueFamilyIndexDecode, allocator);

	if (!vulkanVideoDecodeParser->initialize(dynamic_cast<NvidiaVulkanParserVideoDecodeClient*>(m_decoder.get())))
	{
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->initialize()");
	}

	while (framesInQueue > 0 || !videoStreamHasEnded)
	{
		framesInQueue = m_decoder->GetVideoFrameBuffer()->DequeueDecodedPicture(&frame);

		while (framesInQueue == 0 && !videoStreamHasEnded)
		{
			if (!videoStreamHasEnded)
			{
				deUint8*	pData			= 0;
				deInt64		size			= 0;
				const bool	demuxerSuccess	= demuxer.Demux(&pData, &size);
				const bool	parserSuccess	= vulkanVideoDecodeParser->parseByteStream(pData, size);

				if (!demuxerSuccess || !parserSuccess)
					videoStreamHasEnded = true;
			}

			framesInQueue = m_decoder->GetVideoFrameBuffer()->DequeueDecodedPicture(&frame);
		}

		if (frame.pictureIndex >= 0)
		{
			const VkExtent2D				imageExtent	= frame.pDecodedImage->getExtent();
			const VkImage					image		= frame.pDecodedImage->getImage();
			const VkFormat					format		= frame.pDecodedImage->getFormat();
			const VkImageLayout				layout		= frame.decodedImageLayout;
			MovePtr<MultiPlaneImageData>	resultImage	= getDecodedImage(vkd, device, allocator, image, layout, format, imageExtent, queueFamilyIndexTransfer, queueFamilyIndexDecode);

			if (checksumFrame(*resultImage, frameReferenceChecksum(m_caseDef.testType, frameNumber)))
				framesCorrect++;

			m_decoder->ReleaseDisplayedFrame(&frame);
			frameNumber++;

			if (frameNumber >= 1)
				break;
		}
	}

	if (!vulkanVideoDecodeParser->deinitialize())
	{
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->deinitialize()");
	}

	if (framesCorrect > 0 && framesCorrect == frameNumber)
		return tcu::TestStatus::pass("pass");
	else
		return tcu::TestStatus::fail(de::toString(framesCorrect) + " out of " + de::toString(frameNumber) + " frames rendered correctly");
}

tcu::TestStatus VideoDecodeTestInstance::iterateDoubleFrame (void)
{
	tcu::TestLog&							log							= m_context.getTestContext().getLog();
	const VideoDevice::VideoDeviceFlags		videoDeviceFlags			= VideoDevice::VIDEO_DEVICE_FLAG_REQUIRE_SYNC2_OR_NOT_SUPPORTED
																		| (m_queryWithStatusRequired ? VideoDevice::VIDEO_DEVICE_FLAG_QUERY_WITH_STATUS_FOR_DECODE_SUPPORT : 0);
	const VkDevice							device						= getDeviceSupportingQueue(VK_QUEUE_VIDEO_DECODE_BIT_KHR | VK_QUEUE_TRANSFER_BIT, m_videoCodecOperation, videoDeviceFlags);
	const DeviceInterface&					vkd							= getDeviceDriver();
	const deUint32							queueFamilyIndexDecode		= getQueueFamilyIndexDecode();
	const deUint32							queueFamilyIndexTransfer	= getQueueFamilyIndexTransfer();
	Allocator&								allocator					= getAllocator();
	std::string								videoData					= getTestVideoData();
	VkExtensionProperties					stdExtensionVersion			= getExtensionVersion(m_videoCodecOperation);

	MovePtr<IfcVulkanVideoDecodeParser>		vulkanVideoDecodeParser		(m_decoder->GetNvFuncs()->createIfcVulkanVideoDecodeParser(m_videoCodecOperation, &stdExtensionVersion));
	bool									videoStreamHasEnded			= false;
	int32_t									framesInQueue				= 0;
	int32_t									frameNumber					= 0;
	int32_t									framesCorrect				= 0;
	DecodedFrame							frames[2]					= { initDecodeFrame(), initDecodeFrame() };
	ESEDemuxer								demuxer(videoData, log);

	m_decoder->initialize(m_videoCodecOperation, vkd, device, queueFamilyIndexTransfer, queueFamilyIndexDecode, allocator);

	if (!vulkanVideoDecodeParser->initialize(dynamic_cast<NvidiaVulkanParserVideoDecodeClient*>(m_decoder.get())))
	{
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->initialize()");
	}

	while (framesInQueue > 0 || !videoStreamHasEnded)
	{
		framesInQueue = m_decoder->GetVideoFrameBuffer()->GetDisplayFramesCount();

		while (framesInQueue < 2 && !videoStreamHasEnded)
		{
			if (!videoStreamHasEnded)
			{
				deUint8*	pData			= 0;
				deInt64		size			= 0;
				const bool	demuxerSuccess	= demuxer.Demux(&pData, &size);
				const bool	parserSuccess	= vulkanVideoDecodeParser->parseByteStream(pData, size);

				if (!demuxerSuccess || !parserSuccess)
					videoStreamHasEnded = true;
			}

			framesInQueue = m_decoder->GetVideoFrameBuffer()->GetDisplayFramesCount();
		}

		for (size_t frameNdx = 0; frameNdx < 2; ++frameNdx)
		{
			DecodedFrame&	frame	= frames[frameNdx];

			m_decoder->GetVideoFrameBuffer()->DequeueDecodedPicture(&frame);
		}

		for (size_t frameNdx = 0; frameNdx < 2; ++frameNdx)
		{
			DecodedFrame&		frame		= frames[frameNdx];
			const VkExtent2D	imageExtent	= frame.pDecodedImage->getExtent();
			const VkImage		image		= frame.pDecodedImage->getImage();
			const VkFormat		format		= frame.pDecodedImage->getFormat();
			const VkImageLayout	layout		= frame.decodedImageLayout;

			if (frame.pictureIndex >= 0)
			{
				const bool						assumeCorrect	= m_caseDef.testType == TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS;
				MovePtr<MultiPlaneImageData>	resultImage		= getDecodedImage(vkd, device, allocator, image, layout, format, imageExtent, queueFamilyIndexTransfer, queueFamilyIndexDecode);

				if (assumeCorrect || checksumFrame(*resultImage, frameReferenceChecksum(m_caseDef.testType, frameNumber)))
					framesCorrect++;

				m_decoder->ReleaseDisplayedFrame(&frame);
				frameNumber++;

				if (frameNumber >= DE_LENGTH_OF_ARRAY(frames))
					break;
			}
		}

		if (frameNumber >= DE_LENGTH_OF_ARRAY(frames))
			break;
	}

	if (!vulkanVideoDecodeParser->deinitialize())
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->deinitialize()");

	if (framesCorrect > 0 && framesCorrect == frameNumber)
		return tcu::TestStatus::pass("pass");
	else
		return tcu::TestStatus::fail(de::toString(framesCorrect) + " out of " + de::toString(frameNumber) + " frames rendered correctly");
}

tcu::TestStatus VideoDecodeTestInstance::iterateMultipleFrame (void)
{
	tcu::TestLog&						log							= m_context.getTestContext().getLog();
	const VideoDevice::VideoDeviceFlags	videoDeviceFlags			= VideoDevice::VIDEO_DEVICE_FLAG_REQUIRE_SYNC2_OR_NOT_SUPPORTED
																	| (m_queryWithStatusRequired ? VideoDevice::VIDEO_DEVICE_FLAG_QUERY_WITH_STATUS_FOR_DECODE_SUPPORT : 0);
	const VkDevice						device						= getDeviceSupportingQueue(VK_QUEUE_VIDEO_DECODE_BIT_KHR | VK_QUEUE_TRANSFER_BIT, m_videoCodecOperation, videoDeviceFlags);
	const DeviceInterface&				vkd							= getDeviceDriver();
	const deUint32						queueFamilyIndexDecode		= getQueueFamilyIndexDecode();
	const deUint32						queueFamilyIndexTransfer	= getQueueFamilyIndexTransfer();
	Allocator&							allocator					= getAllocator();
	std::string							videoData					= getTestVideoData();
	VkExtensionProperties				stdExtensionVersion			= getExtensionVersion(m_videoCodecOperation);

	MovePtr<IfcVulkanVideoDecodeParser>	vulkanVideoDecodeParser		(m_decoder->GetNvFuncs()->createIfcVulkanVideoDecodeParser(m_videoCodecOperation, &stdExtensionVersion));
	bool								videoStreamHasEnded			= false;
	int32_t								framesInQueue				= 0;
	int32_t								frameNumber					= 0;
	int32_t								framesCorrect				= 0;
	vector<DecodedFrame>				frames						(m_frameCountTrigger, initDecodeFrame());
	ESEDemuxer							demuxer(videoData, log);

	m_decoder->initialize(m_videoCodecOperation, vkd, device, queueFamilyIndexTransfer, queueFamilyIndexDecode, allocator);

	if (!vulkanVideoDecodeParser->initialize(dynamic_cast<NvidiaVulkanParserVideoDecodeClient*>(m_decoder.get())))
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->initialize()");

	while (framesInQueue > 0 || !videoStreamHasEnded)
	{
		framesInQueue = m_decoder->GetVideoFrameBuffer()->GetDisplayFramesCount();

		while (framesInQueue < m_frameCountTrigger && !videoStreamHasEnded)
		{
			if (!videoStreamHasEnded)
			{
				deUint8*	pData			= 0;
				deInt64		size			= 0;
				const bool	demuxerSuccess	= demuxer.Demux(&pData, &size);
				const bool	parserSuccess	= vulkanVideoDecodeParser->parseByteStream(pData, size);

				if (!demuxerSuccess || !parserSuccess)
					videoStreamHasEnded = true;
			}

			framesInQueue = m_decoder->GetVideoFrameBuffer()->GetDisplayFramesCount();
		}

		for (int32_t frameNdx = 0; frameNdx < m_frameCountTrigger; ++frameNdx)
		{
			DecodedFrame&	frame	= frames[frameNdx];

			m_decoder->GetVideoFrameBuffer()->DequeueDecodedPicture(&frame);
		}

		bool	isResolutionChangeTest = m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE || m_caseDef.testType == TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB;

		for (int32_t frameNdx = 0; frameNdx < m_frameCountTrigger; ++frameNdx)
		{
			DecodedFrame&		frame		= frames[frameNdx];
			VkExtent2D	imageExtent	= frame.pDecodedImage->getExtent();
			const VkImage		image		= frame.pDecodedImage->getImage();
			const VkFormat		format		= frame.pDecodedImage->getFormat();
			const VkImageLayout	layout		= frame.decodedImageLayout;

			if (frame.pictureIndex >= 0)
			{
				// FIXME: The active coded extent is not being tracked
				// in the CTS yet. This is a hack for the resolution
				// change case until that core problem is properly
				// addressed.
				if (isResolutionChangeTest && frameNdx >= 15) {
					imageExtent.width = 176;
					imageExtent.height = 144;
				}
				MovePtr<MultiPlaneImageData>	resultImage	= getDecodedImage(vkd, device, allocator, image, layout, format, imageExtent, queueFamilyIndexTransfer, queueFamilyIndexDecode);

				if (checksumFrame(*resultImage, frameReferenceChecksum(m_caseDef.testType, frameNumber)))
					framesCorrect++;

				m_decoder->ReleaseDisplayedFrame(&frame);
				frameNumber++;
			}
		}
	}

	if (!vulkanVideoDecodeParser->deinitialize())
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->deinitialize()");

	if (framesCorrect > 0 && framesCorrect == frameNumber)
		return tcu::TestStatus::pass("pass");
	else
		return tcu::TestStatus::fail(de::toString(framesCorrect) + " out of " + de::toString(frameNumber) + " frames rendered correctly");
}

class DualVideoDecodeTestInstance : public VideoBaseTestInstance
{
public:
										DualVideoDecodeTestInstance		(Context&					context,
																		 const CaseDef&				data);
										~DualVideoDecodeTestInstance	(void);

	std::string							getTestVideoData				(bool						primary);
	const std::string&					getFrameReferenceChecksum		(bool						primary,
																		 int						frameNumber);
	tcu::TestStatus						iterate							(void);

protected:
	CaseDef								m_caseDef;
	MovePtr<VideoBaseDecoder>			m_decoder1;
	MovePtr<VideoBaseDecoder>			m_decoder2;
	VkVideoCodecOperationFlagBitsKHR	m_videoCodecOperation;
	VkVideoCodecOperationFlagBitsKHR	m_videoCodecOperation1;
	VkVideoCodecOperationFlagBitsKHR	m_videoCodecOperation2;
	int32_t								m_frameCountTrigger;
};

DualVideoDecodeTestInstance::DualVideoDecodeTestInstance (Context& context, const CaseDef& data)
	: VideoBaseTestInstance		(context)
	, m_caseDef					(data)
	, m_decoder1				(new VideoBaseDecoder(context))
	, m_decoder2				(new VideoBaseDecoder(context))
	, m_videoCodecOperation		(VK_VIDEO_CODEC_OPERATION_NONE_KHR)
	, m_videoCodecOperation1	(VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
	, m_videoCodecOperation2	(VK_VIDEO_CODEC_OPERATION_NONE_KHR)
	, m_frameCountTrigger		(10)
{
	const bool	randomOrSwapped			= false;
	const bool	queryResultWithStatus	= false;

	m_decoder1->setDecodeParameters(randomOrSwapped, queryResultWithStatus, m_frameCountTrigger + 1);
	m_decoder2->setDecodeParameters(randomOrSwapped, queryResultWithStatus, m_frameCountTrigger + 1);

	m_videoCodecOperation2	= m_caseDef.testType == TEST_TYPE_H264_DECODE_INTERLEAVED				? VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR
							: m_caseDef.testType == TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED	? VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_EXT
							: m_caseDef.testType == TEST_TYPE_H264_H265_DECODE_INTERLEAVED			? VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR
							: VK_VIDEO_CODEC_OPERATION_NONE_KHR;

	DE_ASSERT(m_videoCodecOperation2 != VK_VIDEO_CODEC_OPERATION_NONE_KHR);

	m_videoCodecOperation = static_cast<VkVideoCodecOperationFlagBitsKHR>(m_videoCodecOperation1 | m_videoCodecOperation2);

	if (m_videoCodecOperation2 == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_EXT)
		TCU_THROW(NotSupportedError, "NOT IMPLEMENTED: REQUIRES ENCODE QUEUE");
}

DualVideoDecodeTestInstance::~DualVideoDecodeTestInstance (void)
{
}

std::string DualVideoDecodeTestInstance::getTestVideoData (bool primary)
{
	switch (m_caseDef.testType)
	{
		case TEST_TYPE_H264_DECODE_INTERLEAVED:					return primary ? getVideoDataClipA() : getVideoDataClipB();
		case TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED:		return getVideoDataClipA();
		case TEST_TYPE_H264_H265_DECODE_INTERLEAVED:			return primary ? getVideoDataClipA() : getVideoDataClipD();
		default:												TCU_THROW(InternalError, "Unknown testType");
	}
}

const std::string& DualVideoDecodeTestInstance::getFrameReferenceChecksum (bool primary, int frameNumber)
{
	switch (m_caseDef.testType)
	{
		case TEST_TYPE_H264_DECODE_INTERLEAVED:					return primary ? TestReferenceChecksums::clipA.at(frameNumber) : TestReferenceChecksums::clipB.at(frameNumber);
		case TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED: /* fallthru */
		case TEST_TYPE_H264_H265_DECODE_INTERLEAVED:			return TestReferenceChecksums::clipA.at(frameNumber);
		default:												TCU_THROW(InternalError, "Unknown testType");
	}
}

tcu::TestStatus DualVideoDecodeTestInstance::iterate (void)
{
	tcu::TestLog&						log							= m_context.getTestContext().getLog();
	const VideoDevice::VideoDeviceFlags	videoDeviceFlags			= VideoDevice::VIDEO_DEVICE_FLAG_REQUIRE_SYNC2_OR_NOT_SUPPORTED;
	const VkDevice						device						= getDeviceSupportingQueue(VK_QUEUE_VIDEO_DECODE_BIT_KHR | VK_QUEUE_TRANSFER_BIT, m_videoCodecOperation, videoDeviceFlags);
	const DeviceInterface&				vkd							= getDeviceDriver();
	const deUint32						queueFamilyIndexDecode		= getQueueFamilyIndexDecode();
	const deUint32						queueFamilyIndexTransfer	= getQueueFamilyIndexTransfer();
	Allocator&							allocator					= getAllocator();
	std::string							videoData1					= getTestVideoData(true);
	std::string							videoData2					= getTestVideoData(false);

	VkExtensionProperties				stdExtensionVersion1		= getExtensionVersion(m_videoCodecOperation1);
	VkExtensionProperties				stdExtensionVersion2		= getExtensionVersion(m_videoCodecOperation2);

	MovePtr<IfcVulkanVideoDecodeParser>	vulkanVideoDecodeParser1	(m_decoder1->GetNvFuncs()->createIfcVulkanVideoDecodeParser(m_videoCodecOperation1, &stdExtensionVersion1));
	MovePtr<IfcVulkanVideoDecodeParser>	vulkanVideoDecodeParser2	(m_decoder2->GetNvFuncs()->createIfcVulkanVideoDecodeParser(m_videoCodecOperation2, &stdExtensionVersion2));
	int32_t								frameNumber					= 0;
	int32_t								framesCorrect				= 0;
	vector<DecodedFrame>				frames						(m_frameCountTrigger, initDecodeFrame());
	ESEDemuxer							demuxer1(videoData1, log);
	ESEDemuxer							demuxer2(videoData2, log);

	m_decoder1->initialize(m_videoCodecOperation1, vkd, device, queueFamilyIndexTransfer, queueFamilyIndexDecode, allocator);

	if (!vulkanVideoDecodeParser1->initialize(dynamic_cast<NvidiaVulkanParserVideoDecodeClient*>(m_decoder1.get())))
	{
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->initialize()");
	}

	m_decoder2->initialize(m_videoCodecOperation2, vkd, device, queueFamilyIndexTransfer, queueFamilyIndexDecode, allocator);

	if (!vulkanVideoDecodeParser2->initialize(dynamic_cast<NvidiaVulkanParserVideoDecodeClient*>(m_decoder2.get())))
	{
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->initialize()");
	}

	{
		bool	videoStreamHasEnded	= false;
		int32_t	framesInQueue		= 0;

		while (framesInQueue < m_frameCountTrigger && !videoStreamHasEnded)
		{
			deUint8*	pData			= 0;
			deInt64		size			= 0;
			const bool	demuxerSuccess	= demuxer1.Demux(&pData, &size);
			const bool	parserSuccess	= vulkanVideoDecodeParser1->parseByteStream(pData, size);

			if (!demuxerSuccess || !parserSuccess)
				videoStreamHasEnded = true;

			framesInQueue = m_decoder1->GetVideoFrameBuffer()->GetDisplayFramesCount();
		}
	}

	{
		bool	videoStreamHasEnded	= false;
		int32_t	framesInQueue		= 0;

		while (framesInQueue < m_frameCountTrigger && !videoStreamHasEnded)
		{
			deUint8*	pData			= 0;
			deInt64		size			= 0;
			const bool	demuxerSuccess	= demuxer2.Demux(&pData, &size);
			const bool	parserSuccess	= vulkanVideoDecodeParser2->parseByteStream(pData, size);

			if (!demuxerSuccess || !parserSuccess)
				videoStreamHasEnded = true;

			framesInQueue = m_decoder2->GetVideoFrameBuffer()->GetDisplayFramesCount();
		}
	}

	m_decoder1->DecodeCachedPictures(m_decoder2.get());

	for (size_t decoderNdx = 0; decoderNdx < 2; ++decoderNdx)
	{
		const bool			firstDecoder	= (decoderNdx == 0);
		VideoBaseDecoder*	decoder			= firstDecoder ? m_decoder1.get() : m_decoder2.get();
		const bool			firstClip		= firstDecoder ? true
											: m_caseDef.testType == TEST_TYPE_H264_H265_DECODE_INTERLEAVED;
		for (int32_t frameNdx = 0; frameNdx < m_frameCountTrigger; ++frameNdx)
		{
			decoder->GetVideoFrameBuffer()->DequeueDecodedPicture(&frames[frameNdx]);

			DecodedFrame&		frame		= frames[frameNdx];
			const VkExtent2D	imageExtent	= frame.pDecodedImage->getExtent();
			const VkImage		image		= frame.pDecodedImage->getImage();
			const VkFormat		format		= frame.pDecodedImage->getFormat();
			const VkImageLayout	layout		= frame.decodedImageLayout;

			if (frame.pictureIndex >= 0)
			{
				MovePtr<MultiPlaneImageData> resultImage = getDecodedImage(vkd, device, allocator, image, layout, format, imageExtent, queueFamilyIndexTransfer, queueFamilyIndexDecode);

				if (checksumFrame(*resultImage, getFrameReferenceChecksum(firstClip, frameNdx)))
					framesCorrect++;

				decoder->ReleaseDisplayedFrame(&frame);
				frameNumber++;
			}
		}
	}

	if (!vulkanVideoDecodeParser2->deinitialize())
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->deinitialize()");

	if (!vulkanVideoDecodeParser1->deinitialize())
		TCU_THROW(InternalError, "vulkanVideoDecodeParser->deinitialize()");

	if (framesCorrect > 0 && framesCorrect == frameNumber)
		return tcu::TestStatus::pass("pass");
	else
		return tcu::TestStatus::fail(de::toString(framesCorrect) + " out of " + de::toString(frameNumber) + " frames rendered correctly");
}

#endif // #ifdef DE_BUILD_VIDEO

class VideoDecodeTestCase : public TestCase
{
	public:
							VideoDecodeTestCase		(tcu::TestContext& context, const char* name, const char* desc, const CaseDef caseDef);
							~VideoDecodeTestCase	(void);

	virtual TestInstance*	createInstance			(Context& context) const;
	virtual void			checkSupport			(Context& context) const;

private:
	CaseDef					m_caseDef;
};

VideoDecodeTestCase::VideoDecodeTestCase (tcu::TestContext& context, const char* name, const char* desc, const CaseDef caseDef)
	: vkt::TestCase	(context, name, desc)
	, m_caseDef		(caseDef)
{
}

VideoDecodeTestCase::~VideoDecodeTestCase	(void)
{
}

void VideoDecodeTestCase::checkSupport (Context& context) const
{
#if (DE_PTR_SIZE != 8)
	// Issue #4253: https://gitlab.khronos.org/Tracker/vk-gl-cts/-/issues/4253
	// These tests rely on external libraries to do the video parsing,
	// and those libraries are only available as 64-bit at this time.
	TCU_THROW(NotSupportedError, "CTS is not built 64-bit so cannot use the 64-bit video parser library");
#endif

	context.requireDeviceFunctionality("VK_KHR_video_queue");
	context.requireDeviceFunctionality("VK_KHR_synchronization2");

	switch (m_caseDef.testType)
	{
		case TEST_TYPE_H264_DECODE_I:
		case TEST_TYPE_H264_DECODE_I_P:
		case TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER:
		case TEST_TYPE_H264_DECODE_I_P_B_13:
		case TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER:
		case TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS:
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE:
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB:
		case TEST_TYPE_H264_DECODE_INTERLEAVED:
		case TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED:
		{
			context.requireDeviceFunctionality("VK_KHR_video_decode_h264");
			break;
		}
		case TEST_TYPE_H265_DECODE_I:
		case TEST_TYPE_H265_DECODE_I_P:
		case TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER:
		case TEST_TYPE_H265_DECODE_I_P_B_13:
		case TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER:
		{
			context.requireDeviceFunctionality("VK_KHR_video_decode_h265");
			break;
		}
		case TEST_TYPE_H264_H265_DECODE_INTERLEAVED:
		{
			context.requireDeviceFunctionality("VK_KHR_video_decode_h264");
			context.requireDeviceFunctionality("VK_KHR_video_decode_h265");
			break;
		}
		default:
			TCU_THROW(InternalError, "Unknown TestType");
	}
}

TestInstance* VideoDecodeTestCase::createInstance (Context& context) const
{
// Vulkan video is unsupported for android platform
	switch (m_caseDef.testType)
	{
		case TEST_TYPE_H264_DECODE_I:
		case TEST_TYPE_H264_DECODE_I_P:
		case TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER:
		case TEST_TYPE_H264_DECODE_I_P_B_13:
		case TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER:
		case TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS:
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE:
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB:
		case TEST_TYPE_H265_DECODE_I:
		case TEST_TYPE_H265_DECODE_I_P:
		case TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER:
		case TEST_TYPE_H265_DECODE_I_P_B_13:
		case TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER:
		{
#ifdef DE_BUILD_VIDEO
			return new VideoDecodeTestInstance(context, m_caseDef);
#endif
		}
		case TEST_TYPE_H264_DECODE_INTERLEAVED:
		case TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED:
		case TEST_TYPE_H264_H265_DECODE_INTERLEAVED:
		{
#ifdef DE_BUILD_VIDEO
			return new DualVideoDecodeTestInstance(context, m_caseDef);
#endif
		}
		default:
			TCU_THROW(InternalError, "Unknown TestType");
	}
#ifndef DE_BUILD_VIDEO
	DE_UNREF(context);
#endif

}

const char* getTestName (const TestType testType)
{
	switch (testType)
	{
		case TEST_TYPE_H264_DECODE_I:							return "h264_i";
		case TEST_TYPE_H264_DECODE_I_P:							return "h264_i_p";
		case TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER:		return "h264_i_p_not_matching_order";
		case TEST_TYPE_H264_DECODE_I_P_B_13:					return "h264_i_p_b_13";
		case TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER:	return "h264_i_p_b_13_not_matching_order";
		case TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS:	return "h264_query_with_status";
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE:			return "h264_resolution_change";
		case TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB:		return "h264_resolution_change_dpb";
		case TEST_TYPE_H264_DECODE_INTERLEAVED:					return "h264_interleaved";
		case TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED:		return "h264_decode_encode_interleaved";
		case TEST_TYPE_H264_H265_DECODE_INTERLEAVED:			return "h264_h265_interleaved";
		case TEST_TYPE_H265_DECODE_I:							return "h265_i";
		case TEST_TYPE_H265_DECODE_I_P:							return "h265_i_p";
		case TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER:		return "h265_i_p_not_matching_order";
		case TEST_TYPE_H265_DECODE_I_P_B_13:					return "h265_i_p_b_13";
		case TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER:	return "h265_i_p_b_13_not_matching_order";
		default:												TCU_THROW(InternalError, "Unknown TestType");
	}
}
}	// anonymous

tcu::TestCaseGroup*	createVideoDecodeTests (tcu::TestContext& testCtx)
{
	MovePtr<tcu::TestCaseGroup>	group	(new tcu::TestCaseGroup(testCtx, "decode", "Video decoding session tests"));

	for (int testTypeNdx = 0; testTypeNdx < TEST_TYPE_LAST; ++testTypeNdx)
	{
		const TestType	testType	= static_cast<TestType>(testTypeNdx);
		const CaseDef	caseDef		=
		{
			testType,	//  TestType	testType;
		};

		group->addChild(new VideoDecodeTestCase(testCtx, getTestName(testType), "", caseDef));
	}

	return group.release();
}
}	// video
}	// vkt
