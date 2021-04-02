#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/scalar_constants.hpp> // glm::pi

#include <vulkan/vulkan.h>

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <unordered_map>

#include "vk_utils.h"
#include "Mesh.hpp"
#include "Texture.hpp"
#include "Timer.hpp"

const int WIDTH  = 800;
const int HEIGHT = 600;

const int MAX_FRAMES_IN_FLIGHT = 3;

const std::vector<const char*> deviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

static Timer timer{};

class Application 
{
    private:
        GLFWwindow * m_window;

        VkInstance m_instance;
        std::vector<const char*> m_enabledLayers;

        VkDebugUtilsMessengerEXT m_debugMessenger;
        VkSurfaceKHR m_surface;

        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device;

        VkQueue m_graphicsQueue;
        VkQueue m_presentQueue;

        vk_utils::ScreenBufferResources m_screen;

        VkRenderPass     m_renderPass;

        VkCommandPool                m_commandPool;
        std::vector<VkCommandBuffer> m_commandBuffers;

        VkImage        m_depthImage;
        VkDeviceMemory m_depthImageMemory;
        VkImageView    m_depthImageView;

        struct SyncObj
        {
            std::vector<VkSemaphore> imageAvailableSemaphores;
            std::vector<VkSemaphore> renderFinishedSemaphores;
            std::vector<VkFence>     inFlightFences;
        } m_sync;

        size_t m_currentFrame{};

        struct Pipe {
            VkPipeline                    pipeline;
            VkPipelineLayout              pipelineLayout;
            std::vector<VkDescriptorPool> descriptorPools;
            std::vector<VkDescriptorSet>  descriptorSets;
        };

        struct RenderObject {
            Mesh*     mesh;
            Pipe*     pipe;
            Texture*  texture;
            glm::mat4 transformMatrix;
        };

        std::unordered_map<std::string, RenderObject> m_renerables;
        std::unordered_map<std::string, Pipe>         m_pipes;
        std::unordered_map<std::string, Mesh>         m_meshes;
        std::unordered_map<std::string, Texture>      m_textures;

        struct UBO {
            glm::mat4 mvp;
        };

        VkDescriptorSetLayout m_sceneDSLayout;
        VkDescriptorPool      m_sceneDSPool;

        std::vector<VkBuffer>        m_uniformBuffers;
        std::vector<VkDeviceMemory>  m_uniformBuffersMemory;
        std::vector<VkDescriptorSet> m_sceneDS;

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallbackFn(
                VkDebugReportFlagsEXT                       flags,
                VkDebugReportObjectTypeEXT                  objectType,
                uint64_t                                    object,
                size_t                                      location,
                int32_t                                     messageCode,
                const char*                                 pLayerPrefix,
                const char*                                 pMessage,
                void*                                       pUserData)
        {
            printf("[Debug Report]: %s: %s\n", pLayerPrefix, pMessage);
            return VK_FALSE;
        }
        VkDebugReportCallbackEXT debugReportCallback;

        static void LoadMeshes(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
                std::unordered_map<std::string, Mesh>& a_meshes)
        {
            auto fillMeshBuffer = [&](VkBuffer& a_buffer, VkDeviceMemory& a_memory, void* a_src, VkBufferUsageFlags a_usage, size_t a_size)
            {
                VkBuffer stagingBuffer{};
                VkDeviceMemory stagingBufferMemory{};

                CreateHostVisibleBuffer(a_device, a_physDevice, a_size, &stagingBuffer, &stagingBufferMemory);

                void *mappedMemory = nullptr;
                vkMapMemory(a_device, stagingBufferMemory, 0, a_size, 0, &mappedMemory);
                memcpy(mappedMemory, a_src, a_size);
                vkUnmapMemory(a_device, stagingBufferMemory);

                CreateDeviceLocalBuffer(a_device, a_physDevice, a_size, &a_buffer, &a_memory, a_usage);

                SubmitStagingBuffer(a_device, a_pool, a_queue, stagingBuffer, a_buffer, a_size);

                vkFreeMemory(a_device, stagingBufferMemory, nullptr);
                vkDestroyBuffer(a_device, stagingBuffer, nullptr);
            };

            auto loadMesh = [&](std::string&& meshName)
            {
                Mesh mesh{};

                std::string fileName{ "assets/meshes/.obj" };
                fileName.insert(fileName.find("."), meshName);
                mesh.loadFromOBJ(fileName.c_str());

                fillMeshBuffer(mesh.getVBO().buffer, mesh.getVBO().memory, mesh.vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        mesh.vertices.size() * sizeof(Vertex));

                fillMeshBuffer(mesh.getIBO().buffer, mesh.getIBO().memory, mesh.indices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        mesh.indices.size() * sizeof(uint32_t));

                a_meshes[meshName] = mesh;

            };

            loadMesh("terrain");
            loadMesh("viking_room");
        }

