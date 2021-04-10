// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/scalar_constants.hpp> // glm::pi

#include "Timer.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#ifndef EYE_HPP
#define EYE_HPP

#ifndef FAR
#define FAR 70.0f
#endif

#ifndef NEAR
#define NEAR 0.001f
#endif

#ifndef FOV
#define FOV 70.0f
#endif

const int WIDTH     = 1600;
const int HEIGHT    = 900;
const int CUBE_SIDE = 1000;

class Eye
{
    private:
        Timer* m_timer{};

    public:
        virtual glm::vec3 position() = 0;
        virtual glm::mat4 view(uint32_t a_face) = 0;
        virtual glm::mat4 projection() = 0;

        Eye(Timer* a_pTimer)
            : m_timer(a_pTimer)
        {
        }

        float getTime() const
        {
            return m_timer->getTime();
        };
};

// TODO: glfw input
class Camera : public Eye
{
    public:
        
        Camera(Timer* a_pTimer)
            : Eye(a_pTimer)
        {
        }

        glm::vec3 position()
        {
            glm::vec3 pos{ glm::vec3(3.5f) };

            return pos;
        }

        glm::mat4 view(uint32_t a_dummy)
        {
            if (a_dummy != 0)
                throw std::runtime_error("[Camera::view]: Specified face for camera");

            glm::mat4 view = glm::lookAt(
                    position(),
                    glm::vec3(0.0f, 2.0f, 0.0f),
                    glm::vec3(0.f, 1.f, 0.f)
                    );

            return view;
        }

        glm::mat4 projection()
        {
            glm::mat4 projection = glm::perspective(glm::radians(FOV), (float)WIDTH / (float)HEIGHT, NEAR, FAR);

            return projection;
        }
};

class Light : public Eye
{
    public:

        Light(Timer* a_pTimer)
            : Eye(a_pTimer)
        {
        }

        glm::vec3 position()
        {
            glm::vec3 pos = glm::vec3(5.0f * (float)sin(this->getTime() / 2.0f), 2.0f, 5.0f * (float)cos(this->getTime() / 2.0f));

            return pos;
        }

        glm::mat4 view(uint32_t a_face)
        {
            // lookAt matrix doesnt suit as soon as we cant look directly up/down with it
            // implenented using basic glm functionality

            glm::mat4 model = glm::translate(glm::mat4(1.0f), -position());

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

            return view * model;
        }

        glm::mat4 projection()
        {
            glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, NEAR, (float)CUBE_SIDE);

            return projection;
        }
};

#endif // EYE_HPP
