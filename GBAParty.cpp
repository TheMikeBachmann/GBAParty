#include <stdio.h>
#include <SDL3/SDL.h> 
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/input.h>
#include <vector>
#include <memory>
#include <thread>
#include <cstdarg>
#include <chrono>
#include <mutex>
#include <cstring>


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

struct destroyTexture {
    void operator()(SDL_Texture* texture) {
        SDL_DestroyTexture(texture);
    }
};

struct closeGamepad {
    void operator()(SDL_Gamepad* gamepad) {
        SDL_CloseGamepad(gamepad);
    }
};

struct GBAMachine {
    std::atomic<uint16_t> controllerState = 0x03FF;
    std::vector<mColor> videoBuffer = std::vector<mColor>(GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS);
    std::vector<mColor> safeVideoBuffer = std::vector<mColor>(GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS);

    std::mutex videoBufferMutex;
    std::unique_ptr<mCore, mGBACore> core;

    std::unique_ptr<SDL_Texture, destroyTexture> texture;
    std::unique_ptr<SDL_Gamepad, closeGamepad> controller;
    std::jthread runFrameThread;

    [[nodiscard]] bool open(const char* romPath) {
        core.reset(mCoreFind(romPath));
        if (core == nullptr) {
            printf("Failed to find ROM: %s\n", romPath);
            return false;
        }
        if (!core->init(core.get())) {
            printf("Failed to initialize core\n");
            return false;
        }
        mCoreInitConfig(core.get(), "GBA");
        if (mCoreLoadFile(core.get(), romPath) != true) {
            printf("Failed to load ROM: %s\n", romPath);
            return false;
        }
        core->setVideoBuffer(core.get(), videoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS);
        core->reset(core.get());
        return true;
    };
};

//logger to supress less important messages
void mGBALogger([[maybe_unused]] struct mLogger* logger, int category, enum mLogLevel level, const char* message, va_list args) {
    if (level & (0x01 | 0x02 | 0x04 | 0x40)) {
        printf("[%d] %d: ", category, level);
        vprintf(message, args);
        printf("\n");
    }
}

int main(void) {
    bool quit = false;
    SDLSession SDL;
    setvbuf(stdout, nullptr, _IONBF, 0);

    // SDL window block
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    // SDL controller subsystem initialization
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        printf("SDL_InitSubSystem Error: %s\n", SDL_GetError());
        return 1;
    }


    // Create a window, outside main loop
    //declared this way because it is now an object that contains a pointer, rather than a pointer. cleans itself up i think because it knows how now?
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("GBA Party!", 640, 480, 0), &SDL_DestroyWindow);
    if (window.get() == nullptr) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        return 1;
    }
    // Create a renderer. The window was just the address, this will display that address
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(), nullptr), &SDL_DestroyRenderer);
    SDL_SetRenderVSync(renderer.get(), 1);
    if (renderer.get() == nullptr) {
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_SetRenderDrawColor(renderer.get(), 255, 0, 0, 255)) {
            printf("SDL_SetRenderDrawColor Error: %s\n", SDL_GetError());
            return 1;
    }

    // mgba core block. Moved here so it's created after the renderer and destroyed in the correct order
    // video buffer for GBA screen
    GBAMachine gbaMachine;
    const char* romPath = "tools/Mario Kart - Super Circuit (USA).gba";
    gbaMachine.controller.reset(SDL_OpenGamepad(0));
    
    static mLogger loggerInstance = { mGBALogger, nullptr };
    mLogSetDefaultLogger(&loggerInstance);
    
    if(!gbaMachine.open(romPath)) {
        printf("Failed to open GBA machine: %s\n", romPath);
        return 1;
    }

    gbaMachine.texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS));
    if (gbaMachine.texture.get() == nullptr) {
        printf("SDL_CreateTexture Error: %s\n", SDL_GetError());
        return 1;
    }
    
  
    gbaMachine.runFrameThread = std::jthread([&gbaMachine](std::stop_token st) {
        auto period = std::chrono::duration<double>(1.0 / 59.7275); // keeps the gba framerate exactly hardware spec
        auto deadline = std::chrono::steady_clock::now() + period;

        while (!st.stop_requested()) {
            gbaMachine.core->setKeys(gbaMachine.core.get(), ~gbaMachine.controllerState.load() & 0x03FF);
            gbaMachine.core->runFrame(gbaMachine.core.get());
            //take the lock
            //copy video buffer to a safe location for rendering
            {
                std::lock_guard<std::mutex> lock(gbaMachine.videoBufferMutex);
                std::copy(gbaMachine.videoBuffer.begin(), gbaMachine.videoBuffer.end(), gbaMachine.safeVideoBuffer.begin());
            }
            
            std::this_thread::sleep_until(deadline);
            deadline += period;
        }
    });

    while (!quit) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                gbaMachine.controller.reset(SDL_OpenGamepad(event.gdevice.which));
                if (gbaMachine.controller != nullptr) {
                    printf("Controller added: %s\n", SDL_GetGamepadName(gbaMachine.controller.get()));
                }
                else {
                    printf("SDL_OpenGamepad Error: %s\n", SDL_GetError());
                }
            }

            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                gbaMachine.controllerState.store(0x03FF);
                gbaMachine.controller.reset(nullptr);
                printf("Controller removed: %s\n", SDL_GetGamepadName(gbaMachine.controller.get() ? gbaMachine.controller.get() : nullptr));
            }


            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
        }

        if (gbaMachine.controller != nullptr) {
            // Update controller state
            uint16_t tempControllerState = 0x03FF;
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_START)) {tempControllerState &= ~(1 << GBA_KEY_START);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_BACK)) {tempControllerState &= ~(1 << GBA_KEY_SELECT);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_DPAD_UP)) {tempControllerState &= ~(1 << GBA_KEY_UP);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {tempControllerState &= ~(1 << GBA_KEY_DOWN);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {tempControllerState &= ~(1 << GBA_KEY_LEFT);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {tempControllerState &= ~(1 << GBA_KEY_RIGHT);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_EAST)) {tempControllerState &= ~(1 << GBA_KEY_A);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_SOUTH)) {tempControllerState &= ~(1 << GBA_KEY_B);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {tempControllerState &= ~(1 << GBA_KEY_L);}
            if (SDL_GetGamepadButton(gbaMachine.controller.get(), SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {tempControllerState &= ~(1 << GBA_KEY_R);}
            gbaMachine.controllerState.store(tempControllerState);
            }
        

        

        if (!SDL_RenderClear(renderer.get())) {
            printf("SDL_RenderClear Error: %s\n", SDL_GetError());
            return 1;
        }

        {
            std::lock_guard<std::mutex> lock(gbaMachine.videoBufferMutex);
            SDL_UpdateTexture(gbaMachine.texture.get(), nullptr, gbaMachine.safeVideoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS * sizeof(mColor));
        }
        SDL_RenderTexture(renderer.get(), gbaMachine.texture.get(), nullptr, nullptr);
        

        SDL_RenderPresent(renderer.get());
    }

    return 0; 
}