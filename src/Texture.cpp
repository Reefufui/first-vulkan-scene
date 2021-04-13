// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#include "vk_utils.h"
#include "Texture.hpp"

#include <stb_image.h>
#include <iostream>
#include <cassert>
#include <cstring>

void Texture::loadFromPNG(const char* a_filename)
{
    int width{};
    int height{};
    int texChannels{};

    stbi_uc* pixels{};
    pixels = stbi_load(a_filename, &width, &height, &texChannels, STBI_rgb_alpha);

    if (!pixels)
    {
        throw std::runtime_error(std::string("Could not load texture: ") + std::string(a_filename));
    }

    m_size = width * height * 4;
    m_extent = VkExtent3D{uint32_t(width), uint32_t(height), 1};
    m_width = width;
    m_height = height;

    rgba = new unsigned char[m_size];
    memcpy(rgba, pixels, m_size);

    stbi_image_free(pixels);
}

void Texture::create(VkDevice a_device, VkPhysicalDevice a_physDevice, int a_usage, VkFormat a_format)
{
    m_device = a_device;

    VkImageCreateInfo imgCreateInfo{};
    imgCreateInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCreateInfo.pNext         = nullptr;
    imgCreateInfo.flags         = 0;
    imgCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgCreateInfo.format        = a_format;
    imgCreateInfo.extent        = m_extent;
    imgCreateInfo.mipLevels     = 1;
    imgCreateInfo.arrayLayers   = 1;
    imgCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCreateInfo.usage         = (VkImageUsageFlags)a_usage;
    imgCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK_RESULT(vkCreateImage(a_device, &imgCreateInfo, nullptr, &m_imageGPU));

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(a_device, m_imageGPU, &memoryRequirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize  = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, a_physDevice);
    VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, &m_imageMemoryGPU));
    VK_CHECK_RESULT(vkBindImageMemory(a_device, m_imageGPU, m_imageMemoryGPU, 0));

    if (a_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        VkSamplerCreateInfo samplerInfo = {};
        {
            samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.pNext        = nullptr;
            samplerInfo.flags        = 0;
            samplerInfo.magFilter    = VK_FILTER_LINEAR;
            samplerInfo.minFilter    = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = m_addressMode;
            samplerInfo.addressModeV = m_addressMode;
            samplerInfo.addressModeW = m_addressMode;
            samplerInfo.mipLodBias   = 0.0f;
            samplerInfo.compareOp    = VK_COMPARE_OP_NEVER;
            samplerInfo.minLod           = 0;
            samplerInfo.maxLod           = 0;
            samplerInfo.maxAnisotropy    = 1.0;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        }

        VK_CHECK_RESULT(vkCreateSampler(a_device, &samplerInfo, nullptr, &m_imageSampler));
    }

    m_aspect = (a_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo imageViewInfo = {};
    {
        imageViewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format     = a_format;
        imageViewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        imageViewInfo.subresourceRange.aspectMask     = m_aspect;
        imageViewInfo.subresourceRange.baseMipLevel   = 0;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount     = 1;
        imageViewInfo.subresourceRange.levelCount     = 1;
        imageViewInfo.image = m_imageGPU;
    }

    VK_CHECK_RESULT(vkCreateImageView(a_device, &imageViewInfo, nullptr, &m_imageView));
}

VkImageMemoryBarrier Texture::makeBarrier(VkImageSubresourceRange a_range, VkAccessFlags a_src, VkAccessFlags a_dst,
        VkImageLayout a_before, VkImageLayout a_after)
{
    VkImageMemoryBarrier moveToGeneralBar{};
    moveToGeneralBar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    moveToGeneralBar.pNext               = nullptr;
    moveToGeneralBar.srcAccessMask       = a_src;
    moveToGeneralBar.dstAccessMask       = a_dst;
    moveToGeneralBar.oldLayout           = a_before;
    moveToGeneralBar.newLayout           = a_after;
    moveToGeneralBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    moveToGeneralBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    moveToGeneralBar.image               = m_imageGPU;
    moveToGeneralBar.subresourceRange    = a_range;
    return moveToGeneralBar;
}

VkImageSubresourceRange Texture::wholeImageRange()
{
    VkImageSubresourceRange rangeWholeImage{};
    rangeWholeImage.aspectMask     = m_aspect;
    rangeWholeImage.baseMipLevel   = 0;
    rangeWholeImage.levelCount     = 1;
    rangeWholeImage.baseArrayLayer = 0;
    rangeWholeImage.layerCount     = 1;
    return rangeWholeImage;
}

VkImageSubresourceRange CubeTexture::wholeImageRange()
{
    VkImageSubresourceRange rangeWholeImage{};
    rangeWholeImage.aspectMask     = m_aspect;
    rangeWholeImage.baseMipLevel   = 0;
    rangeWholeImage.levelCount     = 1;
    rangeWholeImage.baseArrayLayer = 0;
    rangeWholeImage.layerCount     = 6;
    return rangeWholeImage;
}

VkImageSubresourceRange CubeTexture::oneFaceRange(uint32_t a_face)
{
    VkImageSubresourceRange rangeWholeImage{};
    rangeWholeImage.aspectMask     = m_aspect;
    rangeWholeImage.baseMipLevel   = 0;
    rangeWholeImage.levelCount     = 1;
    rangeWholeImage.baseArrayLayer = a_face;
    rangeWholeImage.layerCount     = 1;
    return rangeWholeImage;
}