        static void LoadTextures(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
                std::unordered_map<std::string, Texture>& a_textures)
        {
            auto fillTexture = [&](VkImage a_image, VkDeviceMemory& a_memory, void* a_src, size_t a_size, uint32_t a_w, uint32_t a_h)
            {
                VkBuffer stagingBuffer{};
                VkDeviceMemory stagingBufferMemory{};

                CreateHostVisibleBuffer(a_device, a_physDevice, a_size, &stagingBuffer, &stagingBufferMemory);

                void *mappedMemory = nullptr;
                vkMapMemory(a_device, stagingBufferMemory, 0, a_size, 0, &mappedMemory);
                memcpy(mappedMemory, a_src, a_size);
                vkUnmapMemory(a_device, stagingBufferMemory);

                CopyBufferToTexure(a_device, a_pool, a_queue, stagingBuffer, a_image, a_w, a_h);

                vkFreeMemory(a_device, stagingBufferMemory, nullptr);
                vkDestroyBuffer(a_device, stagingBuffer, nullptr);
            };

            auto loadTexture = [&](std::string&& textureName)
            {
                Texture texture{};

                std::string fileName{ "assets/textures/.jpg" };
                fileName.insert(fileName.find("."), textureName);

                texture.loadFromJPG(fileName.c_str());

                texture.create(a_device, a_physDevice);

                fillTexture(texture.getImage(), *texture.getpMemory(), (void*)texture.rgba, (size_t)texture.getSize(),
                        (uint32_t)texture.getWidth(), (uint32_t)texture.getHeight());

                a_textures[textureName] = texture;
            };

            loadTexture("terrain");
            //loadTexture("viking_room");
        }


        static void ComposeScene(std::unordered_map<std::string, RenderObject>& a_renerables, std::unordered_map<std::string, Pipe>& a_pipes,
                std::unordered_map<std::string, Mesh>& a_meshes, std::unordered_map<std::string, Texture>& a_textures)
        {
            auto createRenderable = [&](std::string&& objectName, std::string&& meshName, std::string&& pipeName)
            {
                RenderObject object{};

                {
                    auto found{ a_meshes.find(meshName) };
                    if (found == a_meshes.end())
                    {
                        throw std::runtime_error(std::string("Mesh not found: ") + objectName);
                    }
                    object.mesh = &(*found).second;
                }

                {
                    auto found = a_pipes.find(pipeName);
                    if (found == a_pipes.end())
                    {
                        throw std::runtime_error(std::string("Pipeline not found: ") + objectName);
                    }
                    object.pipe = &(*found).second;
                }

                object.transformMatrix = glm::mat4{1.f};

                a_renerables[objectName] = object;
            };

            // tag, mesh, pipeline, texture
            createRenderable("terrain", "terrain", "scene");
            createRenderable("viking_room", "viking_room", "scene");
        }

        static void UpdateScene(std::unordered_map<std::string, RenderObject>& a_renerables)
        {
            glm::mat4 model{ 1.f };

            {
                model = glm::scale(model, glm::vec3(80.f));
                model = glm::rotate(model, glm::radians((float)sin(timer.elapsed() / 50) * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));

                a_renerables["terrain"].transformMatrix = model;
                a_renerables["viking_room"].transformMatrix = model;
            }
        }

        void CreateResources()
        {
            std::cout << "\tcreating render pass...\n";
            CreateRenderPass(m_device, m_screen.swapChainImageFormat, &m_renderPass);

            std::cout << "\tcreating depth image...\n";
            CreateDepthImage(m_device, physicalDevice, m_depthImage, m_depthImageMemory, m_depthImageView);

            std::cout << "\tcreating frame buffers...\n";
            CreateScreenFrameBuffers(m_device, m_renderPass, &m_screen, m_depthImageView);

            std::cout << "\tcreating sync objects...\n";
            CreateSyncObjects(m_device, &m_sync);

            std::cout << "\tcreating uniform buffers...\n";
            CreateUniformBuffers(m_device, physicalDevice, sizeof(UBO), m_uniformBuffers, m_uniformBuffersMemory, m_screen.swapChainImageViews.size());

            std::cout << "\tloading textures...\n";
            LoadTextures(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_textures);

            std::cout << "\tcreating descriptor sets...\n";
            CreateSceneDescriptorSetLayout(m_device, &m_sceneDSLayout);
            CreateDSForEachTexture(m_device, m_uniformBuffers, sizeof(UBO), &m_sceneDSLayout, &m_sceneDSPool, m_sceneDS,
                    m_screen.swapChainImageViews.size(), m_textures);

            std::cout << "\tcreating graphics pipeline...\n";
            CreatePipelines(m_device, m_screen.swapChainExtent, m_renderPass, m_pipes, m_textures, m_sceneDSLayout);

            std::cout << "\tloading meshes...\n";
            LoadMeshes(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_meshes);

            std::cout << "\tcomposing scene...\n";
            ComposeScene(m_renerables, m_pipes, m_meshes, m_textures);

            std::cout << "\tcreating command buffers...\n";
            CreateCommandBuffers(m_device, m_commandPool, m_screen.swapChainFramebuffers, &m_commandBuffers);
        }


        void MainLoop()
        {
            while (!glfwWindowShouldClose(m_window)) 
            {
                glfwPollEvents();
                UpdateScene(m_renerables);
                DrawFrame();
            }

            vkDeviceWaitIdle(m_device);
        }

        static void CreateRenderPass(VkDevice a_device, VkFormat a_swapChainImageFormat,
                VkRenderPass* a_pRenderPass)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format         = a_swapChainImageFormat; // = VK_FORMAT_B8G8R8A8_UNORM
            colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorAttachmentRef{};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentDescription depthAttachment{};
            depthAttachment.format         = VK_FORMAT_D32_SFLOAT;
            depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = 1;
            depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = 1;
            subpass.pColorAttachments       = &colorAttachmentRef;
            subpass.pDepthStencilAttachment = &depthAttachmentRef;

            VkSubpassDependency dependency{};
            dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass    = 0;
            dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            std::vector<VkAttachmentDescription> attachments{
                colorAttachment, depthAttachment
            };

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = attachments.size();
            renderPassInfo.pAttachments    = attachments.data();
            renderPassInfo.subpassCount    = 1;
            renderPassInfo.pSubpasses      = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies   = &dependency;

