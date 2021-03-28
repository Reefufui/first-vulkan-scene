// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#include <glm/vec3.hpp>

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec3 color{};
}

struct Mesh {
    std::vector<Vertex> m_verices{};

    struct _Buffer {
        VkBuffer       buffer{};
        VkDeviceMemory memory{};
    };

    _Buffer m_vbo{}, m_ibo{};

}
