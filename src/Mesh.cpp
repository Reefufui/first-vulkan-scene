#include "Mesh.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <iostream>
#include <vector>

void Mesh::loadFromOBJ(const char* a_filename)
{
    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials; // TODO

    std::string warn{};
    std::string err{};

    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, a_filename, nullptr);

    if (!warn.empty())
    {
        std::cout << "WARN: " << warn << std::endl;
    }

    if (!err.empty())
    {
        throw std::runtime_error(err.c_str());
    }

    for (auto shape : shapes)
    {
        // Loop over faces(polygon)
        size_t indexOffset{};

        for (size_t f{}; f < shape.mesh.num_face_vertices.size(); f++)
        {
            //hardcode loading to triangles
            int fv = 3;

            // Loop over vertices in the face.
            for (size_t v = 0; v < fv; v++)
            {                
                // access to vertex
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                //vertex position
                tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
                tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
                tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
                //vertex normal
                tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
                tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
                tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];

                //copy it into our vertex
                Vertex new_vert;
                new_vert.position.x = vx;
                new_vert.position.y = vy;
                new_vert.position.z = vz;

                new_vert.normal.x = nx;
                new_vert.normal.y = ny;
                new_vert.normal.z = nz;

                //we are setting the vertex color as the vertex normal. This is just for display purposes
                new_vert.color = new_vert.normal;

                vertices.push_back(new_vert);
            }

            indexOffset += fv;
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
