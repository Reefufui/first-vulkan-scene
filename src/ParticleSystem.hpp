// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#ifndef PARTICLE_SYSTEM_HPP
#define PARTICLE_SYSTEM_HPP

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <random>
#include <cstring>

#include "Mesh.hpp" // for VertexInputDescription
#include "Texture.hpp"
#include "Timer.hpp"

class ParticleSystem
{
    private:
        struct Particle
        {
            glm::vec4 position;
            glm::vec4 color;
            float     alpha;
            float     size;
            float     rotation;

            glm::vec4 velocity;
            float     rotSpeed;
        };

        std::vector<Particle>      m_particles{};
        std::default_random_engine m_randomEngine{};

        VkBuffer       m_vbo;
        VkDeviceMemory m_vboMem;
        void*          m_mappedMemory;
        size_t         m_vboSize;

        glm::vec3 m_emmiterPos{};
        glm::vec3 m_minVelocity = glm::vec3(-0.3f, 0.2f, -0.3f);
        glm::vec3 m_maxVelocity = glm::vec3(0.3f, 4.0f, 0.3f);

        uint32_t m_parcticleCount{};
        float    m_flameRadius{0.5f};

        InputTexture* m_pAttachedTexture{};

        float random(float a_range)
        {
            std::uniform_real_distribution<float> randomDistribution(0.0f, a_range);
            return randomDistribution(m_randomEngine);
        }

        glm::vec3 getRandomPosition()
        {
            float radius{ random(m_flameRadius) };
            float phi{ random(glm::pi<float>()) - glm::pi<float>() / 2.0f };
            float theta{ random(2.0f * glm::pi<float>()) };

            return glm::vec3(radius * cos(theta) * cos(phi), radius * sin(phi), radius * sin(theta) * cos(phi));
        }

        void createParticle(Particle& a_particle)
        {
            a_particle.position = glm::vec4(m_emmiterPos + getRandomPosition(), 1.0f);
            a_particle.color    = glm::vec4(1.0f);
            a_particle.alpha    = random(0.40f);
            a_particle.size     = 15.0f + random(30.0f);
            a_particle.rotation = random(2.0f * glm::pi<float>());
            a_particle.velocity = glm::vec4(0.0f, glm::max(random(m_maxVelocity.y), m_minVelocity.y), 0.0f, 0.0f);
            a_particle.rotSpeed = random(glm::pi<float>()) - glm::pi<float>();
        }

    public:

        ParticleSystem()
        {
            Timer timer{};
            timer.timeStamp();
            m_randomEngine.seed(timer.getTime());
        }

        VkBuffer&       getVBO() { return m_vbo; };
        VkDeviceMemory& getVBOMemory() { return m_vboMem; };
        size_t          getSize() const { return m_vboSize; };
        uint32_t        getParticleCount() const { return m_parcticleCount; };
        auto&           getMappedMemory() { return m_mappedMemory; };
        auto&           getTexture() { return m_pAttachedTexture; };

        void attachTexture(InputTexture* a_pInputTexture)
        {
            m_pAttachedTexture = a_pInputTexture;
        }

        void initParticles(glm::vec3 a_emmiterPos, uint32_t a_count)
        {
            m_parcticleCount = a_count;
            m_emmiterPos     = a_emmiterPos + glm::vec3(0.0f, m_flameRadius / 3.0f, 0.0f);
            m_vboSize        = a_count * sizeof(Particle);

            m_particles.resize(a_count);

            for (auto& particle : m_particles)
            {
                createParticle(particle);
            }
        }

        void updateParticles(float a_time, glm::vec3 a_emmiterPos)
        {
            m_emmiterPos = a_emmiterPos + glm::vec3(0.0f, m_flameRadius / 3.0f, 0.0f);

            static float previousTime{};
            float timeElapsed = a_time - previousTime;

            for (auto& particle : m_particles)
            {
                particle.position += particle.velocity * timeElapsed * 0.5f;
                particle.alpha += timeElapsed * 2.5f;
                particle.size  -= timeElapsed * random(10.0f);
                particle.rotation += particle.rotSpeed * timeElapsed;

                if (particle.alpha > 2.0f)
                {
                    createParticle(particle);
                }
            }

            previousTime = a_time;

            memcpy(m_mappedMemory, m_particles.data(), m_vboSize);
        }

        static VertexInputDescription getVertexDescription()
        {
            VertexInputDescription description{};

            std::vector<VkVertexInputBindingDescription> vInputBindings {
                // binding, stride, inputRate
                { 0, sizeof(Particle), VK_VERTEX_INPUT_RATE_VERTEX }
            };

            description.bindings = vInputBindings;

            std::vector<VkVertexInputAttributeDescription> vAttributes {
                // location, binding, format, offset

                { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, position) },
                    { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, color) },
                    { 2, 0, VK_FORMAT_R32_SFLOAT, offsetof(Particle, alpha) },
                    { 3, 0, VK_FORMAT_R32_SFLOAT, offsetof(Particle, size) },
                    { 4, 0, VK_FORMAT_R32_SFLOAT, offsetof(Particle, rotation) }
            };

            description.attributes = vAttributes;

            return description;
        }

        void cleanup(VkDevice a_device)
        {
            vkFreeMemory(a_device, m_vboMem, nullptr);
            vkDestroyBuffer(a_device, m_vbo, nullptr);
            m_particles = std::vector<Particle>(0);
        }
};

#endif //PARTICLE_SYSTEM_HPP
