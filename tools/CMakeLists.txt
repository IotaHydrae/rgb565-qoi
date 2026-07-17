# ---------------------------------------------------------------------------
# Download stb_image.h (single-header image loader)
# ---------------------------------------------------------------------------
set(STB_IMAGE_URL
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
)
set(STB_IMAGE_DEST "${CMAKE_CURRENT_BINARY_DIR}/stb_image.h")

if(NOT EXISTS "${STB_IMAGE_DEST}")
    message(STATUS "Downloading stb_image.h …")
    file(DOWNLOAD "${STB_IMAGE_URL}" "${STB_IMAGE_DEST}"
         STATUS STB_DL_STATUS
         TLS_VERIFY ON)

    list(GET STB_DL_STATUS 0 STB_DL_CODE)
    if(NOT STB_DL_CODE EQUAL 0)
        list(GET STB_DL_STATUS 1 STB_DL_MSG)
        message(FATAL_ERROR
            "Failed to download stb_image.h: ${STB_DL_MSG}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Download stb_image_write.h (single-header image writer)
#
# Enables JPEG output in qoi2img.  PNG uses native zlib writer.
# ---------------------------------------------------------------------------
set(STB_IMAGE_WRITE_URL
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
)
set(STB_IMAGE_WRITE_DEST "${CMAKE_CURRENT_BINARY_DIR}/stb_image_write.h")

if(NOT EXISTS "${STB_IMAGE_WRITE_DEST}")
    message(STATUS "Downloading stb_image_write.h …")
    file(DOWNLOAD "${STB_IMAGE_WRITE_URL}" "${STB_IMAGE_WRITE_DEST}"
         STATUS STB_DL_WRITE_STATUS
         TLS_VERIFY ON)

    list(GET STB_DL_WRITE_STATUS 0 STB_DL_WRITE_CODE)
    if(NOT STB_DL_WRITE_CODE EQUAL 0)
        list(GET STB_DL_WRITE_STATUS 1 STB_DL_WRITE_MSG)
        message(WARNING
            "Failed to download stb_image_write.h: ${STB_DL_WRITE_MSG}"
            "  → qoi2img: JPEG not available (PNG/BMP/TGA/PPM work)")
        set(STB_IMAGE_WRITE_AVAILABLE FALSE)
    else()
        set(STB_IMAGE_WRITE_AVAILABLE TRUE)
    endif()
else()
    set(STB_IMAGE_WRITE_AVAILABLE TRUE)
endif()

# ---------------------------------------------------------------------------
# qoi_utils — shared helpers
# ---------------------------------------------------------------------------
add_library(qoi_utils STATIC qoi_utils.c)
target_include_directories(qoi_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qoi_utils PRIVATE rgb565_qoi)
target_compile_features(qoi_utils PRIVATE c_std_99)

# ---------------------------------------------------------------------------
# img2qoi
# ---------------------------------------------------------------------------
add_executable(img2qoi img2qoi.c)
target_include_directories(img2qoi PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(img2qoi PRIVATE qoi_utils rgb565_qoi)
target_compile_features(img2qoi PRIVATE c_std_99)

# ---------------------------------------------------------------------------
# video2qoi
# ---------------------------------------------------------------------------
add_executable(video2qoi video2qoi.c)
target_include_directories(video2qoi PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(video2qoi PRIVATE qoi_utils rgb565_qoi)
target_compile_features(video2qoi PRIVATE c_std_99)

# ---------------------------------------------------------------------------
# qoi2img
# ---------------------------------------------------------------------------
add_executable(qoi2img qoi2img.c)
target_include_directories(qoi2img PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(qoi2img PRIVATE rgb565_qoi z)
target_compile_features(qoi2img PRIVATE c_std_99)

if(STB_IMAGE_WRITE_AVAILABLE)
    target_compile_definitions(qoi2img PRIVATE HAS_STB_IMAGE_WRITE)
    if(UNIX)
        target_link_libraries(qoi2img PRIVATE m)
    endif()
endif()
