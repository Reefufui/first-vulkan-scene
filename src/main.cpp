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
#include <cmath>

#include "vk_utils.h"

//#define SHOW_BARS
#include "Mesh.hpp"
#include "Texture.hpp"
#include "ParticleSystem.hpp"
#include "Timer.hpp"
#include "Eye.hpp"

const int MAX_FRAMES_IN_FLIGHT = 3;

// NOTE: hardcoded in shader
const int SSAO_SAMPLING_KERNEL_SIZE = 30;

const std::vector<const char*> deviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

struct PushConstants {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 lightPos;
};

class Application 
{
    private:

        GLFWwindow* m_window;

        static bool s_shadowmapDebug;
        static bool s_ssaoEnabled;
        static bool s_bloomEnabled;

        static VkDescriptorSet s_blackTexutreDS;

        Timer m_timer;

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
            VkRenderPass shadowCubemapPass;
            VkRenderPass gBufferCreationPass;
            VkRenderPass ssaoPass;
            VkRenderPass ssaoBlurPass;
            VkRenderPass bloomPass;
            VkRenderPass finalRenderPass;
        } m_renderPasses;

        VkCommandPool                m_commandPool;
        std::vector<VkCommandBuffer> m_drawCommandBuffers;
        size_t                       m_currentFrame{}; // for draw command buffer indexing

        struct FramebuffersOffscreen {
            VkFramebuffer shadowCubemapFrameBuffer;
            VkFramebuffer bloomFrameBuffer;
            VkFramebuffer gBufferCreationFrameBuffer;
            VkFramebuffer ssaoFrameBuffer;
            VkFramebuffer ssaoBlurFrameBuffer;
        } m_framebuffersOffscreen;

        struct Attachments {
            // final pass
            Texture     presentDepth;
            CubeTexture shadowCubemap;
            // SSAO
            Texture gPositionAndDepth;
            Texture gNormals;
            Texture ssao;
            Texture blurredSSAO;
            // bloom
            Texture bloom;
            Texture bloomDepth;
            // offscreen (shadow map)
            Texture offscreenDepth;
            Texture offscreenColor;
        } m_attachments;

        struct UniformBuffer {
            VkBuffer        buffer;
            VkDeviceMemory  memory;
            VkDescriptorSet descriptorSet;
        };

        struct InputAttachments {
            InputTexture     gPositionAndDepth;
            InputTexture     gNormals;
            InputTexture     ssao;
            InputTexture     blurredSSAO;
            InputTexture     bloom;
            InputCubeTexture shadowCubemap;
        } m_inputAttachments;

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

        struct DSLayouts {
            VkDescriptorSetLayout textureOnlyLayout; // suits cubemap texture as well
            VkDescriptorSetLayout uboOnlyLayout;
        } m_DSLayouts;

        struct DSPools {
            VkDescriptorPool textureDSPool; // suits cubemap texture as well
            VkDescriptorPool uboDSPool;
        } m_DSPools;

        struct RenderObject {
            Mesh*          mesh;
            Pipe*          pipe;
            InputTexture*  texture;
            glm::mat4      matrix;
            bool           bloom;
        };

        std::unordered_map<std::string, Mesh>           m_meshes;
        std::unordered_map<std::string, Texture>        m_textures;
        std::unordered_map<std::string, Pipe>           m_pipes;
        std::unordered_map<std::string, InputTexture>   m_inputTextures;
        std::unordered_map<std::string, RenderObject>   m_renderables;
        std::unordered_map<std::string, ParticleSystem> m_particleSystems;
        std::unordered_map<std::string, Eye*>           m_pEyes;
        // r/w uniform buffers should be created for each MAX_FRAMES_IN_FLIGHT,
        // but we do not use them in this application for simplicity
        std::unordered_map<std::string, UniformBuffer>  m_roUniformBuffers; // ro = read only

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
                    case GLFW_KEY_3:
                        s_ssaoEnabled = !s_ssaoEnabled;
                        break;
                    case GLFW_KEY_4:
                        s_bloomEnabled = !s_bloomEnabled;
                        break;
                }
            }
        }

        static void CreateParticleSystem(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
                std::unordered_map<std::string, ParticleSystem>& a_particleSystems, std::unordered_map<std::string, InputTexture>& a_IT,
                Timer* a_timer)
        {
            ParticleSystem fire{};

            fire.setTimer(a_timer);
            fire.initParticles(glm::vec3(0.0f, 2.0f, 0.0f), 700);

            CreateHostVisibleBuffer(a_device, a_physDevice, fire.getSize(), &(fire.getVBO()), &(fire.getVBOMemory()),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            vkMapMemory(a_device, fire.getVBOMemory(), 0, fire.getSize(), 0, &fire.getMappedMemory());

            fire.attachTexture(&(a_IT["fire"]));

            a_particleSystems["fire"] = fire;
        }

        static void LoadQuadMesh(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
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

            a_meshes["quad"] = mesh;
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
            loadMesh("lion");

            // This mesh is not from a file!
            LoadQuadMesh(a_device, a_physDevice, a_pool, a_queue, a_meshes);
        }

        static void LoadTextures(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue,
                std::unordered_map<std::string, Texture>& a_textures, Timer a_timer)
        {
            auto fillTexture = [&](Texture& a_texture, void* a_src, size_t a_size)
            {
                VkBuffer stagingBuffer{};
                VkDeviceMemory stagingBufferMemory{};

                CreateHostVisibleBuffer(a_device, a_physDevice, a_size, &stagingBuffer, &stagingBufferMemory);

                void *mappedMemory = nullptr;
                vkMapMemory(a_device, stagingBufferMemory, 0, a_size, 0, &mappedMemory);
                memcpy(mappedMemory, a_src, a_size);
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
                if (textureName != "fire") texture.setAddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT);

                texture.create(a_device, a_physDevice, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R8G8B8A8_SRGB);

                fillTexture(texture, texture.rgba, texture.getSize());

                a_textures[textureName] = texture;
            };

            loadTexture("fireleviathan");
            loadTexture("fire");
            loadTexture("lion");

            loadTexture("white");
            loadTexture("black");

            // This texture is not from a file! (for SSAO)
            {
                a_timer.timeStamp();
                std::default_random_engine randomEngine{ (long unsigned int)a_timer.getTime() };

                auto random = [&](float a_range)
                {
                    std::uniform_real_distribution<float> randomDistribution(0.0f, a_range);
                    return randomDistribution(randomEngine);
                };

                std::vector<glm::vec4> randomNoice(4 * 4);
                for (auto& rotVector : randomNoice)
                {
                    rotVector = glm::vec4(random(2.0f) - 1.0f, random(2.0f) - 1.0f, 0.0f, 0.0f);
                }

                Texture texture{};

                texture.setExtent(VkExtent3D{4, 4, 1});
                texture.setAddressMode(VK_SAMPLER_ADDRESS_MODE_REPEAT);
                texture.create(a_device, a_physDevice, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R32G32B32A32_SFLOAT);

                fillTexture(texture, randomNoice.data(), randomNoice.size() * sizeof(glm::vec4));

                a_textures["noise"] = texture;

                randomNoice = std::vector<glm::vec4>(0);
            }
        }

        static void ComposeScene(std::unordered_map<std::string, RenderObject>& a_renerables, std::unordered_map<std::string, Pipe>& a_pipes,
                std::unordered_map<std::string, Mesh>& a_meshes, std::unordered_map<std::string, InputTexture>& a_textures)
        {
            auto createRenderable = [&](std::string&& objectName, std::string&& meshName, std::string&& pipeName, std::string&& textureName,
                    bool a_bloom)
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
                object.bloom = a_bloom;

                a_renerables[objectName] = object;
            };

            // object / mesh / pipeline / texture

            createRenderable("fireleviathan", "fireleviathan", "scene", "fireleviathan", true);
            createRenderable("surface", "surface", "scene", "black", false);

            createRenderable("lion", "lion", "scene", "white", false);
            a_renerables["lion"].matrix = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -2.5f, 1.5f));
            a_renerables["lion"].matrix = glm::scale(a_renerables["lion"].matrix, glm::vec3(0.1f));
            a_renerables["lion"].matrix = glm::rotate(a_renerables["lion"].matrix, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            a_renerables["lion"].matrix = glm::rotate(a_renerables["lion"].matrix, glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        static void UpdateScene(std::unordered_map<std::string, RenderObject>& a_renerables, float a_time)
        {
            {
                glm::mat4 m{1.0f};

                m = glm::scale(m, glm::vec3(1.0f, 0.7f + 0.1 * (float)sin(a_time), 1.0f));
                m = glm::translate(m, glm::vec3(3.0f, 12.0f, -3.0f));
                m = glm::rotate(m, glm::radians(30.0f * (float)sin(a_time)), glm::vec3(0, 1, 0));

                a_renerables["fireleviathan"].matrix = m;
            }
        }

        void CreateResources()
        {
            std::cout << "\tcreating sync objects...\n";
            CreateSyncObjects(m_device, &m_sync);

            std::cout << "\tloading assets...\n";
            LoadTextures(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_textures, m_timer); // timer for RANDOM noise texture
            LoadMeshes(  m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_meshes);

            std::cout << "\tcreating attachments...\n";
            CreateAttachments(     m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_attachments);
            CreateShadowmapTexture(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_attachments.shadowCubemap);

            std::cout << "\tcreating descriptor sets...\n";
            CreateTextureOnlyLayout(m_device, &m_DSLayouts.textureOnlyLayout);
            CreateTextureDescriptorPool(m_device, m_DSPools.textureDSPool, m_textures.size() + 1 + 3 + 2 + 2);
            // + 1 for cubemap; + 3 for ssao inputs; + 2 for ssao and blurred ssao; +2 for bloom and blurred bloom
            CreateDSForEachModelTexture(m_device, &m_DSLayouts.textureOnlyLayout, m_DSPools.textureDSPool, m_inputTextures, m_textures);
            CreateDSForOtherInputAttachments(m_device, &m_DSLayouts.textureOnlyLayout, m_DSPools.textureDSPool, m_inputAttachments, m_attachments);

            CreateUBOOnlyLayout(m_device, &m_DSLayouts.uboOnlyLayout);
            CreateUBODescriptorPool(m_device, m_DSPools.uboDSPool, 1); // 1 for ssao sampling kernel
            CreateReadOnlyUBOs(m_device, physicalDevice, m_graphicsQueue, m_commandPool, &m_DSLayouts.uboOnlyLayout, m_DSPools.uboDSPool,
                    m_roUniformBuffers, m_timer);

            std::cout << "\tcreating render passes...\n";
            CreateFinalRenderpass(m_device, &(m_renderPasses.finalRenderPass), m_screen.swapChainImageFormat);
            CreateBloomRenderpass(m_device, &(m_renderPasses.bloomPass));
            CreateGBufferRenderPass(m_device, &(m_renderPasses.gBufferCreationPass));
            CreateSSAORenderPass(m_device, &(m_renderPasses.ssaoPass));
            CreateBlurRenderPass(m_device, &(m_renderPasses.ssaoBlurPass), VK_FORMAT_R32_SFLOAT);
            CreateShadowCubemapRenderPass(m_device, &(m_renderPasses.shadowCubemapPass));

            std::cout << "\tcreating frame buffers...\n";
            CreateScreenFrameBuffers(m_device, m_renderPasses.finalRenderPass, &m_screen, m_attachments);
            CreateBloomFrameBuffer(m_device, m_renderPasses.bloomPass, m_framebuffersOffscreen.bloomFrameBuffer, m_attachments);
            CreateGBufferFrameBuffer(m_device, m_renderPasses.gBufferCreationPass,
                    m_framebuffersOffscreen.gBufferCreationFrameBuffer, m_attachments);
            CreateSSAOFrameBuffer(m_device, m_renderPasses.ssaoPass,
                    m_framebuffersOffscreen.ssaoFrameBuffer, m_attachments);
            CreateSSAOBlurFrameBuffer(m_device, m_renderPasses.ssaoPass,
                    m_framebuffersOffscreen.ssaoBlurFrameBuffer, m_attachments);
            CreateShadowCubemapFrameBuffer(m_device, m_renderPasses.shadowCubemapPass,
                    m_framebuffersOffscreen.shadowCubemapFrameBuffer, m_attachments);

            std::cout << "\tcreating graphics pipelines...\n";
            CreateGraphicsPipelines(m_device, m_screen.swapChainExtent, m_renderPasses, m_pipes, m_DSLayouts);

            std::cout << "\tcreating camera & light...\n";
            CreateEyes(m_pEyes, &m_timer);

            std::cout << "\tcreating particle systems...\n";
            CreateParticleSystem(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_particleSystems, m_inputTextures,
                    &m_timer);

            std::cout << "\tcomposing scene...\n";
            ComposeScene(m_renderables, m_pipes, m_meshes, m_inputTextures);

            std::cout << "\tcreating drawing command buffers...\n";
            CreateDrawCommandBuffers(m_device, m_commandPool, m_screen.swapChainFramebuffers, &m_drawCommandBuffers);
        }


        void MainLoop()
        {
            while (!glfwWindowShouldClose(m_window)) 
            {
                glfwPollEvents();
                m_timer.timeStamp();
                UpdateScene(m_renderables, m_timer.getTime());
                UpdateParticleSystems(m_particleSystems, m_pEyes["light"]->position());
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

        static void CreateBloomRenderpass(VkDevice a_device, VkRenderPass* a_pRenderPass)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format         = VK_FORMAT_R32G32B32A32_SFLOAT;
            colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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
            depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthAttachmentRef{};
            depthAttachmentRef.attachment = 1;
            depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = 1;
            subpass.pColorAttachments       = &colorAttachmentRef;
            subpass.pDepthStencilAttachment = &depthAttachmentRef;

            std::vector<VkSubpassDependency> dependency {
                {
                    VK_SUBPASS_EXTERNAL,
                        0,

                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // -->

                        VK_ACCESS_SHADER_READ_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // ==>

                        VK_DEPENDENCY_BY_REGION_BIT
                },
                    {
                        0,
                        VK_SUBPASS_EXTERNAL,

                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // <--
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,

                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // <==
                        VK_ACCESS_SHADER_READ_BIT,

                        VK_DEPENDENCY_BY_REGION_BIT
                    }
            };

            std::vector<VkAttachmentDescription> attachments {
                colorAttachment, depthAttachment
            };

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = attachments.size();
            renderPassInfo.pAttachments    = attachments.data();
            renderPassInfo.subpassCount    = 1;
            renderPassInfo.pSubpasses      = &subpass;
            renderPassInfo.dependencyCount = dependency.size();
            renderPassInfo.pDependencies   = dependency.data();

            if (vkCreateRenderPass(a_device, &renderPassInfo, nullptr, a_pRenderPass) != VK_SUCCESS)
                throw std::runtime_error("[CreateBloomRenderpass]: failed to create render pass!");
        }

        static void CreateGBufferRenderPass(VkDevice a_device, VkRenderPass* a_pRenderPass)
        {
            std::vector<VkAttachmentDescription> attachmentDescr(3);

            attachmentDescr[0].format = VK_FORMAT_R32G32B32A32_SFLOAT; // vec3(position)float(depth)
            attachmentDescr[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; // vec3(normals)
            attachmentDescr[2].format = VK_FORMAT_D32_SFLOAT;          // depth

            for (size_t i{}; i < 3; ++i)
            {
                attachmentDescr[i].samples        = VK_SAMPLE_COUNT_1_BIT;
                attachmentDescr[i].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachmentDescr[i].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                attachmentDescr[i].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachmentDescr[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachmentDescr[i].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            attachmentDescr[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            std::vector<VkAttachmentReference> colorAttachmentRef {
                {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                    {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            };

            VkAttachmentReference depthAttachmentRef { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = colorAttachmentRef.size();
            subpass.pColorAttachments       = colorAttachmentRef.data();
            subpass.pDepthStencilAttachment = &depthAttachmentRef;

            std::vector<VkSubpassDependency> dependency {
                {
                    VK_SUBPASS_EXTERNAL,
                        0,

                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // -->

                        VK_ACCESS_SHADER_READ_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // ==>

                        VK_DEPENDENCY_BY_REGION_BIT
                },
                    {
                        0,
                        VK_SUBPASS_EXTERNAL,

                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // <--
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,

                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // <==
                        VK_ACCESS_SHADER_READ_BIT,

                        VK_DEPENDENCY_BY_REGION_BIT
                    }
            };

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = attachmentDescr.size();
            renderPassInfo.pAttachments    = attachmentDescr.data();
            renderPassInfo.subpassCount    = 1;
            renderPassInfo.pSubpasses      = &subpass;
            renderPassInfo.dependencyCount = dependency.size();
            renderPassInfo.pDependencies   = dependency.data();

            if (vkCreateRenderPass(a_device, &renderPassInfo, nullptr, a_pRenderPass) != VK_SUCCESS)
                throw std::runtime_error("[CreateGBufferRenderPass]: failed to create render pass!");
        }

        static void CreateBlurRenderPass(VkDevice a_device, VkRenderPass* a_pRenderPass, VkFormat a_format)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format         = a_format;
            colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentReference colorAttachmentRef{};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments    = &colorAttachmentRef;

            std::vector<VkAttachmentDescription> attachments{
                colorAttachment
            };

            std::vector<VkSubpassDependency> dependency {
                {
                    VK_SUBPASS_EXTERNAL,
                        0,

                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // -->

                        VK_ACCESS_MEMORY_READ_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // ==>

                        VK_DEPENDENCY_BY_REGION_BIT
                },
                    {
                        0,
                        VK_SUBPASS_EXTERNAL,

                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // <--
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,

                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // <==
                        VK_ACCESS_MEMORY_READ_BIT,

                        VK_DEPENDENCY_BY_REGION_BIT
                    }
            };

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = attachments.size();
            renderPassInfo.pAttachments    = attachments.data();
            renderPassInfo.subpassCount    = 1;
            renderPassInfo.pSubpasses      = &subpass;
            renderPassInfo.dependencyCount = dependency.size();
            renderPassInfo.pDependencies   = dependency.data();

            if (vkCreateRenderPass(a_device, &renderPassInfo, nullptr, a_pRenderPass) != VK_SUCCESS)
                throw std::runtime_error("[CreateBlurRenderPass]: failed to create render pass!");
        }

        static void CreateSSAORenderPass(VkDevice a_device, VkRenderPass* a_pRenderPass)
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format         = VK_FORMAT_R32_SFLOAT;
            colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentReference colorAttachmentRef{};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass {};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments    = &colorAttachmentRef;

            std::vector<VkAttachmentDescription> attachments{
                colorAttachment
            };

            std::vector<VkSubpassDependency> dependency {
                {
                    VK_SUBPASS_EXTERNAL,
                        0,

                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // -->

                        VK_ACCESS_MEMORY_READ_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // ==>

                        VK_DEPENDENCY_BY_REGION_BIT
                },
                    {
                        0,
                        VK_SUBPASS_EXTERNAL,

                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // <--
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,

                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // <==
                        VK_ACCESS_MEMORY_READ_BIT,

                        VK_DEPENDENCY_BY_REGION_BIT
                    }
            };

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = attachments.size();
            renderPassInfo.pAttachments    = attachments.data();
            renderPassInfo.subpassCount    = 1;
            renderPassInfo.pSubpasses      = &subpass;
            renderPassInfo.dependencyCount = dependency.size();
            renderPassInfo.pDependencies   = dependency.data();

            if (vkCreateRenderPass(a_device, &renderPassInfo, nullptr, a_pRenderPass) != VK_SUCCESS)
                throw std::runtime_error("[CreateSSAORenderPass]: failed to create render pass!");
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

        static void CreateUBOOnlyLayout(VkDevice a_device, VkDescriptorSetLayout *a_pDSLayout)
        {
            VkDescriptorSetLayoutBinding uboLayoutBinding{};
            uboLayoutBinding.binding            = 0;
            uboLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboLayoutBinding.descriptorCount    = 1;
            uboLayoutBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
            uboLayoutBinding.pImmutableSamplers = nullptr;

            std::array<VkDescriptorSetLayoutBinding, 1> binds = {uboLayoutBinding};

            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
            descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCreateInfo.bindingCount = binds.size();
            descriptorSetLayoutCreateInfo.pBindings    = binds.data();

            if (vkCreateDescriptorSetLayout(a_device, &descriptorSetLayoutCreateInfo, nullptr, a_pDSLayout) != VK_SUCCESS)
                throw std::runtime_error("[CreateTextureOnlyLayout]: failed to create DS layout!");
        }

        static void CreateOneUBODescriptorSet(VkDevice a_device, const VkDescriptorSetLayout *a_pDSLayout, VkDescriptorPool& a_DSPool,
                VkDescriptorSet& a_dset, VkBuffer& a_buffer, VkDeviceSize a_bufferSize)
        {
            VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
            descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocateInfo.descriptorPool     = a_DSPool;
            descriptorSetAllocateInfo.descriptorSetCount = 1;
            descriptorSetAllocateInfo.pSetLayouts        = a_pDSLayout;

            if (vkAllocateDescriptorSets(a_device, &descriptorSetAllocateInfo, &a_dset) != VK_SUCCESS)
                throw std::runtime_error("[CreateOneUBODescriptorSet]: failed to allocate descriptor set pool!");

            VkWriteDescriptorSet descrWrite{};
            descrWrite.sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descrWrite.dstSet            = a_dset;
            descrWrite.dstBinding        = 0;
            descrWrite.dstArrayElement   = 0;
            descrWrite.descriptorType    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descrWrite.descriptorCount   = 1;

            VkDescriptorBufferInfo bufferInfo{ a_buffer, 0, a_bufferSize };
            descrWrite.pBufferInfo        = &bufferInfo;

            vkUpdateDescriptorSets(a_device, 1, &descrWrite, 0, nullptr);
        }

        // read only <== one for all frames in flight (READ - READ)
        static void CreateReadOnlyUBOs(VkDevice a_device, VkPhysicalDevice a_physDevice, VkQueue a_queue, VkCommandPool a_pool,
                VkDescriptorSetLayout* a_pDSLayout, VkDescriptorPool& a_dsPool,
                std::unordered_map<std::string, UniformBuffer>& a_UBOs, Timer a_timer)
        {
            a_timer.timeStamp();
            std::default_random_engine randomEngine{ (long unsigned int)a_timer.getTime() };

            auto random = [&](float a_range)
            {
                std::uniform_real_distribution<float> randomDistribution(0.0f, a_range);
                return randomDistribution(randomEngine);
            };

            UniformBuffer ssaoSamplerKernel{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
            // data for ssao sampler kernel ubo
            std::vector<glm::vec4> kernel(SSAO_SAMPLING_KERNEL_SIZE);
            {
                for (auto& sampleVector : kernel)
                {
                    glm::vec3 vec = glm::vec3(random(2.0f) - 1.0f, random(2.0f) - 1.0f, random(1.0f));
                    vec = glm::normalize(vec) * random(1.0f);
                    sampleVector = glm::vec4(vec, 1.0f);
                };
            }

            {
                VkBuffer stagingBuffer{};
                VkDeviceMemory stagingBufferMemory{};
                size_t size{ kernel.size() * sizeof(glm::vec4) };

                CreateHostVisibleBuffer(a_device, a_physDevice, size, &stagingBuffer, &stagingBufferMemory);

                void *mappedMemory = nullptr;
                vkMapMemory(a_device, stagingBufferMemory, 0, size, 0, &mappedMemory);
                memcpy(mappedMemory, kernel.data(), size);
                vkUnmapMemory(a_device, stagingBufferMemory);

                CreateDeviceLocalBuffer(a_device, a_physDevice, size, &ssaoSamplerKernel.buffer, &ssaoSamplerKernel.memory,
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

                SubmitStagingBuffer(a_device, a_pool, a_queue, stagingBuffer, ssaoSamplerKernel.buffer, size);

                vkFreeMemory(a_device, stagingBufferMemory, nullptr);
                vkDestroyBuffer(a_device, stagingBuffer, nullptr);

                CreateOneUBODescriptorSet(a_device, a_pDSLayout, a_dsPool, ssaoSamplerKernel.descriptorSet, ssaoSamplerKernel.buffer, size);
            }

            a_UBOs["ssao kernel"] = ssaoSamplerKernel;
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
        static void CreateUBODescriptorPool(VkDevice a_device, VkDescriptorPool& a_dsPool, uint32_t a_count)
        {
            VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, a_count };

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{};
            descriptorPoolCreateInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCreateInfo.maxSets       = a_count;
            descriptorPoolCreateInfo.poolSizeCount = 1;
            descriptorPoolCreateInfo.pPoolSizes    = &poolSize;

            if (vkCreateDescriptorPool(a_device, &descriptorPoolCreateInfo, nullptr, &a_dsPool) != VK_SUCCESS)
                throw std::runtime_error("[CreateUBODescriptorPool]: failed to create descriptor set pool!");
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
                throw std::runtime_error("[CreateTextureDescriptorPool]: failed to create descriptor set pool!");
        }

        static void CreateDSForEachModelTexture(VkDevice a_device, VkDescriptorSetLayout* a_pDSLayout, VkDescriptorPool& a_dsPool,
                std::unordered_map<std::string, InputTexture>& a_inputTextures, std::unordered_map<std::string, Texture>& a_textures)
        {
            for (auto& texture : a_textures)
            {
                Texture* pTextureObj{ &texture.second };
                InputTexture inputTexture{ pTextureObj, VK_NULL_HANDLE };

                CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, inputTexture.descriptorSet, pTextureObj->getImageView(), pTextureObj->getSampler());

                a_inputTextures[texture.first] = inputTexture;
            }
            
            s_blackTexutreDS = a_inputTextures["black"].descriptorSet;
        }

        static void CreateDSForOtherInputAttachments(VkDevice a_device, VkDescriptorSetLayout* a_pDSLayout, VkDescriptorPool& a_dsPool,
                InputAttachments& a_inputAttachments, Attachments& a_attachments)

        {
            CubeTexture* pCubemapTexture{ &a_attachments.shadowCubemap };
            a_inputAttachments.shadowCubemap = InputCubeTexture{ pCubemapTexture, VK_NULL_HANDLE };
            CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, a_inputAttachments.shadowCubemap.descriptorSet,
                    pCubemapTexture->getImageView(), pCubemapTexture->getSampler());

            Texture* pPositionAndDepth{ &a_attachments.gPositionAndDepth };
            a_inputAttachments.gPositionAndDepth = InputTexture{ pPositionAndDepth, VK_NULL_HANDLE };
            CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, a_inputAttachments.gPositionAndDepth.descriptorSet,
                    pPositionAndDepth->getImageView(), pPositionAndDepth->getSampler());

            Texture* pNormals{ &a_attachments.gNormals };
            a_inputAttachments.gNormals = InputTexture{ pNormals, VK_NULL_HANDLE };
            CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, a_inputAttachments.gNormals.descriptorSet,
                    pNormals->getImageView(), pNormals->getSampler());

            Texture* pSSAO{ &a_attachments.ssao };
            a_inputAttachments.ssao = InputTexture{ pSSAO, VK_NULL_HANDLE };
            CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, a_inputAttachments.ssao.descriptorSet,
                    pSSAO->getImageView(), pSSAO->getSampler());

            Texture* pSSAOBlur{ &a_attachments.blurredSSAO };
            a_inputAttachments.blurredSSAO = InputTexture{ pSSAOBlur, VK_NULL_HANDLE };
            CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, a_inputAttachments.blurredSSAO.descriptorSet,
                    pSSAOBlur->getImageView(), pSSAOBlur->getSampler());

            Texture* pBloom{ &a_attachments.bloom };
            a_inputAttachments.bloom = InputTexture{ pBloom, VK_NULL_HANDLE };
            CreateOneImageDescriptorSet(a_device, a_pDSLayout, a_dsPool, a_inputAttachments.bloom.descriptorSet,
                    pBloom->getImageView(), pBloom->getSampler());
        }

        static void CreateGraphicsPipelines(VkDevice a_device, VkExtent2D a_screenExtent, RenderPasses a_renderPasses,
                std::unordered_map<std::string, Pipe>& a_pipes, DSLayouts a_dsLayouts)
        {
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
            rasterizer.cullMode                = VK_CULL_MODE_BACK_BIT;
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
            std::vector<VkDescriptorSetLayout> sceneDSLayouts{
                a_dsLayouts.textureOnlyLayout,      // texture sapmler (for models)
                    a_dsLayouts.textureOnlyLayout,  // shadow map
                    a_dsLayouts.textureOnlyLayout   // ssao map
            };
            createPipeline("scene", sceneDSLayouts, "scene", a_renderPasses.finalRenderPass);

            std::vector<VkDescriptorSetLayout> bloomDSLayouts{
                a_dsLayouts.textureOnlyLayout // texture sapmler (for models)
            };
            createPipeline("bloom", bloomDSLayouts, "bloom", a_renderPasses.bloomPass);

            // fill gbuffer ////////////////////////////////////////////////////////////
            std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates(2);
            for (auto& blend : blendAttachmentStates)
            {
                blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                blend.blendEnable    = VK_FALSE;
            }

            colorBlending.attachmentCount = blendAttachmentStates.size();
            colorBlending.pAttachments    = blendAttachmentStates.data();

            std::vector<VkDescriptorSetLayout> gBufferDSLayouts(0);
            createPipeline("g buffer", gBufferDSLayouts, "gbuffer", a_renderPasses.gBufferCreationPass);

            blendAttachmentStates = std::vector<VkPipelineColorBlendAttachmentState>(0);
            colorBlending.attachmentCount   = 1;
            colorBlending.pAttachments      = &colorBlendAttachment;

            // render to cubemap face //////////////////////////////////////////////////
            std::vector<VkDescriptorSetLayout> shadowCubemapDSLayout(0);
            createPipeline("shadow cubemap", shadowCubemapDSLayout, "shadowmap", a_renderPasses.shadowCubemapPass);

            rasterizer.cullMode = VK_CULL_MODE_NONE;
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

            // calculate ssao //////////////////////////////////////////////////////////
            std::vector<VkDescriptorSetLayout> ssaoDSLayout{
                a_dsLayouts.textureOnlyLayout, // position
                    a_dsLayouts.textureOnlyLayout, // normals
                    a_dsLayouts.textureOnlyLayout, // noise
                    a_dsLayouts.uboOnlyLayout      // full of sampling vectors
            };

            createPipeline("ssao", ssaoDSLayout, "ssao", a_renderPasses.ssaoPass);

            // blur ssao ///////////////////////////////////////////////////////////////
            std::vector<VkDescriptorSetLayout> ssaoBlurDSLayout{
                a_dsLayouts.textureOnlyLayout  // ssao
            };

            createPipeline("blur ssao", ssaoBlurDSLayout, "blur", a_renderPasses.ssaoBlurPass); //TODO:

            // blur bloom //////////////////////////////////////////////////////////////
            std::vector<VkDescriptorSetLayout> bloomBlurDSLayout{
                a_dsLayouts.textureOnlyLayout  // bloom
            };

            depthAndStencil.depthWriteEnable         = VK_FALSE;
            colorBlendAttachment.blendEnable         = VK_TRUE;

            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

            colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

            createPipeline("blur bloom", bloomBlurDSLayout, "gauss", a_renderPasses.finalRenderPass);

            // render particle system //////////////////////////////////////////////////
            vertexDescr = ParticleSystem::getVertexDescription();
            vertexInputInfo = VkPipelineVertexInputStateCreateInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount   = vertexDescr.bindings.size();
            vertexInputInfo.vertexAttributeDescriptionCount = vertexDescr.attributes.size();
            vertexInputInfo.pVertexBindingDescriptions      = vertexDescr.bindings.data();
            vertexInputInfo.pVertexAttributeDescriptions    = vertexDescr.attributes.data();

            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

            inputAssembly.topology                   = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

            std::vector<VkDescriptorSetLayout> particleSystemDSLayout{ a_dsLayouts.textureOnlyLayout };
            createPipeline("particle system", particleSystemDSLayout, "particle", a_renderPasses.finalRenderPass);
        }

        static void CreateEyes(std::unordered_map<std::string, Eye*>& a_eyes, Timer* a_pTimer)
        {
            Camera* camera = new Camera{a_pTimer};
            a_eyes["camera"] = camera;

            Light* light = new Light{a_pTimer};
            a_eyes["light"] = light;
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

        static void CreateBloomFrameBuffer(VkDevice a_device, VkRenderPass a_renderPass, VkFramebuffer& a_frameBuffer, Attachments& a_attachments)
        {
            std::vector<VkImageView> attachments {
                a_attachments.bloom.getImageView(),
                    a_attachments.bloomDepth.getImageView()
            };

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = a_renderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments    = attachments.data();
            framebufferInfo.width           = BLOOM_DIM;
            framebufferInfo.height          = BLOOM_DIM;
            framebufferInfo.layers          = 1;

            if (vkCreateFramebuffer(a_device, &framebufferInfo, nullptr, &a_frameBuffer) != VK_SUCCESS)
                throw std::runtime_error("failed to create framebuffer!");
        }


        static void CreateSSAOFrameBuffer(VkDevice a_device, VkRenderPass a_renderPass, VkFramebuffer& a_frameBuffer, Attachments& a_attachments)
        {
            std::vector<VkImageView> attachments {
                a_attachments.ssao.getImageView(),
            };

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = a_renderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments    = attachments.data();
            framebufferInfo.width           = WIDTH;
            framebufferInfo.height          = HEIGHT;
            framebufferInfo.layers          = 1;

            if (vkCreateFramebuffer(a_device, &framebufferInfo, nullptr, &a_frameBuffer) != VK_SUCCESS)
                throw std::runtime_error("failed to create framebuffer!");
        }

        static void CreateSSAOBlurFrameBuffer(VkDevice a_device, VkRenderPass a_renderPass, VkFramebuffer& a_frameBuffer, Attachments& a_attachments)
        {
            std::vector<VkImageView> attachments {
                a_attachments.blurredSSAO.getImageView(),
            };

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = a_renderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments    = attachments.data();
            framebufferInfo.width           = WIDTH;
            framebufferInfo.height          = HEIGHT;
            framebufferInfo.layers          = 1;

            if (vkCreateFramebuffer(a_device, &framebufferInfo, nullptr, &a_frameBuffer) != VK_SUCCESS)
                throw std::runtime_error("failed to create framebuffer!");
        }

        static void CreateGBufferFrameBuffer(VkDevice a_device, VkRenderPass a_renderPass, VkFramebuffer& a_frameBuffer, Attachments& a_attachments)
        {
            std::vector<VkImageView> attachments {
                a_attachments.gPositionAndDepth.getImageView(),
                    a_attachments.gNormals.getImageView(),
                    a_attachments.presentDepth.getImageView()
            };

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = a_renderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments    = attachments.data();
            framebufferInfo.width           = WIDTH;
            framebufferInfo.height          = HEIGHT;
            framebufferInfo.layers          = 1;

            if (vkCreateFramebuffer(a_device, &framebufferInfo, nullptr, &a_frameBuffer) != VK_SUCCESS)
                throw std::runtime_error("failed to create framebuffer!");
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
                std::unordered_map<std::string, Pipe>& a_pipes, Eye* a_eye)
        {
            VkPipeline&       pipeline = a_pipes["particle system"].pipeline;
            VkPipelineLayout& layout   = a_pipes["particle system"].pipelineLayout;

            for (auto& particleSystem : a_particleSystems)
            {
                auto& system{ particleSystem.second };

                vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

                vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &(system.getTexture()->descriptorSet), 0, nullptr);

                PushConstants constants{};
                constants.model      = glm::mat4(1.0f);
                constants.view       = a_eye->view(0);
                constants.projection = a_eye->projection();

                vkCmdPushConstants(a_cmdBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &constants);

                VkDeviceSize offsets[1] = { 0 };
                vkCmdBindVertexBuffers(a_cmdBuffer, 0, 1, &(system.getVBO()), offsets);

                vkCmdDraw(a_cmdBuffer, system.getParticleCount(), 1, 0, 0);
            }
        }

        static void RecordCommandsOfDrawingBloomedParts(VkFramebuffer a_frameBuffer, VkRenderPass a_renderPass, Pipe& a_pipe,
                VkCommandBuffer a_cmdBuff, std::unordered_map<std::string, RenderObject>& a_objects, Eye* a_camera)
        {
            VkClearValue colorClear;
            colorClear.color = { {  0.0f, 0.0f, 0.0f, 0.0f } };

            VkClearValue depthClear;
            depthClear.depthStencil.depth = 1.f;

            std::vector<VkClearValue> clearValues{ colorClear, depthClear };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = a_renderPass;
            renderPassInfo.framebuffer       = a_frameBuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = { (uint32_t)BLOOM_DIM, (uint32_t)BLOOM_DIM };
            renderPassInfo.clearValueCount   = clearValues.size();
            renderPassInfo.pClearValues      = clearValues.data();

            SetViewportAndScissor(a_cmdBuff, (float)BLOOM_DIM, (float)BLOOM_DIM, true);

            vkCmdBeginRenderPass(a_cmdBuff, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            RecordCommandsOfDrawingRenderables(a_objects, a_cmdBuff, &a_pipe, a_camera, glm::vec3(1.0f),
                    InputCubeTexture{}, InputTexture{}, 0, true, true);

            vkCmdEndRenderPass(a_cmdBuff);
        }

        static void RecordCommandsOfDrawingRenderables(std::unordered_map<std::string, RenderObject> a_objects, VkCommandBuffer a_cmdBuffer,
                const Pipe* a_specialPipeline, Eye* a_eye, glm::vec3 a_lightPos, InputCubeTexture a_shadowCubemap, InputTexture a_SSAOmap,
                uint32_t a_face, bool a_bindTextures, bool a_glowingOnly)
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

                std::vector<VkDescriptorSet> setsToBind(0);

                if (a_glowingOnly) // scene shader
                {
                    if (obj.bloom)
                    {
                        setsToBind.push_back(obj.texture->descriptorSet); // #0
                    }
                    else
                    {
                        setsToBind.push_back(s_blackTexutreDS); // #0
                    }
                }
                else
                {
                    if (a_bindTextures)
                    {
                        setsToBind.push_back(obj.texture->descriptorSet); // #0
                    }
                    if (a_shadowCubemap.shadowCubemap != nullptr)
                    {
                        setsToBind.push_back(a_shadowCubemap.descriptorSet); // #1
                        if (a_SSAOmap.texture != nullptr)
                        {
                            setsToBind.push_back(a_SSAOmap.descriptorSet); // #2
                        }
                    }
                }

                if (setsToBind.size())
                {
                    vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pLayout, 0,
                            setsToBind.size(),
                            setsToBind.data(),
                            0, nullptr);
                }

                PushConstants constants{};
                constants.model      = obj.matrix;
                constants.view       = a_eye->view(a_face);
                constants.projection = a_eye->projection();
                constants.lightPos   = a_lightPos;

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

        static void RecordCommandsOfFillingGBuffer(VkFramebuffer a_frameBuffer, VkRenderPass a_renderPass, Pipe a_pipe,
                VkCommandBuffer a_cmdBuff, const std::unordered_map<std::string, RenderObject>& a_objects, Eye* a_camera)
        {
            std::vector<VkClearValue> clearValues(3);
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[2].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = a_renderPass;
            renderPassInfo.framebuffer       = a_frameBuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = { (uint32_t)WIDTH, (uint32_t)HEIGHT };
            renderPassInfo.clearValueCount   = clearValues.size();
            renderPassInfo.pClearValues      = clearValues.data();

            vkCmdBeginRenderPass(a_cmdBuff, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            RecordCommandsOfDrawingRenderables(a_objects, a_cmdBuff, &a_pipe, a_camera, glm::vec3(0.0f), InputCubeTexture{},
                    InputTexture{}, 0, false, false);

            vkCmdEndRenderPass(a_cmdBuff);
        }

        static void RecordCommandsOfSSAOEvaluation(VkDevice a_device, VkRenderPass a_renderPass, VkFramebuffer a_frameBuffer,
                Mesh a_squareMesh, VkCommandBuffer a_cmdBuffer, Pipe a_pipe, InputAttachments& a_attachments,
                InputTexture& a_noiceTexture, UniformBuffer& a_ssaoKernel, glm::mat4 a_projMatrix)
        {
            std::vector<VkClearValue> clearValues(2);
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = a_renderPass;
            renderPassInfo.framebuffer       = a_frameBuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = { (uint32_t)WIDTH, (uint32_t)HEIGHT };
            renderPassInfo.clearValueCount   = clearValues.size();
            renderPassInfo.pClearValues      = clearValues.data();

            vkCmdBeginRenderPass(a_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_pipe.pipeline);

            std::vector<VkDescriptorSet> setsToBind(0);
            setsToBind.push_back(a_attachments.gPositionAndDepth.descriptorSet);
            setsToBind.push_back(a_attachments.gNormals.descriptorSet);
            setsToBind.push_back(a_noiceTexture.descriptorSet);
            setsToBind.push_back(a_ssaoKernel.descriptorSet);

            vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_pipe.pipelineLayout, 0,
                    setsToBind.size(), setsToBind.data(), 0, nullptr);

            VkBuffer vbo{ a_squareMesh.getVBO().buffer };
            VkBuffer ibo{ a_squareMesh.getIBO().buffer };

            std::vector<VkDeviceSize> offsets{ 0 };

            vkCmdBindVertexBuffers(a_cmdBuffer, 0, 1, &vbo, offsets.data());
            vkCmdBindIndexBuffer(a_cmdBuffer, ibo, 0, VK_INDEX_TYPE_UINT32);

            PushConstants constants{};
            constants.projection = a_projMatrix;

            vkCmdPushConstants(a_cmdBuffer, a_pipe.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &constants);

            vkCmdDrawIndexed(a_cmdBuffer, 6, 1, 0, 0, 0);

            vkCmdEndRenderPass(a_cmdBuffer);
        }

        static void RecordCommandsOfBluringSSAO(VkDevice a_device, VkRenderPass a_renderPass, VkFramebuffer a_frameBuffer,
                Mesh a_squareMesh, VkCommandBuffer a_cmdBuffer, Pipe a_pipe, InputTexture& a_ssao)
        {
            std::vector<VkClearValue> clearValues(2);
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = a_renderPass;
            renderPassInfo.framebuffer       = a_frameBuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = { (uint32_t)WIDTH, (uint32_t)HEIGHT };
            renderPassInfo.clearValueCount   = clearValues.size();
            renderPassInfo.pClearValues      = clearValues.data();

            vkCmdBeginRenderPass(a_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_pipe.pipeline);

            std::vector<VkDescriptorSet> setsToBind{ a_ssao.descriptorSet };

            vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_pipe.pipelineLayout, 0,
                    setsToBind.size(), setsToBind.data(), 0, nullptr);

            VkBuffer vbo{ a_squareMesh.getVBO().buffer };
            VkBuffer ibo{ a_squareMesh.getIBO().buffer };

            std::vector<VkDeviceSize> offsets{ 0 };

            vkCmdBindVertexBuffers(a_cmdBuffer, 0, 1, &vbo, offsets.data());
            vkCmdBindIndexBuffer(a_cmdBuffer, ibo, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(a_cmdBuffer, 6, 1, 0, 0, 0);

            vkCmdEndRenderPass(a_cmdBuffer);
        }

        static void RecordCommandsOfBluringBloom(Mesh a_squareMesh, VkCommandBuffer a_cmdBuffer, Pipe a_pipe, InputTexture& a_bloom)
        {
            vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_pipe.pipeline);

            std::vector<VkDescriptorSet> setsToBind{ a_bloom.descriptorSet };

            vkCmdBindDescriptorSets(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_pipe.pipelineLayout, 0,
                    setsToBind.size(), setsToBind.data(), 0, nullptr);

            VkBuffer vbo{ a_squareMesh.getVBO().buffer };
            VkBuffer ibo{ a_squareMesh.getIBO().buffer };

            std::vector<VkDeviceSize> offsets{ 0 };

            vkCmdBindVertexBuffers(a_cmdBuffer, 0, 1, &vbo, offsets.data());
            vkCmdBindIndexBuffer(a_cmdBuffer, ibo, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(a_cmdBuffer, 6, 1, 0, 0, 0);
        }

        static void RecordCommandsToRenderForCubemapFace(VkFramebuffer a_frameBuffer, VkRenderPass a_renderPass, Pipe a_pipe,
                const uint32_t a_face, VkCommandBuffer a_cmdBuff, const std::unordered_map<std::string, RenderObject>& a_objects,
                Eye* a_light)
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

            RecordCommandsOfDrawingRenderables(a_objects, a_cmdBuff, &a_pipe, a_light, a_light->position(), InputCubeTexture{},
                    InputTexture{}, a_face, false, false);

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

        void RecordDrawingBuffer(VkFramebuffer a_swapChainFramebuffer, VkCommandBuffer a_cmdBuffer)
        {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            if (vkBeginCommandBuffer(a_cmdBuffer, &beginInfo) != VK_SUCCESS) 
                throw std::runtime_error("[CreateCommandPoolAndBuffers]: failed to begin recording command buffer!");

            SetViewportAndScissor(a_cmdBuffer, (float)CUBE_SIDE, (float)CUBE_SIDE, true);

            for (uint32_t face{}; face < 6; ++face)
            {
                RecordCommandsToRenderForCubemapFace(m_framebuffersOffscreen.shadowCubemapFrameBuffer, m_renderPasses.shadowCubemapPass,
                        m_pipes["shadow cubemap"], face, a_cmdBuffer, m_renderables, m_pEyes["light"]);
                RecordCommandsOfCopyingToCubemapFace(face, a_cmdBuffer, m_attachments.offscreenColor, m_inputAttachments.shadowCubemap.shadowCubemap);
            }

            // SSAO
            {
                SetViewportAndScissor(a_cmdBuffer, (float)WIDTH, (float)HEIGHT, true);
                RecordCommandsOfFillingGBuffer(m_framebuffersOffscreen.gBufferCreationFrameBuffer, m_renderPasses.gBufferCreationPass,
                        m_pipes["g buffer"], a_cmdBuffer, m_renderables, m_pEyes["camera"]);
                RecordCommandsOfSSAOEvaluation(m_device, m_renderPasses.ssaoPass, m_framebuffersOffscreen.ssaoFrameBuffer, m_meshes["quad"],
                        a_cmdBuffer, m_pipes["ssao"], m_inputAttachments, m_inputTextures["noise"], m_roUniformBuffers["ssao kernel"],
                        m_pEyes["camera"]->projection());
                RecordCommandsOfBluringSSAO(m_device, m_renderPasses.ssaoBlurPass, m_framebuffersOffscreen.ssaoBlurFrameBuffer, m_meshes["quad"],
                        a_cmdBuffer, m_pipes["blur ssao"], m_inputAttachments.ssao);
            }

            // BLOOM
            RecordCommandsOfDrawingBloomedParts(m_framebuffersOffscreen.bloomFrameBuffer, m_renderPasses.bloomPass,
                    m_pipes["bloom"], a_cmdBuffer, m_renderables, m_pEyes["camera"]);

            VkClearValue colorClear;
            colorClear.color = { {  0.0f, 0.0f, 0.0f, 1.0f } };

            VkClearValue depthClear;
            depthClear.depthStencil.depth = 1.f;

            std::vector<VkClearValue> clearValues{ colorClear, depthClear };

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass        = m_renderPasses.finalRenderPass;
            renderPassInfo.framebuffer       = a_swapChainFramebuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = m_screen.swapChainExtent;
            renderPassInfo.clearValueCount   = clearValues.size();
            renderPassInfo.pClearValues      = clearValues.data();

            SetViewportAndScissor(a_cmdBuffer, (float)WIDTH, (float)HEIGHT, true);

            vkCmdBeginRenderPass(a_cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            if (s_shadowmapDebug)
            {
                RecordCommandsOfShowingCubemap(m_device, m_meshes["quad"], a_cmdBuffer, &m_pipes["show cubemap"], m_inputAttachments.shadowCubemap);
            }
            else
            {
                RecordCommandsOfDrawingRenderables(m_renderables, a_cmdBuffer, nullptr, m_pEyes["camera"], m_pEyes["light"]->position(),
                        m_inputAttachments.shadowCubemap,
                        (s_ssaoEnabled) ? m_inputAttachments.blurredSSAO : m_inputTextures["white"],
                        0, true, false);
                RecordCommandsOfDrawingParticleSystems(m_particleSystems, a_cmdBuffer, m_pipes, m_pEyes["camera"]);

                if (s_bloomEnabled)
                {
                    RecordCommandsOfBluringBloom(m_meshes["quad"], a_cmdBuffer, m_pipes["blur bloom"], m_inputAttachments.bloom);
                }
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
                ps.second.updateParticles(a_emmiterPos);
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

                // SSAO - color attachments
                Texture& gBufferPD = a_attachments.gPositionAndDepth;
                gBufferPD.setAddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
                gBufferPD.setExtent(VkExtent3D{uint32_t(WIDTH), uint32_t(HEIGHT), 1});
                gBufferPD.create(a_device, a_physDevice, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R32G32B32A32_SFLOAT);

                imgBar = gBufferPD.makeBarrier(gBufferPD.wholeImageRange(), 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                gBufferPD.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

                Texture& gBufferN = a_attachments.gNormals;
                gBufferN.setAddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
                gBufferN.setExtent(VkExtent3D{uint32_t(WIDTH), uint32_t(HEIGHT), 1});
                gBufferN.create(a_device, a_physDevice, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R32G32B32A32_SFLOAT);

                imgBar = gBufferN.makeBarrier(gBufferN.wholeImageRange(), 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                gBufferN.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

                Texture& ssao = a_attachments.ssao;
                ssao.setAddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
                ssao.setExtent(VkExtent3D{uint32_t(WIDTH), uint32_t(HEIGHT), 1});
                ssao.create(a_device, a_physDevice, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R32_SFLOAT);

                imgBar = ssao.makeBarrier(ssao.wholeImageRange(), 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                ssao.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

                Texture& blurredSSAO = a_attachments.blurredSSAO;
                blurredSSAO.setExtent(VkExtent3D{uint32_t(WIDTH), uint32_t(HEIGHT), 1});
                blurredSSAO.create(a_device, a_physDevice, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R32_SFLOAT);

                imgBar = blurredSSAO.makeBarrier(blurredSSAO.wholeImageRange(), 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                blurredSSAO.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

                // Bloom - color attachments + depth attachment

                Texture& bloom = a_attachments.bloom;
                bloom.setAddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
                bloom.setExtent(VkExtent3D{uint32_t(BLOOM_DIM), uint32_t(BLOOM_DIM), 1});
                bloom.create(a_device, a_physDevice, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FORMAT_R32G32B32A32_SFLOAT);

                imgBar = bloom.makeBarrier(bloom.wholeImageRange(), 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                bloom.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

                Texture& bloomDepth = a_attachments.bloomDepth;
                bloomDepth.setExtent(VkExtent3D{uint32_t(BLOOM_DIM), uint32_t(BLOOM_DIM), 1});
                bloomDepth.create(a_device, a_physDevice, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_D32_SFLOAT);

                imgBar = bloomDepth.makeBarrier(bloomDepth.wholeImageRange(), 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                bloomDepth.changeImageLayout(cmdBuff, imgBar, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

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

            RecordDrawingBuffer(m_screen.swapChainFramebuffers[imageIndex], m_drawCommandBuffers[imageIndex]);

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

            for (auto& ubo : m_roUniformBuffers)
            {
                vkDestroyBuffer(m_device, ubo.second.buffer, nullptr);
                vkFreeMemory   (m_device, ubo.second.memory, nullptr);
            }

            m_attachments.shadowCubemap.cleanup();
            m_attachments.bloom.cleanup();
            m_attachments.bloomDepth.cleanup();
            m_attachments.presentDepth.cleanup();
            m_attachments.offscreenDepth.cleanup();
            m_attachments.offscreenColor.cleanup();
            m_attachments.gPositionAndDepth.cleanup();
            m_attachments.gNormals.cleanup();
            m_attachments.ssao.cleanup();
            m_attachments.blurredSSAO.cleanup();

            for (auto pipe : m_pipes)
            {
                vkDestroyPipeline      (m_device, pipe.second.pipeline, nullptr);
                vkDestroyPipelineLayout(m_device, pipe.second.pipelineLayout, nullptr);
            }

            vkDestroyDescriptorPool(m_device, m_DSPools.textureDSPool, nullptr);
            vkDestroyDescriptorPool(m_device, m_DSPools.uboDSPool, nullptr);
            vkDestroyDescriptorSetLayout(m_device, m_DSLayouts.textureOnlyLayout, nullptr);
            vkDestroyDescriptorSetLayout(m_device, m_DSLayouts.uboOnlyLayout, nullptr);

            vkDestroyRenderPass(m_device, m_renderPasses.finalRenderPass, nullptr);
            vkDestroyRenderPass(m_device, m_renderPasses.shadowCubemapPass, nullptr);
            vkDestroyRenderPass(m_device, m_renderPasses.ssaoPass, nullptr);
            vkDestroyRenderPass(m_device, m_renderPasses.ssaoBlurPass, nullptr);
            vkDestroyRenderPass(m_device, m_renderPasses.gBufferCreationPass, nullptr);
            vkDestroyRenderPass(m_device, m_renderPasses.bloomPass, nullptr);

            for (auto& eyePtr : m_pEyes)
            {
                free(eyePtr.second);
            }

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
            vkDestroyFramebuffer(m_device, m_framebuffersOffscreen.ssaoFrameBuffer, nullptr);
            vkDestroyFramebuffer(m_device, m_framebuffersOffscreen.ssaoBlurFrameBuffer, nullptr);
            vkDestroyFramebuffer(m_device, m_framebuffersOffscreen.gBufferCreationFrameBuffer, nullptr);
            vkDestroyFramebuffer(m_device, m_framebuffersOffscreen.bloomFrameBuffer, nullptr);

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

bool Application::s_shadowmapDebug;
bool Application::s_ssaoEnabled{true};
bool Application::s_bloomEnabled{true};
VkDescriptorSet Application::s_blackTexutreDS;

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
