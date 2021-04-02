#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include "vk_utils.h"

class Texture
{
    private:
        VkDeviceMemory m_imageMemoryGPU{}; 
        VkImage        m_imageGPU{};
        VkSampler      m_imageSampler{};
        VkImageView    m_imageView{};
        VkDevice       m_device{};
        VkDeviceSize   m_size{};
        VkExtent3D     m_extent{};
        uint32_t       m_height{};
        uint32_t       m_width{};

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

        Texture()
        {
        }

        void loadFromJPG(const char* a_filename);
        void create(VkDevice a_device, VkPhysicalDevice a_physDevice);
        void cleanup();
};

#endif // TEXTURE_HPP
