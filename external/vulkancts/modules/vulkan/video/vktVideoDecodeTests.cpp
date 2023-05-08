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
 * \brief Video decoding tests
 *//*--------------------------------------------------------------------*/

#include "vktVideoDecodeTests.hpp"
#include "vktVideoTestUtils.hpp"

#include "tcuTestLog.hpp"
#include "tcuPlatform.hpp"
#include "tcuFunctionLibrary.hpp"

#include <deDefs.h>
#include "vkDefs.hpp"
#include "vkImageWithMemory.hpp"
#include "vkCmdUtil.hpp"

#ifdef DE_BUILD_VIDEO
	#include "extESExtractor.hpp"
	#include "vktVideoBaseDecodeUtils.hpp"
    #include "vktVideoReferenceChecksums.hpp"

	#include "extNvidiaVideoParserIf.hpp"
// FIXME: The samples repo is missing this internal include from their H265 decoder
	#include "nvVulkanh265ScalingList.h"
	#include <VulkanH264Decoder.h>
	#include <VulkanH265Decoder.h>
#endif

namespace vkt
{
namespace video
{
namespace
{
using namespace vk;
using namespace std;

using de::MovePtr;

enum TestType
{
	TEST_TYPE_H264_DECODE_I,						   // Case 6
	TEST_TYPE_H264_DECODE_I_P,						   // Case 7
	TEST_TYPE_H264_DECODE_I_P_B_13,					   // Case 7a
	TEST_TYPE_H264_DECODE_I_P_NOT_MATCHING_ORDER,	   // Case 8
	TEST_TYPE_H264_DECODE_I_P_B_13_NOT_MATCHING_ORDER, // Case 8a
	TEST_TYPE_H264_DECODE_QUERY_RESULT_WITH_STATUS,	   // Case 9
	TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE,		   // Case 17
	TEST_TYPE_H264_DECODE_RESOLUTION_CHANGE_DPB,	   // Case 18
	TEST_TYPE_H264_DECODE_INTERLEAVED,				   // Case 21
	TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED,	   // Case 23
	TEST_TYPE_H264_H265_DECODE_INTERLEAVED,			   // Case 24

	TEST_TYPE_H265_DECODE_I,						   // Case 15
	TEST_TYPE_H265_DECODE_I_P,						   // Case 16
	TEST_TYPE_H265_DECODE_I_P_NOT_MATCHING_ORDER,	   // Case 16-2
	TEST_TYPE_H265_DECODE_I_P_B_13,					   // Case 16-3
	TEST_TYPE_H265_DECODE_I_P_B_13_NOT_MATCHING_ORDER, // Case 16-4

	TEST_TYPE_LAST
};

static const std::string& frameReferenceChecksum(TestType test, int frameNumber)
{
	switch (test)
	{
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
		default:
			TCU_THROW(InternalError, "Unknown test type");
	}
}

struct TestDefinition
{
	TestType		   testType;

	const char*		   videoClipFilename;

	// Used for the default size of the parser's bitstream buffer, file size of clip rounded up to the next power of 2.
	size_t			   videoClipSizeInBytes;

	// Once the frame with this number is processed, the test stops.
	size_t			   framesToCheck;

	VkVideoCoreProfile profile;
	// TODO: Video profile & level

	TestDefinition(TestType				type,
				   const char*			filename,
				   size_t				filesize,
				   size_t				numFrames,
				   VkVideoCoreProfile&& coreProfile)
		: testType(type)
		, videoClipFilename(filename)
		, videoClipSizeInBytes(filesize)
		, framesToCheck(numFrames)
		, profile(coreProfile)
	{
	}

	// Whether to perform video status queries during coding operations.
	bool						  queryResultWithStatus{false};

	VideoDevice::VideoDeviceFlags requiredDeviceFlags() const
	{
		return VideoDevice::VIDEO_DEVICE_FLAG_REQUIRE_SYNC2_OR_NOT_SUPPORTED |
			   (queryResultWithStatus ? VideoDevice::VIDEO_DEVICE_FLAG_QUERY_WITH_STATUS_FOR_DECODE_SUPPORT : 0);
	}

