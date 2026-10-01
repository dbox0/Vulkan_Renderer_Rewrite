#pragma once
#include <SDL3/SDL_events.h>

// Optional extension the application drives each frame, e.g. the editor.
// The app knows nothing about what a layer does.
class AppLayer
{
public:
    virtual ~AppLayer() = default;

    virtual bool onEvent(const SDL_Event &) { return false; }   // true = consumed, the app ignores it
    virtual bool wantsKeyboard() const { return false; }        // true = no camera input this frame
    virtual void onUpdate(float /*deltaTime*/) {}               // called once per frame, before rendering
};