            if (vkCreateRenderPass(a_device, &renderPassInfo, nullptr, a_pRenderPass) != VK_SUCCESS)
                throw std::runtime_error("[CreateRenderPass]: failed to create render pass!");
        }

        static void CreateSceneDescriptorSetLayout(VkDevice a_device, VkDescriptorSetLayout *a_pDSLayout)
        {
            VkDescriptorSetLayoutBinding uboLayoutBinding{};
            uboLayoutBinding.binding            = 0;
            uboLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboLayoutBinding.descriptorCount    = 1;
            uboLayoutBinding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;
            uboLayoutBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutBinding samplerLayoutBinding{};
            samplerLayoutBinding.binding            = 1;
            samplerLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerLayoutBinding.descriptorCount    = 1;
            samplerLayoutBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
            samplerLayoutBinding.pImmutableSamplers = nullptr;

            std::array<VkDescriptorSetLayoutBinding, 2> binds = {uboLayoutBinding, samplerLayoutBinding};

            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
            descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCreateInfo.bindingCount = binds.size();
            descriptorSetLayoutCreateInfo.pBindings    = binds.data();

            if (vkCreateDescriptorSetLayout(a_device, &descriptorSetLayoutCreateInfo, nullptr, a_pDSLayout) != VK_SUCCESS)
                throw std::runtime_error("[CreateSceneDescriptorSetLayout]: failed to create DS layout!");
        }

        static void CreateDescriptorSet(VkDevice a_device, std::vector<VkBuffer>& a_buffer, size_t a_bufferSize, const VkDescriptorSetLayout *a_pDSLayout,
                VkDescriptorPool *a_pDSPool, std::vector<VkDescriptorSet>& a_dsets, size_t a_count, VkImageView a_imageView, VkSampler a_imageSampler)
        {
            std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes{};
            descriptorPoolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorPoolSizes[0].descriptorCount = a_count;
            descriptorPoolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorPoolSizes[1].descriptorCount = a_count;

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{};
            descriptorPoolCreateInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCreateInfo.maxSets       = a_count;
            descriptorPoolCreateInfo.poolSizeCount = descriptorPoolSizes.size();
            descriptorPoolCreateInfo.pPoolSizes    = descriptorPoolSizes.data();

            if (vkCreateDescriptorPool(a_device, &descriptorPoolCreateInfo, NULL, a_pDSPool) != VK_SUCCESS)
                throw std::runtime_error("[CreateDescriptorSet]: failed to create descriptor set pool!");

            std::vector<VkDescriptorSetLayout> layouts(a_count, *a_pDSLayout);

            VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
            descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocateInfo.descriptorPool     = *a_pDSPool;
            descriptorSetAllocateInfo.descriptorSetCount = a_count;
            descriptorSetAllocateInfo.pSetLayouts        = layouts.data();

            a_dsets.resize(a_count);
            if (vkAllocateDescriptorSets(a_device, &descriptorSetAllocateInfo, a_dsets.data()) != VK_SUCCESS)
                throw std::runtime_error("[CreateDescriptorSet]: failed to allocate descriptor set pool!");

            for (size_t i{}; i < a_count; ++i)
            {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = a_buffer[i];
                bufferInfo.range = a_bufferSize;

                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = a_imageView;
                imageInfo.sampler = a_imageSampler;

                std::array<VkWriteDescriptorSet, 2> descrWrite{};
                descrWrite[0].sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descrWrite[0].dstSet            = a_dsets[i];
                descrWrite[0].dstBinding        = 0;
                descrWrite[0].dstArrayElement   = 0;
                descrWrite[0].descriptorType    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descrWrite[0].descriptorCount   = 1;
                descrWrite[0].pBufferInfo       = &bufferInfo;

                descrWrite[1].sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descrWrite[1].dstSet            = a_dsets[i];
                descrWrite[1].dstBinding        = 1;
                descrWrite[1].dstArrayElement   = 0;
                descrWrite[1].descriptorType    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descrWrite[1].descriptorCount   = 1;
                descrWrite[1].pImageInfo        = &imageInfo;

                vkUpdateDescriptorSets(a_device, descrWrite.size(), descrWrite.data(), 0, nullptr);
            }
        }

        static void CreateDSForEachTexture(VkDevice a_device, std::vector<VkBuffer>& a_buffer, size_t a_bufferSize, const VkDescriptorSetLayout *a_pDSLayout,
                VkDescriptorPool *a_pDSPool, std::vector<VkDescriptorSet>& a_dsets, size_t a_count, std::unordered_map<std::string, Texture>& a_textures)
        {
            for (auto& texture : a_textures)
            {
                std::string textureName{ texture.first };
                Texture     textureObj{ texture.second };
                CreateDescriptorSet(a_device, a_buffer, a_bufferSize, a_pDSLayout, a_pDSPool, a_dsets, a_count,
                        textureObj.getImageView(), textureObj.getSampler());
            }
        }

        static void CreateGraphicsPipeline(VkDevice a_device, VkExtent2D a_screenExtent, VkRenderPass a_renderPass,
                std::unordered_map<std::string, Pipe>& a_pipes, VkDescriptorSetLayout a_dsLayout)
        {
            auto vertShaderCode = vk_utils::ReadFile("shaders/scene.vert.spv");
            auto fragShaderCode = vk_utils::ReadFile("shaders/scene.frag.spv");

            VkShaderModule vertShaderModule = vk_utils::CreateShaderModule(a_device, vertShaderCode);
            VkShaderModule fragShaderModule = vk_utils::CreateShaderModule(a_device, fragShaderCode);

            VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
            vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = vertShaderModule;
            vertShaderStageInfo.pName  = "main";

            VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
            fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName  = "main";

            std::vector<VkPipelineShaderStageCreateInfo> shaderStages {
                vertShaderStageInfo, fragShaderStageInfo
            };

            VertexInputDescription vertexDescr{ Vertex::getVertexDescription() };

            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount   = vertexDescr.bindings.size();
            vertexInputInfo.vertexAttributeDescriptionCount = vertexDescr.attributes.size();
            vertexInputInfo.pVertexBindingDescriptions      = vertexDescr.bindings.data();
            vertexInputInfo.pVertexAttributeDescriptions    = vertexDescr.attributes.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            VkViewport viewport{};
            viewport.x        = 0.0f;
            viewport.y        = 0.0f;
            viewport.width    = (float)a_screenExtent.width;
            viewport.height   = (float)a_screenExtent.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.extent = a_screenExtent;

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports    = &viewport;
            viewportState.scissorCount  = 1;
            viewportState.pScissors     = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable        = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth               = 1.0f;
            rasterizer.cullMode                = VK_CULL_MODE_NONE;
            rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable         = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable  = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthAndStencil{};
            depthAndStencil.sType              = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthAndStencil.depthTestEnable    = VK_TRUE;
            depthAndStencil.depthWriteEnable   = VK_TRUE;
            depthAndStencil.depthCompareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
            depthAndStencil.depthBoundsTestEnable = VK_FALSE;
            depthAndStencil.stencilTestEnable  = VK_FALSE;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable    = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable     = VK_FALSE;
            colorBlending.attachmentCount   = 1;
            colorBlending.pAttachments      = &colorBlendAttachment;
            colorBlending.blendConstants[0] = 0.0f;
            colorBlending.blendConstants[1] = 0.0f;
            colorBlending.blendConstants[2] = 0.0f;
            colorBlending.blendConstants[3] = 0.0f;

            std::vector<VkPushConstantRange> pushConstants{
                { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants) }
            };

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount         = 1;
            pipelineLayoutInfo.pSetLayouts            = &a_dsLayout;
            pipelineLayoutInfo.pushConstantRangeCount = pushConstants.size();
            pipelineLayoutInfo.pPushConstantRanges    = pushConstants.data();

            VkPipelineLayout pipelineLayout{};
            if (vkCreatePipelineLayout(a_device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
                throw std::runtime_error("[CreateGraphicsPipeline]: failed to create pipeline layout!");

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount          = shaderStages.size();
            pipelineInfo.pStages             = shaderStages.data();
            pipelineInfo.pVertexInputState   = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState      = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState   = &multisampling;
            pipelineInfo.pDepthStencilState  = &depthAndStencil;
            pipelineInfo.pColorBlendState    = &colorBlending;
            pipelineInfo.layout              = pipelineLayout;
            pipelineInfo.renderPass          = a_renderPass;
            pipelineInfo.subpass             = 0;
            pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;

            VkPipeline pipeline{};
            if (vkCreateGraphicsPipelines(a_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
                throw std::runtime_error("[CreateGraphicsPipeline]: failed to create graphics pipeline!");

            vkDestroyShaderModule(a_device, fragShaderModule, nullptr);
            vkDestroyShaderModule(a_device, vertShaderModule, nullptr);

            a_pipes["scene"] = Pipe{ pipeline, pipelineLayout };
        }

        static void CreatePipelines(VkDevice a_device, VkExtent2D a_screenExtent, VkRenderPass a_renderPass,
                std::unordered_map<std::string, Pipe>& a_pipes, std::unordered_map<std::string, Texture>& a_textures,
                VkDescriptorSetLayout a_dsLayout)
        {
            CreateGraphicsPipeline(a_device, a_screenExtent, a_renderPass, a_pipes, a_dsLayout);
        }

        static void CreateScreenFrameBuffers(VkDevice a_device, VkRenderPass a_renderPass, vk_utils::ScreenBufferResources* pScreen,
                VkImageView a_depthImageView)
        {
            pScreen->swapChainFramebuffers.resize(pScreen->swapChainImageViews.size());

            for (size_t i = 0; i < pScreen->swapChainImageViews.size(); i++) 
            {
                std::vector<VkImageView> attachments{
                    pScreen->swapChainImageViews[i],
                    a_depthImageView
                };

                VkFramebufferCreateInfo framebufferInfo = {};
                framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass      = a_renderPass;
                framebufferInfo.attachmentCount = attachments.size();
                framebufferInfo.pAttachments    = attachments.data();
                framebufferInfo.width           = pScreen->swapChainExtent.width;
                framebufferInfo.height          = pScreen->swapChainExtent.height;
                framebufferInfo.layers          = 1;

                if (vkCreateFramebuffer(a_device, &framebufferInfo, nullptr, &pScreen->swapChainFramebuffers[i]) != VK_SUCCESS)
                    throw std::runtime_error("failed to create framebuffer!");
            }
        }

        static glm::mat4 camera()
        {
            glm::vec3 cameraPos{ glm::vec3(300.0f, 300.0f, 300.0f) };

            glm::mat4 view = glm::lookAt(
                    cameraPos,                      //eye (cam position)
                    glm::vec3(0.f),                 //center (where we are looking)
                    glm::vec3(0.f, 1.f, 0.f)        //up (worlds upwards direction)
                    );

            glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)WIDTH / (float)HEIGHT, 0.1f, 700.0f);
            projection[1][1] *= -1; // vulkan coordinate space

            return projection * view;
        }

        static void UpdateUniformBuffer(VkDevice a_device, VkBuffer& a_ubo, VkDeviceMemory& a_uboMem, glm::mat4 a_modelMatrix)
        {
            UBO ubo{};
            ubo.mvp = camera() * a_modelMatrix;

            void* mappedMemory{nullptr};
            vkMapMemory(a_device, a_uboMem, 0, sizeof(UBO), 0, &mappedMemory);
            memcpy(mappedMemory, &ubo, sizeof(UBO));
            vkUnmapMemory(a_device, a_uboMem);
        }

        static void RecordCommandBuffer(VkDevice a_device, VkFramebuffer a_swapChainFramebuffer, VkExtent2D a_frameBufferExtent, VkRenderPass a_renderPass,
                const std::unordered_map<std::string, RenderObject>& a_objects, VkBuffer& a_ubo, VkDeviceMemory& a_uboMem, VkCommandBuffer a_cmdBuffer,
                VkDescriptorSet a_descrSet, size_t a_frameID) 
        {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            if (vkBeginCommandBuffer(a_cmdBuffer, &beginInfo) != VK_SUCCESS) 
                throw std::runtime_error("[CreateCommandPoolAndBuffers]: failed to begin recording command buffer!");

            VkClearValue colorClear;
            //colorClear.color = { {  1.0f, 0.7f, 0.6f, 1.0f } };
            colorClear.color = { {  0.0f, 0.0f, 0.0f, 0.0f } };

            VkClearValue depthClear;
            depthClear.depthStencil.depth = 1.f;

            std::vector<VkClearValue> clearValues{ colorClear, depthClear };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = a_renderPass;
            renderPassInfo.framebuffer       = a_swapChainFramebuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = a_frameBufferExtent;
            renderPassInfo.clearValueCount  = clearValues.size();
            renderPassInfo.pClearValues     = clearValues.data();

            vkCmdBeginRenderPass(a_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            Mesh* previousMesh{nullptr};
            Pipe* previousPipe{nullptr};

            for (auto& object : a_objects)
            {
                auto& obj{ object.second };

                if (obj.pipe != previousPipe)
                {
                    vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.pipe->pipeline);
                    vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.pipe->pipelineLayout, 0, 1, &a_descrSet, 0, nullptr);
                    //vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.pipe->pipelineLayout, 0, 1,
                    //&(obj.pipe->descriptorSets[a_frameID]), 0, nullptr);
                    previousPipe = obj.pipe;
                }

                MeshPushConstants constants{};
                constants.mvp = camera() * obj.transformMatrix;

                UpdateUniformBuffer(a_device, a_ubo, a_uboMem, obj.transformMatrix);

                vkCmdPushConstants(a_cmdBuffer, obj.pipe->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

                if (obj.mesh != previousMesh)
                {
                    std::vector<VkBuffer> vertexBuffers{ obj.mesh->getVBO().buffer };
                    VkBuffer indexBuffers{ obj.mesh->getIBO().buffer };
                    std::vector<VkDeviceSize> offsets{ 0 };

                    vkCmdBindVertexBuffers(a_cmdBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());
                    vkCmdBindIndexBuffer(a_cmdBuffer, indexBuffers, 0, VK_INDEX_TYPE_UINT32);
                }

                vkCmdDrawIndexed(a_cmdBuffer, obj.mesh->indices.size(), 1, 0, 0, 0);
            }

            vkCmdEndRenderPass(a_cmdBuffer);

            if (vkEndCommandBuffer(a_cmdBuffer) != VK_SUCCESS) {
                throw std::runtime_error("failed to record command buffer!");
            }
        }

        static void CreateCommandBuffers(VkDevice a_device, VkCommandPool a_cmdPool, std::vector<VkFramebuffer> a_swapChainFramebuffers,
                std::vector<VkCommandBuffer>* a_cmdBuffers) 
        {
            std::vector<VkCommandBuffer>& commandBuffers = (*a_cmdBuffers);

            commandBuffers.resize(a_swapChainFramebuffers.size());

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = a_cmdPool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

            if (vkAllocateCommandBuffers(a_device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
                throw std::runtime_error("[CreateCommandPoolAndBuffers]: failed to allocate command buffers!");
        }

        static void CreateSyncObjects(VkDevice a_device, SyncObj* a_pSyncObjs)
        {
            a_pSyncObjs->imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            a_pSyncObjs->renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            a_pSyncObjs->inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
            {
                if (vkCreateSemaphore(a_device, &semaphoreInfo, nullptr, &a_pSyncObjs->imageAvailableSemaphores[i]) != VK_SUCCESS ||
                        vkCreateSemaphore(a_device, &semaphoreInfo, nullptr, &a_pSyncObjs->renderFinishedSemaphores[i]) != VK_SUCCESS ||
                        vkCreateFence    (a_device, &fenceInfo,     nullptr, &a_pSyncObjs->inFlightFences[i]) != VK_SUCCESS) {
                    throw std::runtime_error("[CreateSyncObjects]: failed to create synchronization objects for a frame!");
                }
            }
        }

        static void CreateDepthImage(VkDevice a_device, VkPhysicalDevice a_physDevice, VkImage& a_image, VkDeviceMemory& a_imageMemory, VkImageView& a_imageView)
        {
            VkImageCreateInfo imgCreateInfo{};
            imgCreateInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imgCreateInfo.pNext         = nullptr;
            imgCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
            imgCreateInfo.format        = VK_FORMAT_D32_SFLOAT; //TODO: VK_FORMAT_D32_SFLOAT
            imgCreateInfo.extent        = VkExtent3D{uint32_t(WIDTH), uint32_t(HEIGHT), 1};
            imgCreateInfo.mipLevels     = 1;
            imgCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imgCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imgCreateInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imgCreateInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imgCreateInfo.arrayLayers   = 1;
            VK_CHECK_RESULT(vkCreateImage(a_device, &imgCreateInfo, nullptr, &a_image));

            VkMemoryRequirements memoryRequirements{};
            vkGetImageMemoryRequirements(a_device, a_image, &memoryRequirements);

            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize  = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, a_physDevice);
            VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, &a_imageMemory));
            VK_CHECK_RESULT(vkBindImageMemory(a_device, a_image, a_imageMemory, 0));

            VkImageViewCreateInfo imageViewInfo = {};
            {
                imageViewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                imageViewInfo.viewType   = VK_IMAGE_VIEW_TYPE_2D;
                imageViewInfo.format     = VK_FORMAT_D32_SFLOAT;
                imageViewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
                imageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
                imageViewInfo.subresourceRange.baseMipLevel   = 0;
                imageViewInfo.subresourceRange.baseArrayLayer = 0;
                imageViewInfo.subresourceRange.layerCount     = 1;
                imageViewInfo.subresourceRange.levelCount     = 1;
                imageViewInfo.image = a_image;
            }
            VK_CHECK_RESULT(vkCreateImageView(a_device, &imageViewInfo, nullptr, &a_imageView));
        }

        static void CreateUniformBuffers(VkDevice a_device, VkPhysicalDevice a_physDevice, const size_t a_bufferSize, std::vector<VkBuffer>& a_ubos,
                std::vector<VkDeviceMemory>& a_ubosMemory, size_t a_count)
        {
            a_ubos.resize(a_count);
            a_ubosMemory.resize(a_count);

            for (size_t i{}; i < a_count; ++i)
            {
                CreateHostVisibleBuffer(a_device, a_physDevice, a_bufferSize, &(a_ubos[i]), &(a_ubosMemory[i]), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            }
        }

        static void CreateHostVisibleBuffer(VkDevice a_device, VkPhysicalDevice a_physDevice, const size_t a_bufferSize,
                VkBuffer *a_pBuffer, VkDeviceMemory *a_pBufferMemory, VkBufferUsageFlags a_usage = 0)
        {
            VkBufferCreateInfo bufferCreateInfo{};
            bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.pNext       = nullptr;
            bufferCreateInfo.size        = a_bufferSize;                         
            bufferCreateInfo.usage       = a_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;            

            VK_CHECK_RESULT(vkCreateBuffer(a_device, &bufferCreateInfo, NULL, a_pBuffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(a_device, (*a_pBuffer), &memoryRequirements);

            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.pNext           = nullptr;
            allocateInfo.allocationSize  = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, 
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    a_physDevice);

            VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, a_pBufferMemory));

            VK_CHECK_RESULT(vkBindBufferMemory(a_device, (*a_pBuffer), (*a_pBufferMemory), 0));
        }

        static void CreateDeviceLocalBuffer(VkDevice a_device, VkPhysicalDevice a_physDevice, const size_t a_bufferSize,
                VkBuffer *a_pBuffer, VkDeviceMemory *a_pBufferMemory, VkBufferUsageFlags a_usage)
        {

            VkBufferCreateInfo bufferCreateInfo{};
            bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.pNext       = nullptr;
            bufferCreateInfo.size        = a_bufferSize;
            bufferCreateInfo.usage       = a_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;            

            VK_CHECK_RESULT(vkCreateBuffer(a_device, &bufferCreateInfo, NULL, a_pBuffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(a_device, (*a_pBuffer), &memoryRequirements);

            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.pNext           = nullptr;
            allocateInfo.allocationSize  = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = vk_utils::FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, a_physDevice);

            VK_CHECK_RESULT(vkAllocateMemory(a_device, &allocateInfo, NULL, a_pBufferMemory));

            VK_CHECK_RESULT(vkBindBufferMemory(a_device, (*a_pBuffer), (*a_pBufferMemory), 0));
        }

        static void SubmitStagingBuffer(VkDevice a_device, VkCommandPool a_pool, VkQueue a_queue, VkBuffer a_cpuBuffer, VkBuffer a_gpuBuffer, size_t a_size)
        {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = a_pool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuff;
            if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
                throw std::runtime_error("[SubmitStagingBuffer]: failed to allocate command buffer!");

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; 

            vkBeginCommandBuffer(cmdBuff, &beginInfo);

            VkBufferCopy copyRegion{};
            copyRegion.size = a_size;
            vkCmdCopyBuffer(cmdBuff, a_cpuBuffer, a_gpuBuffer, 1, &copyRegion);

            vkEndCommandBuffer(cmdBuff);

            RunCommandBuffer(cmdBuff, a_queue, a_device);

            vkFreeCommandBuffers(a_device, a_pool, 1, &cmdBuff);
        }

        static VkImageMemoryBarrier imBarTransfer(VkImage a_image, const VkImageSubresourceRange& a_range, VkImageLayout before, VkImageLayout after)
        {
            VkImageMemoryBarrier moveToGeneralBar{};
            moveToGeneralBar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            moveToGeneralBar.pNext               = nullptr;
            moveToGeneralBar.srcAccessMask       = 0;
            moveToGeneralBar.dstAccessMask       = VK_PIPELINE_STAGE_TRANSFER_BIT;
            moveToGeneralBar.oldLayout           = before;
            moveToGeneralBar.newLayout           = after;
            moveToGeneralBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            moveToGeneralBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            moveToGeneralBar.image               = a_image;
            moveToGeneralBar.subresourceRange    = a_range;
            return moveToGeneralBar;
        }

        static VkImageSubresourceRange WholeImageRange()
        {
            VkImageSubresourceRange rangeWholeImage{};
            rangeWholeImage.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            rangeWholeImage.baseMipLevel   = 0;
            rangeWholeImage.levelCount     = 1;
            rangeWholeImage.baseArrayLayer = 0;
            rangeWholeImage.layerCount     = 1;
            return rangeWholeImage;
        }

        static void CopyBufferToTexure(VkDevice a_device, VkCommandPool a_pool, VkQueue a_queue, VkBuffer a_cpuBuffer, VkImage a_image, uint32_t a_w, uint32_t a_h)
        {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = a_pool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuff;
            if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
                throw std::runtime_error("[CopyBufferToTexure]: failed to allocate command buffer!");

            VkCommandBufferBeginInfo beginInfo{};    
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuff, &beginInfo));

            VkImageSubresourceRange rangeWholeImage = WholeImageRange();

            VkImageSubresourceLayers shittylayers{};
            shittylayers.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            shittylayers.mipLevel       = 0;
            shittylayers.baseArrayLayer = 0;
            shittylayers.layerCount     = 1;

            VkBufferImageCopy wholeRegion = {};
            wholeRegion.bufferOffset      = 0;
            wholeRegion.bufferRowLength   = a_w;
            wholeRegion.bufferImageHeight = a_h;
            wholeRegion.imageExtent       = VkExtent3D{a_w, a_h, 1};
            wholeRegion.imageOffset       = VkOffset3D{0,0,0};
            wholeRegion.imageSubresource  = shittylayers;

            VkImageMemoryBarrier moveToGeneralBar = imBarTransfer(a_image,
                    rangeWholeImage,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            vkCmdPipelineBarrier(cmdBuff,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0, nullptr,            // general memory barriers
                    0, nullptr,            // buffer barriers
                    1, &moveToGeneralBar); // image  barriers

            VkClearColorValue clearVal = {};
            clearVal.float32[0] = 1.0f;
            clearVal.float32[1] = 0.0f;
            clearVal.float32[2] = 0.0f;
            clearVal.float32[3] = 0.0f;

            vkCmdClearColorImage(cmdBuff, a_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &rangeWholeImage);

            vkCmdCopyBufferToImage(cmdBuff, a_cpuBuffer, a_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &wholeRegion);


            VkImageMemoryBarrier imgBar{};
            {
                imgBar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imgBar.pNext = nullptr;
                imgBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imgBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                imgBar.srcAccessMask       = 0;
                imgBar.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                imgBar.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imgBar.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imgBar.image               = a_image;

                imgBar.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                imgBar.subresourceRange.baseMipLevel   = 0;
                imgBar.subresourceRange.levelCount     = 1;
                imgBar.subresourceRange.baseArrayLayer = 0;
                imgBar.subresourceRange.layerCount     = 1;
            };

            vkCmdPipelineBarrier(cmdBuff,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &imgBar);

            VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuff));

            RunCommandBuffer(cmdBuff, a_queue, a_device);

            vkFreeCommandBuffers(a_device, a_pool, 1, &cmdBuff);
        }

        static void RunCommandBuffer(VkCommandBuffer a_cmdBuff, VkQueue a_queue, VkDevice a_device)
        {
            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &a_cmdBuff;

            VkFence fence;
            VkFenceCreateInfo fenceCreateInfo{};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceCreateInfo.flags = 0;
            VK_CHECK_RESULT(vkCreateFence(a_device, &fenceCreateInfo, NULL, &fence));

            VK_CHECK_RESULT(vkQueueSubmit(a_queue, 1, &submitInfo, fence));

            VK_CHECK_RESULT(vkWaitForFences(a_device, 1, &fence, VK_TRUE, 100000000000));

            vkDestroyFence(a_device, fence, NULL);
        }

        void DrawFrame() 
        {
            vkWaitForFences(m_device, 1, &m_sync.inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
            vkResetFences  (m_device, 1, &m_sync.inFlightFences[m_currentFrame]);

            uint32_t imageIndex;
            vkAcquireNextImageKHR(m_device, m_screen.swapChain, UINT64_MAX, m_sync.imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

            if (vkResetCommandBuffer(m_commandBuffers[imageIndex], 0) != VK_SUCCESS)
            {
                throw std::runtime_error("[DrawFrame]: failed to reset command buffer!");
            }

            RecordCommandBuffer(m_device, m_screen.swapChainFramebuffers[imageIndex], m_screen.swapChainExtent, m_renderPass, m_renerables,
                    m_uniformBuffers[imageIndex], m_uniformBuffersMemory[imageIndex], m_commandBuffers[imageIndex], m_sceneDS[imageIndex],
                    imageIndex);

            VkSemaphore      waitSemaphores[]{ m_sync.imageAvailableSemaphores[m_currentFrame] };
            VkPipelineStageFlags waitStages[]{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores    = waitSemaphores;
            submitInfo.pWaitDstStageMask  = waitStages;

            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &m_commandBuffers[imageIndex];

            VkSemaphore signalSemaphores[] { m_sync.renderFinishedSemaphores[m_currentFrame] };
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = signalSemaphores;

            if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_sync.inFlightFences[m_currentFrame]) != VK_SUCCESS)
            {
                throw std::runtime_error("[DrawFrame]: failed to submit draw command buffer!");
            }

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores    = signalSemaphores;

            VkSwapchainKHR swapChains[]{ m_screen.swapChain };
            presentInfo.swapchainCount  = 1;
            presentInfo.pSwapchains     = swapChains;
            presentInfo.pImageIndices   = &imageIndex;

            vkQueuePresentKHR(m_presentQueue, &presentInfo);
            m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

    public:

        Application()
        {
            std::cout << "\tinitializing window...\n";
            glfwInit();

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

            m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);

            std::cout << "\tinitializing vulkan devices and queue...\n";

            const int deviceId = 0;

            std::vector<const char*> extensions;
            {
                uint32_t glfwExtensionCount = 0;
                const char** glfwExtensions;
                glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
                extensions     = std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
            }

            m_instance = vk_utils::CreateInstance(enableValidationLayers, m_enabledLayers, extensions);
            if (enableValidationLayers)
                vk_utils::InitDebugReportCallback(m_instance, &debugReportCallbackFn, &debugReportCallback);

            if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS)
                throw std::runtime_error("glfwCreateWindowSurface: failed to create window surface!");

            physicalDevice = vk_utils::FindPhysicalDevice(m_instance, true, deviceId);
            auto queueFID  = vk_utils::GetQueueFamilyIndex(physicalDevice, VK_QUEUE_GRAPHICS_BIT);

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFID, m_surface, &presentSupport);
            if (!presentSupport)
                throw std::runtime_error("vkGetPhysicalDeviceSurfaceSupportKHR: no present support for the target device and graphics queue");

            m_device = vk_utils::CreateLogicalDevice(queueFID, physicalDevice, m_enabledLayers, deviceExtensions);
            vkGetDeviceQueue(m_device, queueFID, 0, &m_graphicsQueue);
            vkGetDeviceQueue(m_device, queueFID, 0, &m_presentQueue);

            // ==> commandPool
            {
                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                poolInfo.queueFamilyIndex = vk_utils::GetQueueFamilyIndex(physicalDevice, VK_QUEUE_GRAPHICS_BIT);

                if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
                    throw std::runtime_error("[CreateCommandPoolAndBuffers]: failed to create command pool!");
            }

            vk_utils::CreateCwapChain(physicalDevice, m_device, m_surface, WIDTH, HEIGHT, &m_screen);

            vk_utils::CreateScreenImageViews(m_device, &m_screen);
        }

        ~Application() 
        { 
            std::cout << "\tcleaning up...\n";

            for (auto mesh : m_meshes)
            {
                mesh.second.setDevice(m_device);
                mesh.second.cleanup();
            }

            for (auto tex : m_textures)
            {
                tex.second.cleanup();
            }

            for (auto pipe : m_pipes)
            {
                vkDestroyPipeline      (m_device, pipe.second.pipeline, nullptr);
                vkDestroyPipelineLayout(m_device, pipe.second.pipelineLayout, nullptr);
            }

            vkDestroyDescriptorSetLayout(m_device, m_sceneDSLayout, nullptr);
            vkDestroyDescriptorPool(m_device, m_sceneDSPool, nullptr);

            for (auto& uboMem : m_uniformBuffersMemory)
            {
                vkFreeMemory(m_device, uboMem, nullptr);
            }
            for (auto& ubo : m_uniformBuffers)
            {
                vkDestroyBuffer(m_device, ubo, nullptr);
            }

            // free depth image resources
            vkFreeMemory      (m_device, m_depthImageMemory, NULL);
            vkDestroyImage    (m_device, m_depthImage,       NULL);
            vkDestroyImageView(m_device, m_depthImageView,   NULL);

            vkDestroyRenderPass    (m_device, m_renderPass, nullptr);

            if (enableValidationLayers)
            {
                // destroy callback.
                auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugReportCallbackEXT");
                func(m_instance, debugReportCallback, NULL);
            }

            for (size_t i{}; i < MAX_FRAMES_IN_FLIGHT; ++i) 
            {
                vkDestroySemaphore(m_device, m_sync.renderFinishedSemaphores[i], nullptr);
                vkDestroySemaphore(m_device, m_sync.imageAvailableSemaphores[i], nullptr);
                vkDestroyFence    (m_device, m_sync.inFlightFences[i], nullptr);
            }

            vkDestroyCommandPool(m_device, m_commandPool, nullptr);

            for (auto framebuffer : m_screen.swapChainFramebuffers) {
                vkDestroyFramebuffer(m_device, framebuffer, nullptr);
            }

            for (auto imageView : m_screen.swapChainImageViews) {
                vkDestroyImageView(m_device, imageView, nullptr);
            }

            vkDestroySwapchainKHR(m_device, m_screen.swapChain, nullptr);
            vkDestroyDevice(m_device, nullptr);

            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            vkDestroyInstance(m_instance, nullptr);

            glfwDestroyWindow(m_window);

            glfwTerminate();
        }


        void run() 
        {
            CreateResources();

            std::cout << "\tlaunching main loop...\n";
            MainLoop();
        }
};

int main() 
{
    Application app{};

    try 
    {
        app.run();
    }
    catch (const std::exception& e) 
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
