#include "Mesh.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

namespace std
{
    template<> struct hash<Vertex>
    {
        size_t operator()(Vertex const& vertex) const
        {
            return ((hash<glm::vec3>()(vertex.position) ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.uv) << 1);
        }
    };
}

void Mesh::loadFromOBJ(const char* a_filename)
{
    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials; // TODO

    std::string warn{};
    std::string err{};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.8, 1.0); // dis(gen)

    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, a_filename, nullptr);

    if (!warn.empty())
    {
        std::cout << "WARN: " << warn << std::endl;
    }

    if (!err.empty())
    {
        throw std::runtime_error(err.c_str());
    }

    if (!attrib.vertices.size())
    {
        throw std::runtime_error("Missing vertices in obj file");
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    bool hasNormals       { attrib.normals.size() != 0 };
    bool hasTextureCoords { attrib.texcoords.size() != 0 };

    for (const auto& shape : shapes)
    {
        for (const auto& index : shape.mesh.indices)
        {
            Vertex vertex{};

            /////////////////////////////////////////////////////////////
            vertex.position = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            /////////////////////////////////////////////////////////////
            vertex.color = { 0.58f, 0.29f, 0.0f }; //brown
            /////////////////////////////////////////////////////////////

            if (hasNormals)
            {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            }
            else
            {
                vertex.normal = glm::vec3(1.0f);
            }

            if (hasTextureCoords)
            {
                vertex.uv = {
                    attrib.texcoords[2 * index.vertex_index + 0],
                    1.0f - attrib.texcoords[2 * index.vertex_index + 1]
                };
            }


            if (!uniqueVertices.count(vertex))
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }
}

void Mesh::cleanup()
{
    std::vector<Buffer> buffers{ m_vbo, m_ibo };

    for (auto bo : buffers)
    {
        if (bo.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, bo.memory, NULL);
        }

        if (bo.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, bo.buffer, NULL);
        }
    }
}

