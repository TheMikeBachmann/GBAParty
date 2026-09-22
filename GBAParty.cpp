#include <stdio.h>
#include <SDL2/SDL.h> 
#include <mgba/core/core.h>
#include <mgba/gba/interface.h>
#include <vector>

int main(void) {
    bool quit = false;
    struct mCore* core = nullptr;

    // video buffer for GBA screen
    std::vector<mColor> videoBuffer(GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS);

    // mgba core block
    const char* romPath = "tools/Mario Kart - Super Circuit (USA).gba";
    core = mCoreFind(romPath);
    if (core == nullptr) {
        printf("Failed to find ROM: %s\n", romPath);
        return 1;
    }
    if (!core->init(core)) {
        printf("Failed to initialize core\n");
        core->deinit(core);
        return 1;
    }
    mCoreInitConfig(core, "GBA");
    if (mCoreLoadFile(core, romPath) != true) {
        printf("Failed to load ROM: %s\n", romPath);
        core->deinit(core);
        return 1;
    }

    core->setVideoBuffer(core, videoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS);
    core->reset(core);

    // SDL window block
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        core->deinit(core);
        return 1;
    }
    // Create a window, outside main loop
    SDL_Window* window = SDL_CreateWindow("Hello World!", 100, 100, 640, 480, SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        core->deinit(core);
        SDL_Quit();
        return 1;
    }
    // Create a renderer. The window was just the address, this will display that address
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        core->deinit(core);
        SDL_Quit();
        return 1;
    }
    if (SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255) != 0) {
            printf("SDL_SetRenderDrawColor Error: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            core->deinit(core);
            SDL_Quit();
            return 1;
        }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS);


    while (!quit) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }
        }

        core->runFrame(core);

        if (SDL_RenderClear(renderer) != 0) {
            printf("SDL_RenderClear Error: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            core->deinit(core);
            SDL_Quit();
            return 1;
        }

        SDL_UpdateTexture(texture, nullptr, videoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS * sizeof(mColor));
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    core->deinit(core);

    return 0; // Notifies the OS that the program ran successfully
}