void Texture::changeImageLayout(VkCommandBuffer& a_cmdBuff, VkImageMemoryBarrier& a_imBar, VkPipelineStageFlags a_srcStage, VkPipelineStageFlags a_dstStage)
{
    vkCmdPipelineBarrier(a_cmdBuff,
            a_srcStage,
            a_dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &a_imBar);
}

void Texture::copyBufferToTexture(VkCommandBuffer& a_cmdBuff, VkBuffer a_cpuBuffer)
{
    VkImageSubresourceLayers shittylayers{};
    shittylayers.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    shittylayers.mipLevel       = 0;
    shittylayers.baseArrayLayer = 0;
    shittylayers.layerCount     = 1;

    VkBufferImageCopy wholeRegion = {};
    wholeRegion.bufferOffset      = 0;
    wholeRegion.bufferRowLength   = m_width;
    wholeRegion.bufferImageHeight = m_height;
    wholeRegion.imageExtent       = m_extent;
    wholeRegion.imageOffset       = VkOffset3D{};
    wholeRegion.imageSubresource  = shittylayers;

    vkCmdCopyBufferToImage(a_cmdBuff, a_cpuBuffer, m_imageGPU, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &wholeRegion);
}

void CubeTexture::copyImageToCubeface(VkCommandBuffer& a_cmdBuff, VkImage a_image, uint32_t a_face)
{
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, a_face, 1 };
    copyRegion.srcOffset      = VkOffset3D{};
    copyRegion.dstOffset      = VkOffset3D{};
    copyRegion.extent         = m_extent;

    vkCmdCopyImage(a_cmdBuff, a_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_imageGPU, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
}

void Texture::cleanup()
{
    vkFreeMemory      (m_device, m_imageMemoryGPU, NULL);
    vkDestroyImage    (m_device, m_imageGPU,        NULL);
    vkDestroyImageView(m_device, m_imageView,       NULL);
    vkDestroySampler  (m_device, m_imageSampler,    NULL);

    if (rgba)
    {
        free(rgba);
    }
}

void CubeTexture::create(VkDevice a_device, VkPhysicalDevice a_physDevice, int a_usage, VkFormat a_format)
{
    m_device = a_device;

    VkImageCreateInfo imgCreateInfo{};
    imgCreateInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCreateInfo.pNext         = nullptr;
    imgCreateInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imgCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgCreateInfo.format        = a_format;
    imgCreateInfo.extent        = m_extent;
    imgCreateInfo.mipLevels     = 1;
    imgCreateInfo.arrayLayers   = 6;
    imgCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCreateInfo.usage         = (VkImageUsageFlags)a_usage;
    imgCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK_RESULT(vkCreateImage(a_device, &imgCreateInfo, nullptr, &m_imageGPU));

    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(a_device, m_imageGPU, &memoryRequirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize  = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, a_physDevice);
    VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, &m_imageMemoryGPU));
    VK_CHECK_RESULT(vkBindImageMemory(a_device, m_imageGPU, m_imageMemoryGPU, 0));

    if (a_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        VkSamplerCreateInfo samplerInfo = {};
        {
            samplerInfo.sType          = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.mipmapMode     = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV   = samplerInfo.addressModeU;
            samplerInfo.addressModeW   = samplerInfo.addressModeU;
            samplerInfo.mipLodBias     = 0.0f;
            samplerInfo.maxAnisotropy  = 1.0f;
            samplerInfo.compareOp      = VK_COMPARE_OP_NEVER;
            samplerInfo.minLod         = 0.0f;
            samplerInfo.maxLod         = 1.0f;
            samplerInfo.borderColor    = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        }

        VK_CHECK_RESULT(vkCreateSampler(a_device, &samplerInfo, nullptr, &m_imageSampler));
    }

    m_aspect = (a_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo imageViewInfo = {};
    {
        imageViewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.viewType   = VK_IMAGE_VIEW_TYPE_CUBE;
        imageViewInfo.format     = a_format;
        imageViewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        imageViewInfo.subresourceRange.aspectMask     = m_aspect;
        imageViewInfo.subresourceRange.baseMipLevel   = 0;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount     = 6;
        imageViewInfo.subresourceRange.levelCount     = 1;
        imageViewInfo.image = m_imageGPU;
    }

    VK_CHECK_RESULT(vkCreateImageView(a_device, &imageViewInfo, nullptr, &m_imageView));
}

// TODO
void CubeTexture::loadFromPNG(const char* a_filename)
{
    int width{};
    int height{};
    int texChannels{};

    stbi_uc* pixels{};
    pixels = stbi_load(a_filename, &width, &height, &texChannels, STBI_rgb_alpha);

    if (!pixels)
    {
        throw std::runtime_error(std::string("Could not load texture: ") + std::string(a_filename));
    }

    m_size = width * height * 4;
    m_extent = VkExtent3D{uint32_t(width), uint32_t(height), 1};
    m_width = width;
    m_height = height;

    rgba = new unsigned char[m_size];
    memcpy(rgba, pixels, m_size);

    stbi_image_free(pixels);
}

