// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include "vk_utils.h"

class Texture
{
    protected:
        VkDeviceMemory m_imageMemoryGPU{}; 
        VkImage        m_imageGPU{};
        VkSampler      m_imageSampler{};
        VkImageView    m_imageView{};
        VkDevice       m_device{};
        VkDeviceSize   m_size{};
        VkExtent3D     m_extent{};
        uint32_t       m_height{};
        uint32_t       m_width{};
        VkImageAspectFlagBits m_aspect{};
        VkSamplerAddressMode  m_addressMode{ VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER };

    public:

        unsigned char* rgba{};

        VkDeviceMemory  getMemory()       { return m_imageMemoryGPU; }
        VkDeviceMemory* getpMemory()      { return &m_imageMemoryGPU; }
        VkImage         getImage()        { return m_imageGPU; }
        VkImage*        getpImage()       { return &m_imageGPU; }
        VkSampler       getSampler()      { return m_imageSampler; }
        VkImageView     getImageView()    { return m_imageView; }
        VkDeviceSize    getSize()         { return m_size; }
        uint32_t        getHeight()       { return m_height; }
        uint32_t        getWidth()        { return m_width; }

        void setExtent(VkExtent3D ext) { m_extent = ext; };
        void setAddressMode(VkSamplerAddressMode mode) { m_addressMode = mode; };

        VkImageMemoryBarrier    makeBarrier(VkImageSubresourceRange a_range, VkAccessFlags a_src, VkAccessFlags a_dst, VkImageLayout a_before, VkImageLayout a_after);

        virtual VkImageSubresourceRange wholeImageRange();

        virtual void loadFromPNG(const char* a_filename);
        virtual void create(VkDevice a_device, VkPhysicalDevice a_physDevice, int a_usage, VkFormat a_format);
        void         copyBufferToTexture(VkCommandBuffer& a_cmdBuff, VkBuffer a_cpuBuffer);
        void         changeImageLayout(VkCommandBuffer& a_cmdBuff, VkImageMemoryBarrier& a_imBar, VkPipelineStageFlags a_srcStage, VkPipelineStageFlags a_dstStage);
        void         cleanup();
};

class CubeTexture : public Texture
{
    public:
        VkImageSubresourceRange oneFaceRange(uint32_t a_face);
        VkImageSubresourceRange wholeImageRange();
        void loadFromPNG(const char* a_filename);
        void create(VkDevice a_device, VkPhysicalDevice a_physDevice, int a_usage, VkFormat a_format);
        void copyImageToCubeface(VkCommandBuffer& a_cmdBuff, VkImage a_image, uint32_t a_face);
};

struct InputTexture {
    Texture*         texture;
    VkDescriptorSet  descriptorSet;
};

struct InputCubeTexture {
    CubeTexture*    shadowCubemap;
    VkDescriptorSet descriptorSet;
};

#endif // TEXTURE_HPP

