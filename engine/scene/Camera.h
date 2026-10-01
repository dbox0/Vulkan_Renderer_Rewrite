#pragma once
#include <SDL3/SDL_events.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Fly camera. Hold the right mouse button to look and move with WASD + Q/E; drag with the middle button to pan.
class Camera
{
public:
    void handleEvent(const SDL_Event &event);
    void update(const bool *keyboardState, float deltaTime);

    glm::mat4 viewProjection(float aspectRatio) const;
    glm::mat4 view() const;
    glm::mat4 projection(float aspectRatio) const;
    glm::mat4 rotation() const;

    void lookAt(const glm::vec3 &target);

    glm::vec3 position{ 0.0f, 0.0f, 5.0f };
    float yaw   = 0.0f;
    float pitch = 0.0f;

    float fovDegrees = 75.0f;
    float nearPlane  = 0.01f;
    float farPlane   = 1000.0f;

    float speed           = 6.0f;
    float lookSensitivity = 0.005f;
    float panSensitivity  = 0.005f;

private:
    bool m_looking = false;
    bool m_panning = false;
};
