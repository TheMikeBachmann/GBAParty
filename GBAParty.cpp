#include <stdio.h>
#include <SDL2/SDL.h> 
#include <mgba/core/core.h>
#include <mgba/gba/interface.h>
#include <vector>
#include <memory>

// Created to enable automatic SDL cleanup on program exit
struct SDLSession {
    ~SDLSession() {
        SDL_Quit();
    }
};

struct mGBACore {
    void operator()(mCore* core) {
        core->deinit(core);
    }
};

int main(void) {
    bool quit = false;
    SDLSession SDL;

    // SDL window block
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    // mgba core block. Moved here so SDL_Quit isn't called before the SDL_Init
    // video buffer for GBA screen
    //struct mCore* core = nullptr;
    const char* romPath = "tools/Mario Kart - Super Circuit (USA).gba";
    std::vector<mColor> videoBuffer(GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS);
    std::unique_ptr<mCore, mGBACore> core(mCoreFind(romPath));
    if (core == nullptr) {
        printf("Failed to find ROM: %s\n", romPath);
        return 1;
    }
    if (!core->init(core.get())) {
        printf("Failed to initialize core\n");
        return 1;
    }
    mCoreInitConfig(core.get(), "GBA");
    if (mCoreLoadFile(core.get(), romPath) != true) {
        printf("Failed to load ROM: %s\n", romPath);

        return 1;
    }
    core->setVideoBuffer(core.get(), videoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS);
    core->reset(core.get());

    // Create a window, outside main loop
    //declared this way because it is now an object that contains a pointer, rather than a pointer. cleans itself up i think because it knows how now?
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("GBA Party!", 100, 100, 640, 480, SDL_WINDOW_SHOWN), &SDL_DestroyWindow);
    if (window.get() == nullptr) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        return 1;
    }
    // Create a renderer. The window was just the address, this will display that address
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC), &SDL_DestroyRenderer);
    if (renderer.get() == nullptr) {
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_SetRenderDrawColor(renderer.get(), 255, 0, 0, 255) != 0) {
            printf("SDL_SetRenderDrawColor Error: %s\n", SDL_GetError());
            return 1;
        }

    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS), &SDL_DestroyTexture);
    if (texture.get() == nullptr) {
        printf("SDL_CreateTexture Error: %s\n", SDL_GetError());
        return 1;
    }
    


    while (!quit) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }
        }

        core->runFrame(core.get());

        if (SDL_RenderClear(renderer.get()) != 0) {
            printf("SDL_RenderClear Error: %s\n", SDL_GetError());
            return 1;
        }

        SDL_UpdateTexture(texture.get(), nullptr, videoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS * sizeof(mColor));
        SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
        

        SDL_RenderPresent(renderer.get());
    }

    return 0; 
}