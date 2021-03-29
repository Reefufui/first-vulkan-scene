// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#ifndef MESH_HPP
#define MESH_HPP

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

struct VertexInputDescription {
    std::vector<VkVertexInputBindingDescription>   bindings{};
    std::vector<VkVertexInputAttributeDescription> attributes{};
    VkPipelineVertexInputStateCreateFlags          flags{};
};

struct MeshPushConstants {
    glm::vec4 data;
    glm::mat4 mvp;
};

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec3 color{};

    static VertexInputDescription getVertexDescription()
    {
        VertexInputDescription description{};

        std::vector<VkVertexInputBindingDescription> vInputBindings {
            // binding, stride, inputRate
            { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
        };

        description.bindings = vInputBindings;

        std::vector<VkVertexInputAttributeDescription> vAttributes {
            // location, binding, format, offset

            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) }, //position
                { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)   }, //normal
                { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)    }  //color
        };

        description.attributes = vAttributes;

        return description;
    }

};

class Mesh {
    private:
        VkDevice m_device;

        struct Buffer {
            VkBuffer       buffer{};
            VkDeviceMemory memory{};
        };

        Buffer m_vbo{}, m_ibo{};

    public:

        std::vector<Vertex> vertices{};

        Buffer& getVBO() { return m_vbo; }
        Buffer& getIBO() { return m_ibo; }

        void setDevice(VkDevice a_device) { m_device = a_device; }
        void loadFromOBJ(const char* a_filename);
        void cleanup();
};

#endif // ifndef MESH_HPP

