#pragma once
#include <SDL3/SDL.h>
#include "players.h"

struct Layout {
    SDL_FRect playerAreas[kMaxPlayers];
    int count = 0;
};

inline Layout computeLayout(int winW, int winH, int count) {
    Layout layout;
    layout.count = count;
    for (int i = 0; i < count; i++) {
        layout.playerAreas[i] = {0, 0, static_cast<float>(winW) / count, static_cast<float>(winH)};
    }
    return layout;
}