	const VkExtensionProperties* extensionProperties() const
	{
		static const VkExtensionProperties h264StdExtensionVersion = {
			VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION};
		static const VkExtensionProperties h265StdExtensionVersion = {
			VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION};

		switch (profile.GetCodecType())
		{
			case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
				return &h264StdExtensionVersion;
			case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
				return &h265StdExtensionVersion;
			default:
				tcu::die("Unsupported video codec %s\n", util::codecToName(profile.GetCodecType()));
				break;
		}

		TCU_THROW(InternalError, "Unsupported codec");
	};
} DecodeTestCases[] = {TestDefinition(TEST_TYPE_H264_DECODE_I,
									  "vulkan/video/clip-a.h264",
									  2 * 1024 * 1024,
									  1,
									  VkVideoCoreProfile(VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
														 VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 STD_VIDEO_H264_PROFILE_IDC_HIGH)),
					   TestDefinition(TEST_TYPE_H264_DECODE_I_P,
									  "vulkan/video/clip-a.h264",
									  2 * 1024 * 1024,
									  2,
									  VkVideoCoreProfile(VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
														 VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 STD_VIDEO_H264_PROFILE_IDC_HIGH)),
					   TestDefinition(TEST_TYPE_H264_DECODE_I_P_B_13,
									  "vulkan/video/jellyfish-250-mbps-4k-uhd-GOB-IPB13.h264",
									  4 * 1024 * 1024,
									  26,
									  VkVideoCoreProfile(VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
														 VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 STD_VIDEO_H264_PROFILE_IDC_MAIN)),
					   TestDefinition(TEST_TYPE_H265_DECODE_I,
									  "vulkan/video/clip-d.h265",
									  8 * 1024,
									  1,
									  VkVideoCoreProfile(VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR,
														 VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 STD_VIDEO_H265_PROFILE_IDC_MAIN)),
					   TestDefinition(TEST_TYPE_H265_DECODE_I_P,
									  "vulkan/video/clip-d.h265",
									  8 * 1024,
									  2,
									  VkVideoCoreProfile(VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR,
														 VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 STD_VIDEO_H265_PROFILE_IDC_MAIN)),
					   TestDefinition(TEST_TYPE_H265_DECODE_I_P_B_13,
									  "vulkan/video/jellyfish-250-mbps-4k-uhd-GOB-IPB13.h265",
									  4 * 1024 * 1024,
									  26,
									  VkVideoCoreProfile(VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR,
														 VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
														 STD_VIDEO_H265_PROFILE_IDC_MAIN))};

// Vulkan video is not supported on android platform
// all external libraries, helper functions and test instances has been excluded
#ifdef DE_BUILD_VIDEO

class VideoDecodeTestInstance : public VideoBaseTestInstance
{
public:
	VideoDecodeTestInstance(Context& context, const TestDefinition& testDefinition);

	std::string getTestVideoData(void);

