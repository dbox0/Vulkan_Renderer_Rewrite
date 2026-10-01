#include "Camera.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>

void Camera::handleEvent(const SDL_Event &event)
{
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event.button.button == SDL_BUTTON_RIGHT) {
            m_looking = down;
            // Hides the cursor and keeps motion coming when it would leave the window.
            SDL_SetWindowRelativeMouseMode(SDL_GetWindowFromID(event.button.windowID), down);
        }
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            m_panning = down;
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        const float dx = event.motion.xrel;
        const float dy = event.motion.yrel;
        if (m_looking) {
            constexpr float pitchLimit = 1.5607963f;   // just under 90 degrees
            yaw += dx * lookSensitivity;
            pitch = glm::clamp(pitch - dy * lookSensitivity, -pitchLimit, pitchLimit);
        } else if (m_panning) {
            const glm::mat4 rot = rotation();
            position -= glm::vec3(rot[0]) * dx * panSensitivity;
            position += glm::vec3(rot[1]) * dy * panSensitivity;
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        m_looking = false;
        m_panning = false;
        SDL_SetWindowRelativeMouseMode(SDL_GetWindowFromID(event.window.windowID), false);
        break;
    default:
        break;
    }
}

void Camera::update(const bool *keys, float deltaTime)
{
    if (!m_looking) {
        return;
    }

    glm::vec3 direction{ 0.0f };
    if (keys[SDL_SCANCODE_W]) direction.z -= 1.0f;
    if (keys[SDL_SCANCODE_S]) direction.z += 1.0f;
    if (keys[SDL_SCANCODE_A]) direction.x -= 1.0f;
    if (keys[SDL_SCANCODE_D]) direction.x += 1.0f;
    if (keys[SDL_SCANCODE_E]) direction.y += 1.0f;
    if (keys[SDL_SCANCODE_Q]) direction.y -= 1.0f;

    if (glm::length2(direction) > 0.0f) {
        const glm::vec3 local = glm::normalize(direction) * speed * deltaTime;
        position += glm::vec3(rotation() * glm::vec4(local, 0.0f));
    }
}

glm::mat4 Camera::rotation() const
{
    const glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3{ 1.0f, 0.0f, 0.0f });
    const glm::quat yawRotation   = glm::angleAxis(yaw, glm::vec3{ 0.0f, -1.0f, 0.0f });
    return glm::mat4_cast(yawRotation) * glm::mat4_cast(pitchRotation);
}

glm::mat4 Camera::view() const
{
    return glm::inverse(glm::translate(glm::mat4(1.0f), position) * rotation());
}

glm::mat4 Camera::projection(float aspectRatio) const
{
    return glm::perspectiveRH_ZO(glm::radians(fovDegrees), aspectRatio, nearPlane, farPlane);
}

glm::mat4 Camera::viewProjection(float aspectRatio) const
{
    return projection(aspectRatio) * view();
}

void Camera::lookAt(const glm::vec3 &target)
{
    const glm::vec3 d = target - position;
    if (glm::length2(d) < 1e-8f) {
        return;
    }
    const glm::vec3 dir = glm::normalize(d);
    yaw   = std::atan2(dir.x, -dir.z);
    pitch = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
}
