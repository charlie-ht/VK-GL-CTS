# cmake file for nvidia-video-samples

if (NOT DE_DEFS)
	message(FATAL_ERROR "Include Defs.cmake")
endif ()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vk_video_decoder/CMakeLists.txt")
	set(DEFAULT_NV_VIDEO_SAMPLES_SRC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src)
else ()
	message(FATAL_ERROR "nvidia-video-samples source code not found")
endif ()

set(NV_VIDEO_SAMPLES_SRC_PATH ${DEFAULT_NV_VIDEO_SAMPLES_SRC_PATH} CACHE STRING "Path to nvidia-video-samples source tree")

if (IS_ABSOLUTE ${NV_VIDEO_SAMPLES_SRC_PATH})
	set(NV_VIDEO_SAMPLES_ABS_PATH ${NV_VIDEO_SAMPLES_SRC_PATH})
else ()
	set(NV_VIDEO_SAMPLES_ABS_PATH "${CMAKE_SOURCE_DIR}/${NV_VIDEO_SAMPLES_SRC_PATH}")
endif ()

if (EXISTS "${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/NvVideoParser/CMakeLists.txt")
	message(STATUS "nvidia-video-samples parser found")

	set(NV_CODECUTILS_SOURCE "${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/VkCodecUtils")
	set(NV_COMMON_SOURCE "${NV_VIDEO_SAMPLES_ABS_PATH}/common/src")

	set(VK_VIDEO_PARSER_SOURCE "${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/VkVideoParser")
	set(VK_VIDEO_DECODER_SOURCE "${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/VkVideoDecoder")
	set(NV_VIDEO_PARSER_SOURCE "${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/NvVideoParser")
	set(NV_FRAMEBUFFER_SOURCE "${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/VulkanVideoFrameBuffer")

	set(SAMPLE_COMMON_INCLUDE "${NV_VIDEO_SAMPLES_ABS_PATH}/common/include")
	set(SAMPLE_DECODER_INCLUDE "${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/include")

	set (PARSER_SOURCES
		${SAMPLE_COMMON_INCLUDE}/VkVideoCore/VkVideoRefCountBase.h
		${NV_COMMON_SOURCE}/VkCodecUtils/nvVkFormats.cpp

		${NV_CODECUTILS_SOURCE}/VulkanDeviceMemoryImpl.cpp

		${SAMPLE_DECODER_INCLUDE}/NvVideoParser/nvVulkanVideoUtils.h
		${SAMPLE_DECODER_INCLUDE}/vkvideo_parser/VulkanVideoParser.h
		${SAMPLE_DECODER_INCLUDE}/vkvideo_parser/VulkanVideoParserParams.h
		${SAMPLE_DECODER_INCLUDE}/vkvideo_parser/PictureBufferBase.h
		${SAMPLE_DECODER_INCLUDE}/vkvideo_parser/VulkanVideoParserIf.h

		${NV_VIDEO_PARSER_SOURCE}/include/VulkanH264Decoder.h
		${NV_VIDEO_PARSER_SOURCE}/include/VulkanH265Decoder.h
		${NV_VIDEO_PARSER_SOURCE}/include/VulkanH26xDecoder.h
		${NV_VIDEO_PARSER_SOURCE}/include/VulkanVideoDecoder.h
		${NV_VIDEO_PARSER_SOURCE}/src/VulkanH264Parser.cpp
		${NV_VIDEO_PARSER_SOURCE}/src/VulkanH265Parser.cpp
		${NV_VIDEO_PARSER_SOURCE}/src/VulkanVideoDecoder.cpp
		${NV_VIDEO_PARSER_SOURCE}/src/nvVulkanh264ScalingList.cpp
	)

	set(CMAKE_C_FLAGS ${DE_3RD_PARTY_C_FLAGS})
	set(CMAKE_CXX_FLAGS ${DE_3RD_PARTY_CXX_FLAGS})

	add_definitions(-DCTS_BUILD)

	set(PARSER_INCLUDE_DIRS
		"${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/include"
		"${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/include/vkvideo_parser"
		"${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/include/NvVideoParser"
		"${NV_VIDEO_SAMPLES_ABS_PATH}/common/include"
		"${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/NvVideoParser/include"
		"${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs/VulkanVideoFrameBuffer"
		"${NV_FRAMEBUFFER_SOURCE}/include"
		"${NV_VIDEO_SAMPLES_ABS_PATH}/vk_video_decoder/libs"
		"${VK_VIDEO_DECODER_SOURCE}"
		)

	# target_include_directories(nvvideoparser BEFORE PUBLIC ${PARSER_INCLUDE_DIRS})

	set(NV_VIDEO_PARSER_INCLUDE_DIRS ${PARSER_INCLUDE_DIRS} PARENT_SCOPE)
	set(NV_VIDEO_PARSER_SOURCES ${PARSER_SOURCES} PARENT_SCOPE)
	# set(NV_VIDEO_PARSER_LIBRARY nvvideoparser PARENT_SCOPE)
else ()
	message(FATAL_ERROR "nvidia-video-samples not found")
endif ()