	tcu::TestStatus iterate(void);

protected:
	TestDefinition			  m_testDefinition;
	MovePtr<VideoBaseDecoder> m_decoder{};
	static_assert(sizeof(DeviceContext) < 128, "DeviceContext has grown bigger than expected!");
	DeviceContext m_deviceContext;
};

VideoDecodeTestInstance::VideoDecodeTestInstance(Context& context, const TestDefinition& testDefinition)
	: VideoBaseTestInstance(context), m_testDefinition(testDefinition)
{
	VkDevice device = getDeviceSupportingQueue(VK_QUEUE_VIDEO_DECODE_BIT_KHR | VK_QUEUE_TRANSFER_BIT,
											   m_testDefinition.profile.GetCodecType(),
											   m_testDefinition.requiredDeviceFlags());

	m_deviceContext.context = &m_context;
	m_deviceContext.device	= device;
	m_deviceContext.phys	= m_context.getPhysicalDevice();
	m_deviceContext.vd		= &m_videoDevice;
	// TODO: Support for multiple queues / multithreading
	m_deviceContext.transferQueue =
		getDeviceQueue(m_context.getDeviceInterface(), device, m_videoDevice.getQueueFamilyIndexTransfer(), 0);
	m_deviceContext.decodeQueue =
		getDeviceQueue(m_context.getDeviceInterface(), device, m_videoDevice.getQueueFamilyIndexDecode(), 0);

	VkSharedBaseObj<VulkanVideoFrameBuffer> vkVideoFrameBuffer;
	VK_CHECK(VulkanVideoFrameBuffer::Create(&m_deviceContext, vkVideoFrameBuffer));

	m_decoder = de::newMovePtr<VideoBaseDecoder>(
		&m_deviceContext, m_testDefinition.profile, m_testDefinition.framesToCheck, vkVideoFrameBuffer);
}

static void createParser(TestDefinition& params, VkParserVideoDecodeClient* decoderClient, VkSharedBaseObj<VulkanVideoDecodeParser>& parser)
{
	const VkParserInitDecodeParameters pdParams = {
		NV_VULKAN_VIDEO_PARSER_API_VERSION,
		decoderClient,
		static_cast<uint32_t>(params.videoClipSizeInBytes),
		32, // FIXME: Currently failing to be able to get video caps early enough on NVIDIA
		32,
		0,
		0,
		nullptr,
		true,
	};

	const VkExtensionProperties* pStdExtensionVersion = params.extensionProperties();
	DE_ASSERT(pStdExtensionVersion);

	switch(params.profile.GetCodecType())
	{
		case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
		{
			if (strcmp(pStdExtensionVersion->extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME) || pStdExtensionVersion->specVersion != VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION) {
				tcu::die("The requested decoder h.264 Codec STD version is NOT supported. The supported decoder h.264 Codec STD version is version %d of %s\n",
								 VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);
			}
			VkSharedBaseObj<VulkanH264Decoder> nvVideoH264DecodeParser( new VulkanH264Decoder(params.profile.GetCodecType()));
			parser = nvVideoH264DecodeParser;
			break;
		}
		case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
		{
			if (strcmp(pStdExtensionVersion->extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME) || pStdExtensionVersion->specVersion != VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION) {
			tcu::die("The requested decoder h.265 Codec STD version is NOT supported. The supported decoder h.265 Codec STD version is version %d of %s\n",
					 VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME);
			}
			VkSharedBaseObj<VulkanH265Decoder> nvVideoH265DecodeParser( new VulkanH265Decoder(params.profile.GetCodecType()));
			parser = nvVideoH265DecodeParser;
			break;
		}
		default:
			TCU_FAIL("Unsupported codec type!");
	}

	VK_CHECK(parser->Initialize(&pdParams));
}

static std::vector<deUint8> semiplanarToYV12(const ycbcr::MultiPlaneImageData& multiPlaneImageData)
{
	DE_ASSERT(multiPlaneImageData.getFormat() == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);

	std::vector<deUint8> YV12Buffer;
	size_t plane0Size = multiPlaneImageData.getPlaneSize(0);
	size_t plane1Size = multiPlaneImageData.getPlaneSize(1);

	YV12Buffer.resize(plane0Size + plane1Size);

	// Copy the luma plane.
	deMemcpy(YV12Buffer.data(), multiPlaneImageData.getPlanePtr(0), plane0Size);

	// Deinterleave the Cr and Cb plane.
	uint16_t *plane2 = (uint16_t*)multiPlaneImageData.getPlanePtr(1);
	std::vector<deUint8>::size_type idx = plane0Size;
	for (unsigned i = 0 ; i < plane1Size / 2; i ++)
		YV12Buffer[idx++] = static_cast<deUint8>(plane2[i] & 0xFF);
	for (unsigned i = 0 ; i < plane1Size / 2; i ++)
		YV12Buffer[idx++] = static_cast<deUint8>((plane2[i] >> 8) & 0xFF);

	return YV12Buffer;
}

tcu::TestStatus VideoDecodeTestInstance::iterate(void)
{
	tcu::TestLog&							 log = m_context.getTestContext().getLog();
	ESEDemuxer								 demuxer(m_testDefinition.videoClipFilename, log);
	VkSharedBaseObj<VulkanVideoDecodeParser> parser;
	createParser(m_testDefinition, m_decoder.get(), parser);

	bool					  videoStreamHasEnded = false;
	int32_t					  frameNumber		  = 0;
	int32_t					  framesCorrect		  = 0;
	std::vector<DecodedFrame> frameData(6);
	for (auto& frame : frameData)
		frame.Reset();
	size_t frameDataIdx		= 0;

	auto   processNextChunk = [&demuxer, &parser]()
	{
		deUint8*				pData		   = 0;
		deInt64					size		   = 0;
		bool					demuxerSuccess = demuxer.Demux(&pData, &size);

		VkParserBitstreamPacket pkt;
		pkt.pByteStream			 = pData; // Ptr to byte stream data decode/display event
		pkt.nDataLength			 = size; // Data length for this packet
		pkt.llPTS				 = 0; // Presentation Time Stamp for this packet (clock rate specified at initialization)
		pkt.bEOS				 = !demuxerSuccess; // true if this is an End-Of-Stream packet (flush everything)
		pkt.bPTSValid			 = false; // true if llPTS is valid (also used to detect frame boundaries for VC1 SP/MP)
		pkt.bDiscontinuity		 = false; // true if DecMFT is signalling a discontinuity
		pkt.bPartialParsing		 = 0; // 0: parse entire packet, 1: parse until next
		pkt.bEOP				 = false; // true if the packet in pByteStream is exactly one frame
		pkt.pbSideData			 = nullptr; // Auxiliary encryption information
		pkt.nSideDataLength		 = 0; // Auxiliary encrypton information length

		size_t	   parsedBytes	 = 0;
		const bool parserSuccess = parser->ParseByteStream(&pkt, &parsedBytes);
		if (videoLoggingEnabled())
			tcu::print("Parsed %ld bytes\n", parsedBytes);

		return !(demuxerSuccess && parserSuccess);
	};

	auto getNextFrame = [this, &videoStreamHasEnded, &processNextChunk](DecodedFrame* pFrame)
	{
		// The below call to DequeueDecodedPicture allows returning the next frame without parsing of the stream.
		// Parsing is only done when there are no more frames in the queue.
		int32_t framesInQueue = m_decoder->GetVideoFrameBuffer()->DequeueDecodedPicture(pFrame);

		// Loop until a frame (or more) is parsed and added to the queue.
		while ((framesInQueue == 0) && !videoStreamHasEnded)
		{

			videoStreamHasEnded = processNextChunk();

			framesInQueue		= m_decoder->GetVideoFrameBuffer()->DequeueDecodedPicture(pFrame);
		}

		if ((framesInQueue == 0) && videoStreamHasEnded)
		{
			return -1;
		}

		return framesInQueue;
	};

	auto onFrame = [this, &frameData, &frameDataIdx, &getNextFrame](const DecodedFrame** outFrame)
	{
		auto&		  vk				= m_deviceContext.getDeviceDriver();
		auto		  device			= m_deviceContext.device;
		DecodedFrame& frame				= frameData[frameDataIdx];
		DecodedFrame* pLastDecodedFrame = &frame;

		// Make sure the frame complete fence signaled (video frame is processed) before returning the frame.
		if (pLastDecodedFrame->frameCompleteFence != VkFence())
		{
			VkResult result = vk.waitForFences(device, 1, &pLastDecodedFrame->frameCompleteFence, true, 100 * 1000 * 1000 /* 100 mSec */);
			assert(result == VK_SUCCESS);
			if (result != VK_SUCCESS)
			{
				tcu::print("\nERROR: WaitForFences() result: 0x%x\n", result);
			}
			result = vk.getFenceStatus(device, pLastDecodedFrame->frameCompleteFence);
			assert(result == VK_SUCCESS);
			if (result != VK_SUCCESS)
			{
				tcu::print("\nERROR: GetFenceStatus() result: 0x%x\n", result);
			}
		}

		m_decoder->ReleaseDisplayedFrame(pLastDecodedFrame);
		pLastDecodedFrame->Reset();

		int framesRemaining = getNextFrame(pLastDecodedFrame);

		if (pLastDecodedFrame)
		{
			if (videoLoggingEnabled())
				std::cout << "<= Wait on picIdx: " << pLastDecodedFrame->pictureIndex
						  << "\t\tdisplayWidth: " << pLastDecodedFrame->displayWidth
						  << "\t\tdisplayHeight: " << pLastDecodedFrame->displayHeight
						  << "\t\tdisplayOrder: " << pLastDecodedFrame->displayOrder
						  << "\tdecodeOrder: " << pLastDecodedFrame->decodeOrder
						  << "\ttimestamp " << pLastDecodedFrame->timestamp
						  << "\tdstImageView " << (pLastDecodedFrame->outputImageView ? pLastDecodedFrame->outputImageView->GetImageResource()->GetImage() : VkImage())
						  << std::endl;
			if (outFrame)
				*outFrame = pLastDecodedFrame;
		}
		frameDataIdx = (frameDataIdx + 1) % frameData.size();
		return framesRemaining;
	};

#if 1
#ifdef _WIN32
	FILE* output = fopen("C:\\Users\\Igalia\\cts-raw.yv12", "wb");
#else
	FILE* output = fopen("/tmp/cts-raw.yv12", "wb");
#endif
#endif

	int	  framesRemaining = 0;
	std::vector<int> incorrectFrames;
	std::vector<int> correctFrames;

	do
	{
		const DecodedFrame* pOutFrame = nullptr;
		framesRemaining				  = onFrame(&pOutFrame);
		if (framesRemaining > 0 && pOutFrame)
		{
			const VkExtent2D	imageExtent{(deUint32)pOutFrame->displayWidth, (deUint32)pOutFrame->displayHeight};
			const VkImage		image		= pOutFrame->outputImageView->GetImageResource()->GetImage();
			const VkFormat		format		= pOutFrame->outputImageView->GetImageResource()->GetImageCreateInfo().format;
			const VkImageLayout layout		= VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
			auto				resultImage = getDecodedImage(m_deviceContext.getDeviceDriver(),
												  m_deviceContext.device,
												  m_deviceContext.allocator(),
												  image,
												  layout,
												  format,
												  imageExtent,
												  pOutFrame->frameCompleteSemaphore,
												  m_deviceContext.transferQueueFamilyIdx(),
												  m_deviceContext.decodeQueueFamilyIdx());

#if 1
			auto bytes = semiplanarToYV12(*resultImage);
			fwrite(bytes.data(), 1, bytes.size(), output);
#endif
			if (checksumFrame(*resultImage, frameReferenceChecksum(m_testDefinition.testType, frameNumber))) {
				framesCorrect++;
				correctFrames.push_back(frameNumber);
			} else {
				incorrectFrames.push_back(frameNumber);
			}
			frameNumber++;
			if (frameNumber == m_testDefinition.framesToCheck) {
				framesRemaining = 0;
				videoStreamHasEnded = true;
			}
		}
	}
	while (framesRemaining > 0 || !videoStreamHasEnded);

#if 1
	fclose(output);
#endif
	if (framesCorrect > 0 && framesCorrect == frameNumber)
		return tcu::TestStatus::pass(de::toString(framesCorrect) + " correctly decoded frames");
	else {
		stringstream ss;
		ss << framesCorrect << " out of " << frameNumber << " frames rendered correctly (";
		if (correctFrames.size() < incorrectFrames.size()) {
			ss << "correct frames: ";
			for (int i : correctFrames) ss << i << " ";
		} else {
			ss << "incorrect frames: ";
			for (int i : incorrectFrames) ss << i << " ";
		}
		ss << ")";
		return tcu::TestStatus::fail(ss.str());
	}
}

#endif // #ifdef DE_BUILD_VIDEO

class VideoDecodeTestCase : public vkt::TestCase
{
public:
	VideoDecodeTestCase(tcu::TestContext& context, const char* name, const char* desc, TestDefinition testDefinition)
		: vkt::TestCase(context, name, desc), m_testDefinition(testDefinition)
	{
	}

	TestInstance* createInstance(Context& context) const override;
	void		  checkSupport(Context& context) const override;

private:
	TestDefinition m_testDefinition;
};

void VideoDecodeTestCase::checkSupport (Context& context) const
{
	context.requireDeviceFunctionality("VK_KHR_video_queue");
	context.requireDeviceFunctionality("VK_KHR_synchronization2");

	switch (m_testDefinition.testType)
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
	switch (m_testDefinition.testType)
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
			return new VideoDecodeTestInstance(context, m_testDefinition);
#endif
		}
		case TEST_TYPE_H264_DECODE_INTERLEAVED:
		case TEST_TYPE_H264_BOTH_DECODE_ENCODE_INTERLEAVED:
		case TEST_TYPE_H264_H265_DECODE_INTERLEAVED:
		{
#ifdef DE_BUILD_VIDEO
			TCU_THROW(NotSupportedError, "These tests need to be reimplemented");
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

	for (const auto& tc : DecodeTestCases)
	{
		group->addChild(new VideoDecodeTestCase(testCtx, getTestName(tc.testType), "", tc));
	}

	return group.release();
}

}	// video
}	// vkt
