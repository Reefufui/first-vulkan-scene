#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

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
#include <utility>

#include "vk_utils.h"

//#define SHOW_BARS
#include "Mesh.hpp"
#include "Texture.hpp"
#include "ParticleSystem.hpp"
#include "Timer.hpp"

const int WIDTH     = 800;
const int HEIGHT    = 600;
const int CUBE_SIDE = 1000;

const int MAX_FRAMES_IN_FLIGHT = 3;

const std::vector<const char*> deviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

class Application 
{
    private:

        GLFWwindow* m_window;

        static Timer s_timer;
        static bool  s_shadowmapDebug;

        VkInstance m_instance;
        std::vector<const char*> m_enabledLayers;

        VkDebugUtilsMessengerEXT m_debugMessenger;
        VkSurfaceKHR m_surface;

        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice         m_device;

        VkQueue m_graphicsQueue;
        VkQueue m_presentQueue;

        vk_utils::ScreenBufferResources m_screen;

        struct RenderPasses {
            VkRenderPass     finalRenderPass;
            VkRenderPass     shadowCubemapPass;
        } m_renderPasses;

        VkCommandPool                m_commandPool;
        std::vector<VkCommandBuffer> m_drawCommandBuffers;
        size_t                       m_currentFrame{}; // for draw command buffer indexing

        struct FramebuffersOffscreen {
            VkFramebuffer shadowCubemapFrameBuffer;
        } m_framebuffersOffscreen;

        struct Attachments {
            // final pass
            Texture     presentDepth;
            // offscreen (shadow map)
            Texture     offscreenDepth;
            Texture     offscreenColor;
        } m_attachments;

        struct SyncObj
        {
            std::vector<VkSemaphore> imageAvailableSemaphores;
            std::vector<VkSemaphore> renderFinishedSemaphores;
            std::vector<VkFence>     inFlightFences;
        } m_sync;

        struct Pipe {
            VkPipeline                    pipeline;
            VkPipelineLayout              pipelineLayout;
        };

        CubeTexture      m_shadowCubemapTexture;
        InputCubeTexture m_shadowCubemap;

        struct DSLayouts {
            VkDescriptorSetLayout textureOnlyLayout; // suits cubemap texture as well
        } m_DSLayouts;

        struct DSPools {
            VkDescriptorPool textureDSPool; // suits cubemap texture as well
        } m_DSPools;

        struct RenderObject {
            Mesh*          mesh;
            Pipe*          pipe;
            InputTexture*  texture;
            glm::mat4      matrix;
        };

        std::unordered_map<std::string, Mesh>           m_meshes;
        std::unordered_map<std::string, Texture>        m_textures;
        std::unordered_map<std::string, Pipe>           m_pipes;
        std::unordered_map<std::string, InputTexture>   m_inputTextures;
        std::unordered_map<std::string, RenderObject>   m_renerables;
        std::unordered_map<std::string, ParticleSystem> m_particleSystems;

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

