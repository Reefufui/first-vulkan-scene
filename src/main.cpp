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

const int WIDTH  = 800;
const int HEIGHT = 600;

const int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char*> deviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

class HelloTriangleApplication 
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
        VkPipelineLayout m_pipelineLayout;
        VkPipeline       m_graphicsPipeline;

        VkCommandPool                m_commandPool;
        std::vector<VkCommandBuffer> m_commandBuffers;

        // buffers
        VkBuffer       m_vbo, m_ibo;
        VkDeviceMemory m_vboMem, m_iboMem;

        //images
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
        unsigned m_frameNumber{};

        // Meshes
        std::vector<Mesh> m_meshes{};

        ////////////////////////////////////////////////////////

        struct Material {
            VkPipeline pipeline;
            VkPipelineLayout pipelineLayout;
        };

        struct RenderObject {
            Mesh* mesh;
            Material* material;
            glm::mat4 transformMatrix;
        };

        std::vector<RenderObject> m_renerables;
        std::unordered_map<std::string, Material> m_materials;
        std::unordered_map<std::string, Mesh> _m_meshes;

        Material* createMaterial(VkPipeline a_pipeline, VkPipelineLayout a_layout, const std::string& name)
        {
            m_materials[name] = Material{ a_pipeline, a_layout };
            return &m_materials[name];
        }

        Material* getMaterial(const std::string& name)
        {
            auto found{ m_materials.find(name) };

            if (found != m_materials.end())
            {
                return &(*found).second;
            }
            else
            {
                return nullptr;
            }
        }

        Mesh* getMesh(const std::string& name)
        {
            auto found{ _m_meshes.find(name) };

            if (found != _m_meshes.end())
            {
                return &(*found).second;
            }
            else
            {
                return nullptr;
            }

            return nullptr;
        }

        void drawObjects(VkCommandBuffer a_cmdBuffer, std::vector<RenderObject> a_objects)
        {
        }

        ////////////////////////////////////////////////////////

        void InitWindow() 
        {
            glfwInit();

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

            m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
        }

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


        void InitVulkan() 
        {
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

            // ==> commadnPool
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

        static void LoadMesh(VkDevice a_device, VkPhysicalDevice a_physDevice, VkCommandPool a_pool, VkQueue a_queue, Mesh& a_mesh, std::string a_filename)
        {
            a_mesh.loadFromOBJ(a_filename.c_str());

            //TODO: vertex normals & texture coordinates

            CreateVertexBuffer(a_device, a_physDevice, a_mesh.vertices.size() * sizeof(Vertex), &a_mesh.getVBO().buffer, &a_mesh.getVBO().memory);

            void *mappedMemory = nullptr;

            vkMapMemory(a_device, a_mesh.getVBO().memory, 0, a_mesh.vertices.size() * sizeof(Vertex), 0, &mappedMemory);
            memcpy(mappedMemory, a_mesh.vertices.data(), a_mesh.vertices.size() * sizeof(Vertex));
            vkUnmapMemory(a_device, a_mesh.getVBO().memory);
        }

        void CreateResources()
        {
            std::cout << "\tcreating render pass...\n";
            CreateRenderPass(m_device, m_screen.swapChainImageFormat, &m_renderPass);

            std::cout << "\tcreating graphics pipeline...\n";
            CreateGraphicsPipeline(m_device, m_screen.swapChainExtent, m_renderPass, &m_pipelineLayout, &m_graphicsPipeline);

            std::cout << "\tcreating depth image...\n";
            CreateDepthImage(m_device, physicalDevice, m_depthImage, m_depthImageMemory, m_depthImageView);

            std::cout << "\tcreating frame buffers...\n";
            CreateScreenFrameBuffers(m_device, m_renderPass, &m_screen, m_depthImageView);

            std::cout << "\tcreating sync objects...\n";
            CreateSyncObjects(m_device, &m_sync);

            std::cout << "\tloading meshes...\n";

            std::vector<std::string> filenames{
                "assets/models/teapot.obj"
            };

            for (auto filename : filenames)
            {
                m_meshes.push_back(Mesh());
                LoadMesh(m_device, physicalDevice, m_commandPool, m_graphicsQueue, m_meshes.back(), filename);
            }

            std::cout << "\tcreating command buffers...\n";
            CreateCommandBuffers(m_device, m_commandPool, m_screen.swapChainFramebuffers, &m_commandBuffers);
        }


        void MainLoop()
        {
            while (!glfwWindowShouldClose(m_window)) 
            {
                glfwPollEvents();
                DrawFrame();
            }

            vkDeviceWaitIdle(m_device);
        }

        void Cleanup() 
        { 
            // free our meshes
            for (auto mesh : m_meshes)
            {
                mesh.setDevice(m_device);
                mesh.cleanup();
            }

            // free depth image resources
            vkFreeMemory      (m_device, m_depthImageMemory, NULL);
            vkDestroyImage    (m_device, m_depthImage,       NULL);
            vkDestroyImageView(m_device, m_depthImageView,   NULL);

            if (enableValidationLayers)
            {
                // destroy callback.
                auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugReportCallbackEXT");
                if (func == nullptr)
                    throw std::runtime_error("Could not load vkDestroyDebugReportCallbackEXT");
                func(m_instance, debugReportCallback, NULL);
            }

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
            {
                vkDestroySemaphore(m_device, m_sync.renderFinishedSemaphores[i], nullptr);
                vkDestroySemaphore(m_device, m_sync.imageAvailableSemaphores[i], nullptr);
                vkDestroyFence    (m_device, m_sync.inFlightFences[i], nullptr);
            }

            vkDestroyCommandPool(m_device, m_commandPool, nullptr);

            for (auto framebuffer : m_screen.swapChainFramebuffers) {
                vkDestroyFramebuffer(m_device, framebuffer, nullptr);
            }

            vkDestroyPipeline      (m_device, m_graphicsPipeline, nullptr);
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
            vkDestroyRenderPass    (m_device, m_renderPass, nullptr);

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

        static void CreateGraphicsPipeline(VkDevice a_device, VkExtent2D a_screenExtent, VkRenderPass a_renderPass,
                VkPipelineLayout* a_pLayout, VkPipeline* a_pPipiline)
        {
            //auto vertShaderCode = vk_utils::ReadFile("shaders/vertex.vert.spv");
            auto vertShaderCode = vk_utils::ReadFile("shaders/tri_mesh.vert.spv");
            auto fragShaderCode = vk_utils::ReadFile("shaders/fragment.frag.spv");

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
            pipelineLayoutInfo.setLayoutCount         = 0;
            pipelineLayoutInfo.pushConstantRangeCount = pushConstants.size();
            pipelineLayoutInfo.pPushConstantRanges    = pushConstants.data();

            if (vkCreatePipelineLayout(a_device, &pipelineLayoutInfo, nullptr, a_pLayout) != VK_SUCCESS)
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
            pipelineInfo.layout              = (*a_pLayout);
            pipelineInfo.renderPass          = a_renderPass;
            pipelineInfo.subpass             = 0;
            pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(a_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, a_pPipiline) != VK_SUCCESS)
                throw std::runtime_error("[CreateGraphicsPipeline]: failed to create graphics pipeline!");

            vkDestroyShaderModule(a_device, fragShaderModule, nullptr);
            vkDestroyShaderModule(a_device, vertShaderModule, nullptr);
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

        static glm::mat4 camera(unsigned a_frameNumber)
        {
            glm::vec3 camPos = { 0.f, 0.f, -113.f };

            glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
            glm::mat4 projection = glm::perspective(glm::radians(90.f), (float)WIDTH / (float)HEIGHT, 0.1f, 120.0f);
            projection[1][1] *= -1;
            glm::mat4 model = glm::rotate(glm::mat4{ 1.0f }, glm::radians(a_frameNumber * 0.004f), glm::vec3(0, 1, 0));

            return projection * view * model;
        }

        static void RecordCommandBuffer(VkFramebuffer a_swapChainFramebuffer, VkExtent2D a_frameBufferExtent, VkRenderPass a_renderPass,
                VkPipeline a_graphicsPipeline, VkPipelineLayout a_layout, std::vector<Mesh> a_meshes, VkCommandBuffer a_cmdBuffer, const unsigned a_frameID) 
        {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            if (vkBeginCommandBuffer(a_cmdBuffer, &beginInfo) != VK_SUCCESS) 
                throw std::runtime_error("[CreateCommandPoolAndBuffers]: failed to begin recording command buffer!");

            VkClearValue colorClear;
            colorClear.color = { {  1.0f, 0.7f, 0.6f, 1.0f } };

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

            vkCmdBindPipeline(a_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, a_graphicsPipeline);

            {
                std::vector<VkBuffer>     vertexBuffers{ a_meshes[0].getVBO().buffer };
                std::vector<VkDeviceSize> offsets{ 0 };

                vkCmdBindVertexBuffers(a_cmdBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());
            }

            // push constants
            {
                MeshPushConstants constants{};
                constants.mvp = camera(a_frameID);

                vkCmdPushConstants(a_cmdBuffer, a_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);
            }

            vkCmdDraw(a_cmdBuffer, a_meshes[0].vertices.size(), 1, 0, 0);

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

        static void CreateVertexBuffer(VkDevice a_device, VkPhysicalDevice a_physDevice, const size_t a_bufferSize,
                VkBuffer *a_pBuffer, VkDeviceMemory *a_pBufferMemory)
        {

            VkBufferCreateInfo bufferCreateInfo{};
            bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.pNext       = nullptr;
            bufferCreateInfo.size        = a_bufferSize;                         
            bufferCreateInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
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

        static void CreateIndexBuffer(VkDevice a_device, VkPhysicalDevice a_physDevice, const size_t a_bufferSize,
                VkBuffer *a_pBuffer, VkDeviceMemory *a_pBufferMemory)
        {

            VkBufferCreateInfo bufferCreateInfo{};
            bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.pNext       = nullptr;
            bufferCreateInfo.size        = a_bufferSize;                         
            bufferCreateInfo.usage       = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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

            RecordCommandBuffer(m_screen.swapChainFramebuffers[imageIndex], m_screen.swapChainExtent, m_renderPass, m_graphicsPipeline, m_pipelineLayout,
                    m_meshes, m_commandBuffers[imageIndex], m_frameNumber);

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
            m_currentFrame = (m_frameNumber + 1) % MAX_FRAMES_IN_FLIGHT;
            ++m_frameNumber;
        }

    public:

        void run() 
        {
            std::cout << "\tinitializing window...\n";
            InitWindow();

            std::cout << "\tinitializing vulkan devices and queue...\n";
            InitVulkan();
            CreateResources();

            std::cout << "\tlaunching main loop...\n";
            MainLoop();

            std::cout << "\tcleaning up...\n";
            Cleanup();
        }
};

int main() 
{
    HelloTriangleApplication app;

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