        static void keyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods)
        {
            if (a_action == GLFW_PRESS)
            {
                switch (a_key)
                {
                    case GLFW_KEY_1:
                        s_shadowmapDebug = false;
                        break;
                    case GLFW_KEY_2:
                        s_shadowmapDebug = true;
                        break;
                }
            }
        }

        static void CreateParticleSystem(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
                std::unordered_map<std::string, ParticleSystem>& a_particleSystems, std::unordered_map<std::string, InputTexture>& a_IT)
        {
            ParticleSystem fire{};

            fire.initParticles(glm::vec3(0.0f, 2.0f, 0.0f), 700);

            CreateHostVisibleBuffer(a_device, a_physDevice, fire.getSize(), &(fire.getVBO()), &(fire.getVBOMemory()),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            vkMapMemory(a_device, fire.getVBOMemory(), 0, fire.getSize(), 0, &fire.getMappedMemory());

            fire.attachTexture(&(a_IT["fire"]));

            a_particleSystems["fire"] = fire;
        }

        static void LoadDebugSquareMesh(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
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

            float vertices[] =
            {
                // X     Y
                -1.0f, -1.0f,
                +1.0f, -1.0f,
                -1.0f, +1.0f,
                +1.0f, +1.0f
            };

            uint32_t indices[] =
            {
                0, 1, 2,
                1, 2, 3
            };

            Mesh mesh{};

            fillMeshBuffer(mesh.getVBO().buffer, mesh.getVBO().memory, vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 8 * sizeof(float));

            fillMeshBuffer(mesh.getIBO().buffer, mesh.getIBO().memory, indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 6 * sizeof(uint32_t));

            a_meshes["debug square"] = mesh;
        }

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

            loadMesh("fireleviathan");
            loadMesh("surface");
            loadMesh("cube");
            loadMesh("dogeeye");
            loadMesh("doge");

            // This mesh is not from a file!
            LoadDebugSquareMesh(a_device, a_physDevice, a_pool, a_queue, a_meshes);
        }

        static void LoadTextures(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
                std::unordered_map<std::string, Texture>& a_textures)
        {
            auto fillTexture = [&](Texture& a_texture)
            {
                VkBuffer stagingBuffer{};
                VkDeviceMemory stagingBufferMemory{};

                CreateHostVisibleBuffer(a_device, a_physDevice, a_texture.getSize(), &stagingBuffer, &stagingBufferMemory);

                void *mappedMemory = nullptr;
                vkMapMemory(a_device, stagingBufferMemory, 0, a_texture.getSize(), 0, &mappedMemory);
                memcpy(mappedMemory, a_texture.rgba, a_texture.getSize());
                vkUnmapMemory(a_device, stagingBufferMemory);

                VkCommandBufferAllocateInfo allocInfo = {};
                allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool        = a_pool;
                allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;

                VkCommandBuffer cmdBuff{};
                if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
                    throw std::runtime_error("[CopyBufferToTexure]: failed to allocate command buffer!");

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuff, &beginInfo));
                {
                    VkImageMemoryBarrier imgBar = a_texture.makeBarrier(a_texture.wholeImageRange(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    a_texture.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

                    a_texture.copyBufferToTexture(cmdBuff, stagingBuffer);

                    imgBar = a_texture.makeBarrier(a_texture.wholeImageRange(), 0, VK_ACCESS_SHADER_READ_BIT,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    a_texture.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                }
                VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuff));

                RunCommandBuffer(cmdBuff, a_queue, a_device);

                vkFreeCommandBuffers(a_device, a_pool, 1, &cmdBuff);
                vkFreeMemory(a_device, stagingBufferMemory, nullptr);
                vkDestroyBuffer(a_device, stagingBuffer, nullptr);
            };

            auto loadTexture = [&](std::string&& textureName)
            {
                Texture texture{};

                std::string fileName{ "assets/textures/.png" };
                fileName.insert(fileName.find("."), textureName);

                texture.loadFromPNG(fileName.c_str());

                texture.create(a_device, a_physDevice, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R8G8B8A8_SRGB);

                fillTexture(texture);

                a_textures[textureName] = texture;
            };

            loadTexture("fireleviathan");
            loadTexture("white");
            loadTexture("troll");
            loadTexture("dogeeye");
            loadTexture("doge");
            loadTexture("fire");
        }


        static void ComposeScene(std::unordered_map<std::string, RenderObject>& a_renerables, std::unordered_map<std::string, Pipe>& a_pipes,
                std::unordered_map<std::string, Mesh>& a_meshes, std::unordered_map<std::string, InputTexture>& a_textures)
        {
            auto createRenderable = [&](std::string&& objectName, std::string&& meshName, std::string&& pipeName, std::string&& textureName)
            {
                RenderObject object{};

                {
                    auto found{ a_meshes.find(meshName) };
                    if (found == a_meshes.end())
                    {
                        throw std::runtime_error(std::string("Mesh not found: ") + meshName);
                    }
                    object.mesh = &(*found).second;
                }

                {
                    auto found = a_pipes.find(pipeName);
                    if (found == a_pipes.end())
                    {
                        throw std::runtime_error(std::string("Pipeline not found: ") + pipeName);
                    }
                    object.pipe = &(*found).second;
                }

                {
                    auto found = a_textures.find(textureName);
                    if (found != a_textures.end())
                    {
                        object.texture = &(*found).second;
                    }
                }

                object.matrix = glm::mat4(1.0f);

                a_renerables[objectName] = object;
            };

            // object / mesh / pipeline / texture

            //createRenderable("fireleviathan", "fireleviathan", "scene", "fireleviathan");
            createRenderable("dogeeye", "dogeeye", "scene", "dogeeye");
            createRenderable("doge", "doge", "scene", "doge");
            createRenderable("surface", "surface", "scene", "white");
            createRenderable("small cube", "cube", "scene", "troll");
            a_renerables["small cube"].matrix = glm::translate(glm::mat4(1.0f), glm::vec3(-6.5f, 0.5f, 0.5f));

            //createRenderable("cube", "cube", "scene", "troll");
            //a_renerables["cube"].matrix = glm::scale(glm::mat4(1.0f), glm::vec3(50.0f));
        }

        static void UpdateScene(std::unordered_map<std::string, RenderObject>& a_renerables)
        {
            float radius{ 3.0f };
            glm::vec3 translation = glm::vec3(radius * (float)sin(s_timer.getTime()), 2.0f, radius * (float)cos(s_timer.getTime()));

            {
                glm::mat4 m{1.0f};

                m = glm::scale(m, glm::vec3(1.0f, 1.0f + 0.1 * (float)sin(s_timer.getTime()), 1.0f));
                m = glm::translate(m, glm::vec3(0.0f, 2.0f, 0.0f));
                //m = glm::translate(m, translation);
                //m = glm::rotate(m, glm::radians(30.0f * (float)sin(s_timer.getTime())), glm::vec3(0, 1, 0));

                a_renerables["doge"].matrix = m;
                a_renerables["dogeeye"].matrix = m;
            }
        }

        void CreateResources()
        {
            std::cout << "\tcreating sync objects...\n";
            CreateSyncObjects(m_device, &m_sync);

            std::cout << "\tloading assets...\n";
            LoadTextures(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_textures);
            LoadMeshes(  m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_meshes);

            std::cout << "\tcreating attachments...\n";
            CreateAttachments(     m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_attachments);
            CreateShadowmapTexture(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_shadowCubemapTexture);

            std::cout << "\tcreating descriptor sets...\n";
            CreateTextureOnlyLayout(m_device, &m_DSLayouts.textureOnlyLayout);
            CreateTextureDescriptorPool(m_device, m_DSPools.textureDSPool, m_textures.size() + 1); // + 1 for cubemap
            CreateDSForEachTexture(m_device, &m_DSLayouts.textureOnlyLayout, m_DSPools.textureDSPool, m_inputTextures, m_textures,
                    m_shadowCubemap, m_shadowCubemapTexture);

            std::cout << "\tcreating render passes...\n";
            CreateFinalRenderpass(        m_device, &(m_renderPasses.finalRenderPass), m_screen.swapChainImageFormat);
            CreateShadowCubemapRenderPass(m_device, &(m_renderPasses.shadowCubemapPass));

            std::cout << "\tcreating frame buffers...\n";
            CreateScreenFrameBuffers(      m_device, m_renderPasses.finalRenderPass,   &m_screen,                                        m_attachments);
            CreateShadowCubemapFrameBuffer(m_device, m_renderPasses.shadowCubemapPass, m_framebuffersOffscreen.shadowCubemapFrameBuffer, m_attachments);

            std::cout << "\tcreating graphics pipelines...\n";
            CreateGraphicsPipelines(m_device, m_screen.swapChainExtent, m_renderPasses, m_pipes, m_DSLayouts);

            std::cout << "\tcreating particle systems...\n";
            CreateParticleSystem(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_particleSystems, m_inputTextures);

            std::cout << "\tcomposing scene...\n";
            ComposeScene(m_renerables, m_pipes, m_meshes, m_inputTextures);

            std::cout << "\tcreating drawing command buffers...\n";
            CreateDrawCommandBuffers(m_device, m_commandPool, m_screen.swapChainFramebuffers, &m_drawCommandBuffers);
        }


        void MainLoop()
        {
            while (!glfwWindowShouldClose(m_window)) 
            {
                glfwPollEvents();
                s_timer.timeStamp();
                UpdateScene(m_renerables);
                UpdateParticleSystems(m_particleSystems, LightPos());
                DrawFrame();
            }

            vkDeviceWaitIdle(m_device);
        }

        static void CreateFinalRenderpass(VkDevice a_device, VkRenderPass* a_pRenderPass, VkFormat a_swapChainImageFormat)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format         = a_swapChainImageFormat;
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
            depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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

            std::vector<VkAttachmentDescription> attachments {
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
                throw std::runtime_error("[CreateFinalRenderpass]: failed to create render pass!");
        }

        static void CreateShadowCubemapRenderPass(VkDevice a_device, VkRenderPass* a_pRenderPass)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format         = VK_FORMAT_R32_SFLOAT;
            colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
            depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = 1;
            depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = 1;
            subpass.pColorAttachments       = &colorAttachmentRef;
            subpass.pDepthStencilAttachment = &depthAttachmentRef;

            std::vector<VkAttachmentDescription> attachments{
                colorAttachment, depthAttachment
            };

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = attachments.size();
            renderPassInfo.pAttachments    = attachments.data();
            renderPassInfo.subpassCount    = 1;
            renderPassInfo.pSubpasses      = &subpass;

            if (vkCreateRenderPass(a_device, &renderPassInfo, nullptr, a_pRenderPass) != VK_SUCCESS)
                throw std::runtime_error("[CreateShadowCubemapRenderPass]: failed to create render pass!");
        }

        static void CreateTextureOnlyLayout(VkDevice a_device, VkDescriptorSetLayout *a_pDSLayout)
        {
            VkDescriptorSetLayoutBinding samplerLayoutBinding{};
            samplerLayoutBinding.binding            = 0;
            samplerLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerLayoutBinding.descriptorCount    = 1;
            samplerLayoutBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
            samplerLayoutBinding.pImmutableSamplers = nullptr;

            std::array<VkDescriptorSetLayoutBinding, 1> binds = {samplerLayoutBinding};

            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
            descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCreateInfo.bindingCount = binds.size();
            descriptorSetLayoutCreateInfo.pBindings    = binds.data();

            if (vkCreateDescriptorSetLayout(a_device, &descriptorSetLayoutCreateInfo, nullptr, a_pDSLayout) != VK_SUCCESS)
                throw std::runtime_error("[CreateTextureOnlyLayout]: failed to create DS layout!");
        }

        static void CreateOneImageDescriptorSet(VkDevice a_device, const VkDescriptorSetLayout *a_pDSLayout, VkDescriptorPool& a_DSPool,
                VkDescriptorSet& a_dset, VkImageView a_imageView, VkSampler a_imageSampler)
        {
            VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
            descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocateInfo.descriptorPool     = a_DSPool;
            descriptorSetAllocateInfo.descriptorSetCount = 1;
            descriptorSetAllocateInfo.pSetLayouts        = a_pDSLayout;

            if (vkAllocateDescriptorSets(a_device, &descriptorSetAllocateInfo, &a_dset) != VK_SUCCESS)
                throw std::runtime_error("[CreateOneImageDescriptorSet]: failed to allocate descriptor set pool!");

            VkWriteDescriptorSet descrWrite{};
            descrWrite.sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descrWrite.dstSet            = a_dset;
            descrWrite.dstBinding        = 0;
            descrWrite.dstArrayElement   = 0;
            descrWrite.descriptorType    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descrWrite.descriptorCount   = 1;

            VkDescriptorImageInfo        imageInfo{ a_imageSampler, a_imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            descrWrite.pImageInfo        = &imageInfo;

            vkUpdateDescriptorSets(a_device, 1, &descrWrite, 0, nullptr);
        }

        // this suits cubemap texture as well
        static void CreateTextureDescriptorPool(VkDevice a_device, VkDescriptorPool& a_dsPool, uint32_t a_count)
        {
            VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, a_count };

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{};
            descriptorPoolCreateInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCreateInfo.maxSets       = a_count;
            descriptorPoolCreateInfo.poolSizeCount = 1;
            descriptorPoolCreateInfo.pPoolSizes    = &poolSize;

            if (vkCreateDescriptorPool(a_device, &descriptorPoolCreateInfo, nullptr, &a_dsPool) != VK_SUCCESS)
                throw std::runtime_error("[CreateDSForEachTexture]: failed to create descriptor set pool!");
        }

        static void CreateDSForEachTexture(VkDevice a_device, VkDescriptorSetLayout* a_pDSLayout, VkDescriptorPool& a_dsPool,
                std::unordered_map<std::string, InputTexture>& a_inputTextures, std::unordered_map<std::string, Texture>& a_textures,
                InputCubeTexture& a_inputCubeTexture, CubeTexture& a_cubeTexture)
        {
            for (auto& texture : a_textures)
            {
                Texture* pTextureObj{ &texture.second };
                InputTexture inputTexture{ pTextureObj, VK_NULL_HANDLE };

                CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, inputTexture.descriptorSet, pTextureObj->getImageView(), pTextureObj->getSampler());

                a_inputTextures[texture.first] = inputTexture;
            }

            a_inputCubeTexture.shadowCubemap = &a_cubeTexture;
            CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, a_inputCubeTexture.descriptorSet, a_cubeTexture.getImageView(), a_cubeTexture.getSampler());
        }

        static void CreateGraphicsPipelines(VkDevice a_device, VkExtent2D a_screenExtent, RenderPasses a_renderPasses,
                std::unordered_map<std::string, Pipe>& a_pipes, DSLayouts a_dsLayouts)
        {
            // initializing default pipeline structures
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
            rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
                { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants) }
            };

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.pushConstantRangeCount = pushConstants.size();
            pipelineLayoutInfo.pPushConstantRanges    = pushConstants.data();

            std::vector<VkDynamicState> dynamicStates {
                VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT
            };

            VkPipelineDynamicStateCreateInfo dynamicStatesInfo{};
            dynamicStatesInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicStatesInfo.dynamicStateCount = dynamicStates.size();
            dynamicStatesInfo.pDynamicStates    = dynamicStates.data();

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.pVertexInputState   = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState      = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState   = &multisampling;
            pipelineInfo.pDepthStencilState  = &depthAndStencil;
            pipelineInfo.pColorBlendState    = &colorBlending;
            pipelineInfo.pDynamicState       = &dynamicStatesInfo;
            pipelineInfo.subpass             = 0;
            pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;

            VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
            vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.pName  = "main";

            VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
            fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.pName  = "main";

            auto createPipeline = [&](std::string&& a_pipeName, std::vector<VkDescriptorSetLayout>& a_dsLayouts, std::string&& a_shaderName, VkRenderPass a_renderPass)
            {
                pipelineLayoutInfo.setLayoutCount = a_dsLayouts.size();
                if (pipelineLayoutInfo.setLayoutCount)
                {
                    pipelineLayoutInfo.pSetLayouts = a_dsLayouts.data();
                }
                else
                {
                    pipelineLayoutInfo.pSetLayouts = nullptr;
                }

                VkPipelineLayout pipelineLayout{};
                if (vkCreatePipelineLayout(a_device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
                    throw std::runtime_error("[CreateGraphicsPipeline]: failed to create pipeline layout!");

                std::string fileName{};

                fileName = "shaders/.vert.spv";
                fileName.insert(fileName.find("."), a_shaderName);
                auto vertShaderCode = vk_utils::ReadFile(fileName.c_str());

                fileName = "shaders/.frag.spv";
                fileName.insert(fileName.find("."), a_shaderName);
                auto fragShaderCode = vk_utils::ReadFile(fileName.c_str());

                vertShaderStageInfo.module = vk_utils::CreateShaderModule(a_device, vertShaderCode);
                fragShaderStageInfo.module = vk_utils::CreateShaderModule(a_device, fragShaderCode);

                std::vector<VkPipelineShaderStageCreateInfo> shaderStages {
                    vertShaderStageInfo, fragShaderStageInfo
                };

                pipelineInfo.stageCount          = shaderStages.size();
                pipelineInfo.pStages             = shaderStages.data();
                pipelineInfo.layout              = pipelineLayout;
                pipelineInfo.renderPass          = a_renderPass;

                VkPipeline pipeline{};
                if (vkCreateGraphicsPipelines(a_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
                    throw std::runtime_error("[CreateGraphicsPipeline]: failed to create graphics pipeline!");
                a_pipes[a_pipeName] = Pipe{ pipeline, pipelineLayout };

                vkDestroyShaderModule(a_device, fragShaderStageInfo.module, nullptr);
                vkDestroyShaderModule(a_device, vertShaderStageInfo.module, nullptr);
            };

            // render meshes ///////////////////////////////////////////////////////////
            std::vector<VkDescriptorSetLayout> sceneDSLayouts{ a_dsLayouts.textureOnlyLayout, a_dsLayouts.textureOnlyLayout };
            createPipeline("scene", sceneDSLayouts, "scene", a_renderPasses.finalRenderPass);

            // render to cubemap face //////////////////////////////////////////////////
            std::vector<VkDescriptorSetLayout> shadowCubemapDSLayout(0);
            createPipeline("shadow cubemap", shadowCubemapDSLayout, "shadowmap", a_renderPasses.shadowCubemapPass);

            // display cubemap faces ///////////////////////////////////////////////////
            VkVertexInputBindingDescription   inputBindings{ 0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX };
            VkVertexInputAttributeDescription attributes{ 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
            vertexInputInfo = VkPipelineVertexInputStateCreateInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount   = 1;
            vertexInputInfo.vertexAttributeDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions      = &inputBindings;
            vertexInputInfo.pVertexAttributeDescriptions    = &attributes;

            std::vector<VkDescriptorSetLayout> showCubemapDSLayout{ a_dsLayouts.textureOnlyLayout };
            createPipeline("show cubemap", showCubemapDSLayout, "showcubemap", a_renderPasses.finalRenderPass);

            // render particle system //////////////////////////////////////////////////
            vertexDescr = ParticleSystem::getVertexDescription();
            vertexInputInfo = VkPipelineVertexInputStateCreateInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount   = vertexDescr.bindings.size();
            vertexInputInfo.vertexAttributeDescriptionCount = vertexDescr.attributes.size();
            vertexInputInfo.pVertexBindingDescriptions      = vertexDescr.bindings.data();
            vertexInputInfo.pVertexAttributeDescriptions    = vertexDescr.attributes.data();

            inputAssembly.topology                   = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            depthAndStencil.depthWriteEnable         = VK_FALSE;
            colorBlendAttachment.blendEnable         = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;

            std::vector<VkDescriptorSetLayout> particleSystemDSLayout{ a_dsLayouts.textureOnlyLayout };
            createPipeline("particle system", particleSystemDSLayout, "particle", a_renderPasses.finalRenderPass);
        }

        static void CreateScreenFrameBuffers(VkDevice a_device, VkRenderPass a_renderPass, vk_utils::ScreenBufferResources* pScreen,
                Attachments& a_attachments)
        {
            pScreen->swapChainFramebuffers.resize(pScreen->swapChainImageViews.size());

            for (size_t i = 0; i < pScreen->swapChainImageViews.size(); i++) 
            {
                std::vector<VkImageView> attachments{
                    pScreen->swapChainImageViews[i],
                    a_attachments.presentDepth.getImageView()
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

        static void CreateShadowCubemapFrameBuffer(VkDevice a_device, VkRenderPass a_renderPass, VkFramebuffer& a_frameBuffer, Attachments& a_attachments)
        {
            std::vector<VkImageView> attachments {
                a_attachments.offscreenColor.getImageView(),
                    a_attachments.offscreenDepth.getImageView(),
            };

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = a_renderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments    = attachments.data();
            framebufferInfo.width           = CUBE_SIDE;
            framebufferInfo.height          = CUBE_SIDE;
            framebufferInfo.layers          = 1;

            if (vkCreateFramebuffer(a_device, &framebufferInfo, nullptr, &a_frameBuffer) != VK_SUCCESS)
                throw std::runtime_error("failed to create framebuffer!");
        }

        static glm::mat4 Camera()
        {
            glm::vec3 cameraPos{ glm::vec3(4.0f) };

            glm::mat4 view = glm::lookAt(
                    cameraPos,                      //eye (cam position)
                    glm::vec3(0.0f, 0.0f, 0.0f),    //center (where we are looking)
                    glm::vec3(0.f, 1.f, 0.f)        //up (worlds upwards direction)
                    );

            glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)WIDTH / (float)HEIGHT, 0.001f, 70.0f);

            return projection * view;
        }

        static glm::vec3 LightPos()
        {
            glm::vec3 pos = glm::vec3(5.0f * (float)sin(s_timer.getTime() / 3.0f), 2.0f, 5.0f * (float)cos(s_timer.getTime() / 3.0f));
            //glm::vec3 pos = glm::vec3(-3.0f, 2.0f, -3.0f);
            return pos;
        }

        static glm::mat4 light(uint32_t a_face)
        {
            // lookAt matrix doesnt suit as soon as we cant look directly up/down with it
            // implenented using basic glm functionality

            glm::mat4 model = glm::translate(glm::mat4(1.0f), -LightPos());

            glm::mat4 view = glm::mat4(1.0f);
            switch (a_face)
            {
                case 0: // +X
                    view = glm::rotate(view, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                    view = glm::rotate(view, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                    break;
                case 1: // -X
                    view = glm::rotate(view, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                    view = glm::rotate(view, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                    break;
                case 2: // -Y
                    view = glm::rotate(view, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                    view = glm::rotate(view, glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                    break;
                case 3: // +Y
                    view = glm::rotate(view, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                    view = glm::rotate(view, glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                    break;
                case 4: // +Z
                    view = glm::rotate(view, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                    break;
                case 5: // -Z
                    view = glm::rotate(view, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                    break;
            }

            glm::mat4 projection = glm::perspective(glm::radians(90.f), 1.0f, 0.001f, (float)CUBE_SIDE);

            return projection * view * model;
        }

        static void RecordCommandsOfShowingCubemap(VkDevice a_device, Mesh a_squareMesh, VkCommandBuffer a_cmdBuffer, const Pipe* a_cubemapPipe,
                InputCubeTexture a_cubeTexture)
        {
            vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_cubemapPipe->pipeline);

            vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_cubemapPipe->pipelineLayout, 0, 1,
                    &a_cubeTexture.descriptorSet, 0, nullptr);

            VkBuffer vbo{ a_squareMesh.getVBO().buffer };
            VkBuffer ibo{ a_squareMesh.getIBO().buffer };

            std::vector<VkDeviceSize> offsets{ 0 };

            vkCmdBindVertexBuffers(a_cmdBuffer, 0, 1, &vbo, offsets.data());
            vkCmdBindIndexBuffer(a_cmdBuffer, ibo, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(a_cmdBuffer, 6, 6, 0, 0, 0); // 6 instances for each cube face
        }

        static void RecordCommandsOfDrawingParticleSystems(std::unordered_map<std::string, ParticleSystem> a_particleSystems, VkCommandBuffer a_cmdBuffer,
                std::unordered_map<std::string, Pipe>& a_pipes)
        {
            VkPipeline&       pipeline = a_pipes["particle system"].pipeline;
            VkPipelineLayout& layout   = a_pipes["particle system"].pipelineLayout;

            for (auto& particleSystem : a_particleSystems)
            {
                auto& system{ particleSystem.second };

                vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

                vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &(system.getTexture()->descriptorSet), 0, nullptr);

                PushConstants constants{};
                constants.model    = glm::mat4(1.0f);
                constants.vp       = Camera();
                constants.lightPos = LightPos();

                vkCmdPushConstants(a_cmdBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &constants);

                VkDeviceSize offsets[1] = { 0 };
                vkCmdBindVertexBuffers(a_cmdBuffer, 0, 1, &(system.getVBO()), offsets);

                vkCmdDraw(a_cmdBuffer, system.getParticleCount(), 1, 0, 0);
            }
        }

        static void RecordCommandsOfDrawingRenderables(std::unordered_map<std::string, RenderObject> a_objects, VkCommandBuffer a_cmdBuffer,
                const Pipe* a_specialPipeline, glm::mat4 a_vpMatrix, InputCubeTexture a_shadowCubemap)
        {
            bool  specialPipeline{ a_specialPipeline != nullptr };
            Mesh* previousMesh{nullptr};
            Pipe* previousPipe{nullptr};

            if (specialPipeline)
            {
                vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_specialPipeline->pipeline);
            }

            for (auto& object : a_objects)
            {
                auto& obj{ object.second };

                const VkPipeline&       pipeline = (!specialPipeline) ? obj.pipe->pipeline       : a_specialPipeline->pipeline;
                const VkPipelineLayout& pLayout  = (!specialPipeline) ? obj.pipe->pipelineLayout : a_specialPipeline->pipelineLayout;

                if (!specialPipeline && obj.pipe != previousPipe)
                {
                    vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    previousPipe = obj.pipe;
                }

                if (!specialPipeline)
                {
                    std::vector<VkDescriptorSet> setsToBind{
                        obj.texture->descriptorSet, a_shadowCubemap.descriptorSet
                    };

                    vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pLayout, 0,
                            setsToBind.size(),
                            setsToBind.data(),
                            0, nullptr);
                }

                PushConstants constants{};
                constants.model    = obj.matrix;
                constants.vp       = a_vpMatrix;
                constants.lightPos = LightPos();

                vkCmdPushConstants(a_cmdBuffer, pLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &constants);

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
        }

        static void RecordCommandsToRenderForCubemapFace(VkFramebuffer a_frameBuffer, VkRenderPass a_renderPass, Pipe a_pipe,
                const uint32_t a_face, VkCommandBuffer a_cmdBuff, const std::unordered_map<std::string, RenderObject>& a_objects)
        {
            std::vector<VkClearValue> clearValues(2);
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = a_renderPass;
            renderPassInfo.framebuffer       = a_frameBuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = { (uint32_t)CUBE_SIDE, (uint32_t)CUBE_SIDE };
            renderPassInfo.clearValueCount   = clearValues.size();
            renderPassInfo.pClearValues      = clearValues.data();

            vkCmdBeginRenderPass(a_cmdBuff, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            RecordCommandsOfDrawingRenderables(a_objects, a_cmdBuff, &a_pipe, light(a_face), InputCubeTexture{});

            vkCmdEndRenderPass(a_cmdBuff);
        }

        static void RecordCommandsOfCopyingToCubemapFace(const uint32_t a_face, VkCommandBuffer a_cmdBuff, Texture& a_srcTexutre,
                CubeTexture* a_cubemap)
        {
            VkImageMemoryBarrier imgBar = a_srcTexutre.makeBarrier(a_srcTexutre.wholeImageRange(), 0, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            a_srcTexutre.changeImageLayout(a_cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            imgBar = a_cubemap->makeBarrier(a_cubemap->oneFaceRange(a_face), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            a_cubemap->changeImageLayout(a_cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            a_cubemap->copyImageToCubeface(a_cmdBuff, a_srcTexutre.getImage(), a_face);

            imgBar = a_srcTexutre.makeBarrier(a_srcTexutre.wholeImageRange(), 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            a_srcTexutre.changeImageLayout(a_cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

            imgBar = a_cubemap->makeBarrier(a_cubemap->oneFaceRange(a_face), 0, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            a_cubemap->changeImageLayout(a_cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }

        static void SetViewportAndScissor(VkCommandBuffer a_cmdBuffer, const float&& a_width, const float&& a_height, const bool&& a_flipViewport)
        {
            VkViewport viewport{};

            if (a_flipViewport)
            {
                viewport.x = 0;
                viewport.y = (float)a_height - 0;
                viewport.width = (float)a_width;
                viewport.height = -(float)a_height;
            }
            else
            {
                viewport.x        = 0;
                viewport.y        = 0;
                viewport.width    = (float)a_width;
                viewport.height   = (float)a_height;
            }

            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(a_cmdBuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.extent = { (uint32_t)a_width, (uint32_t)a_height };
            vkCmdSetScissor(a_cmdBuffer, 0, 1, &scissor);
        }

        static void RecordDrawingBuffer(VkDevice a_device, VkFramebuffer a_swapChainFramebuffer, FramebuffersOffscreen a_offscreenFrameBuffers,
                VkExtent2D a_frameBufferExtent, RenderPasses a_renderPasses, std::unordered_map<std::string, RenderObject>& a_objects,
                VkCommandBuffer a_cmdBuffer, std::unordered_map<std::string, Pipe>& a_pipes, Attachments& a_attachments,
                InputCubeTexture& a_cubemap, std::unordered_map<std::string, Mesh>& a_meshes,
                std::unordered_map<std::string, ParticleSystem>& a_systems) 
        {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            if (vkBeginCommandBuffer(a_cmdBuffer, &beginInfo) != VK_SUCCESS) 
                throw std::runtime_error("[CreateCommandPoolAndBuffers]: failed to begin recording command buffer!");

            SetViewportAndScissor(a_cmdBuffer, (float)CUBE_SIDE, (float)CUBE_SIDE, true);

            for (uint32_t face{}; face < 6; ++face)
            {
                RecordCommandsToRenderForCubemapFace(a_offscreenFrameBuffers.shadowCubemapFrameBuffer, a_renderPasses.shadowCubemapPass,
                        a_pipes["shadow cubemap"], face, a_cmdBuffer, a_objects);
                RecordCommandsOfCopyingToCubemapFace(face, a_cmdBuffer, a_attachments.offscreenColor, a_cubemap.shadowCubemap);
            }

            VkClearValue colorClear;
            //colorClear.color = { {  1.0f, 0.7f, 0.6f, 1.0f } };
            colorClear.color = { {  0.0f, 0.0f, 0.0f, 1.0f } };

            VkClearValue depthClear;
            depthClear.depthStencil.depth = 1.f;

            std::vector<VkClearValue> clearValues{ colorClear, depthClear };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = a_renderPasses.finalRenderPass;
            renderPassInfo.framebuffer       = a_swapChainFramebuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = a_frameBufferExtent;
            renderPassInfo.clearValueCount   = clearValues.size();
            renderPassInfo.pClearValues      = clearValues.data();

            SetViewportAndScissor(a_cmdBuffer, (float)WIDTH, (float)HEIGHT, true);
            vkCmdBeginRenderPass(a_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            if (s_shadowmapDebug)
            {
                RecordCommandsOfShowingCubemap(a_device, a_meshes["debug square"], a_cmdBuffer, &a_pipes["show cubemap"], a_cubemap);
            }
            else
            {
                RecordCommandsOfDrawingRenderables(a_objects, a_cmdBuffer, nullptr, Camera(), a_cubemap);
                RecordCommandsOfDrawingParticleSystems(a_systems, a_cmdBuffer, a_pipes);
            }

            vkCmdEndRenderPass(a_cmdBuffer);

            if (vkEndCommandBuffer(a_cmdBuffer) != VK_SUCCESS) {
                throw std::runtime_error("failed to record command buffer!");
            }
        }

        static void UpdateParticleSystems(std::unordered_map<std::string, ParticleSystem>& a_particleSystems, glm::vec3 a_emmiterPos)
        {
            for (auto& ps : a_particleSystems)
            {
                ps.second.updateParticles(s_timer.getTime(), a_emmiterPos);
            }
        }

        static void CreateDrawCommandBuffers(VkDevice a_device, VkCommandPool a_cmdPool, std::vector<VkFramebuffer> a_swapChainFramebuffers,
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

        static void CreateShadowmapTexture(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
                CubeTexture& a_cubemap)
        {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = a_pool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuff{};
            if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
                throw std::runtime_error("[CopyBufferToTexure]: failed to allocate command buffer!");

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuff, &beginInfo));
            {
                a_cubemap.setExtent(VkExtent3D{uint32_t(CUBE_SIDE), uint32_t(CUBE_SIDE), 1});
                a_cubemap.create(a_device, a_physDevice, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R32_SFLOAT);

                VkImageMemoryBarrier imgBar = a_cubemap.makeBarrier(a_cubemap.wholeImageRange(), 0, VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                a_cubemap.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }
            VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuff));

            RunCommandBuffer(cmdBuff, a_queue, a_device);

            vkFreeCommandBuffers(a_device, a_pool, 1, &cmdBuff);
        }

        static void CreateAttachments(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue, Attachments& a_attachments)
        {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = a_pool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuff{};
            if (vkAllocateCommandBuffers(a_device, &allocInfo, &cmdBuff) != VK_SUCCESS)
                throw std::runtime_error("[CopyBufferToTexure]: failed to allocate command buffer!");

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuff, &beginInfo));
            {
                // Shadow cubemap renderpass - color attachment
                Texture& offscreenColor = a_attachments.offscreenColor;
                offscreenColor.setExtent(VkExtent3D{uint32_t(CUBE_SIDE), uint32_t(CUBE_SIDE), 1});
                offscreenColor.create(a_device, a_physDevice, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_FORMAT_R32_SFLOAT);

                VkImageMemoryBarrier imgBar = offscreenColor.makeBarrier(offscreenColor.wholeImageRange(), 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                offscreenColor.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

                // Shadow cubemap renderpass - depth attachment
                Texture& offscreenDepth = a_attachments.offscreenDepth;
                offscreenDepth.setExtent(VkExtent3D{uint32_t(CUBE_SIDE), uint32_t(CUBE_SIDE), 1});
                offscreenDepth.create(a_device, a_physDevice, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_D32_SFLOAT);

                imgBar = offscreenDepth.makeBarrier(offscreenDepth.wholeImageRange(), 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                offscreenDepth.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

                // Final renderpass - depth attachment
                Texture& presentDepth = a_attachments.presentDepth;
                presentDepth.setExtent(VkExtent3D{uint32_t(WIDTH), uint32_t(HEIGHT), 1});
                presentDepth.create(a_device, a_physDevice, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_D32_SFLOAT);

                imgBar = presentDepth.makeBarrier(presentDepth.wholeImageRange(), 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                presentDepth.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
            }
            VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuff));
            RunCommandBuffer(cmdBuff, a_queue, a_device);

            vkFreeCommandBuffers(a_device, a_pool, 1, &cmdBuff);
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

            if (vkResetCommandBuffer(m_drawCommandBuffers[imageIndex], 0) != VK_SUCCESS)
            {
                throw std::runtime_error("[DrawFrame]: failed to reset command buffer!");
            }

            RecordDrawingBuffer(m_device, m_screen.swapChainFramebuffers[imageIndex], m_framebuffersOffscreen, m_screen.swapChainExtent,
                    m_renderPasses, m_renerables, m_drawCommandBuffers[imageIndex], m_pipes, m_attachments, m_shadowCubemap, m_meshes,
                    m_particleSystems);

            VkSemaphore      waitSemaphores[]{ m_sync.imageAvailableSemaphores[m_currentFrame] };
            VkPipelineStageFlags waitStages[]{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores    = waitSemaphores;
            submitInfo.pWaitDstStageMask  = waitStages;

            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &m_drawCommandBuffers[imageIndex];

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

            glfwSetKeyCallback(m_window, keyCallback);

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

            for (auto ps : m_particleSystems)
            {
                ps.second.cleanup(m_device);
            }

            m_shadowCubemapTexture.cleanup();

            m_attachments.presentDepth.cleanup();
            m_attachments.offscreenDepth.cleanup();
            m_attachments.offscreenColor.cleanup();

            for (auto pipe : m_pipes)
            {
                vkDestroyPipeline      (m_device, pipe.second.pipeline, nullptr);
                vkDestroyPipelineLayout(m_device, pipe.second.pipelineLayout, nullptr);
            }

            vkDestroyDescriptorPool(m_device, m_DSPools.textureDSPool, nullptr);
            vkDestroyDescriptorSetLayout(m_device, m_DSLayouts.textureOnlyLayout, nullptr);

            vkDestroyRenderPass(m_device, m_renderPasses.finalRenderPass, nullptr);
            vkDestroyRenderPass(m_device, m_renderPasses.shadowCubemapPass, nullptr);

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
            vkDestroyFramebuffer(m_device, m_framebuffersOffscreen.shadowCubemapFrameBuffer, nullptr);

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

Timer Application::s_timer;
bool  Application::s_shadowmapDebug;     // "2" binding (normal mode - "1")

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
