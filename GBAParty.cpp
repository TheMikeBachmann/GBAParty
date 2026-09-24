#include <stdio.h>
#include <SDL2/SDL.h> 
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

//logger to supress less important messages
void mGBALogger(struct mLogger* logger, int category, enum mLogLevel level, const char* message, va_list args) {
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
    uint16_t controllerState = 0x03FF;
    uint16_t lastControllerState = controllerState;

    // SDL window block
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    // SDL controller subsystem initialization
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_InitSubSystem Error: %s\n", SDL_GetError());
        return 1;
    }
    std::unique_ptr<SDL_GameController, decltype(&SDL_GameControllerClose)> controller(nullptr, &SDL_GameControllerClose);

    // mgba core block. Moved here so SDL_Quit isn't called before the SDL_Init
    // video buffer for GBA screen
    const char* romPath = "tools/Mario Kart - Super Circuit (USA).gba";
    std::vector<mColor> videoBuffer(GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS);
    std::vector<mColor> safeVideoBuffer(GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS);
    static mLogger loggerInstance = { mGBALogger };
    mLogSetDefaultLogger(&loggerInstance);
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
    
    std::mutex videoBufferMutex;
    std::jthread runFrameThread([&core, &videoBufferMutex, &videoBuffer, &safeVideoBuffer](std::stop_token st) {
        auto period = std::chrono::duration<double>(1.0 / 59.7275); // keeps the gba framerate exactly hardware spec
        auto deadline = std::chrono::steady_clock::now() + period;

        while (!st.stop_requested()) {
            core->runFrame(core.get());
            //take the lock
            //copy video buffer to a safe location for rendering
            {
                std::lock_guard<std::mutex> lock(videoBufferMutex);
                std::copy(videoBuffer.begin(), videoBuffer.end(), safeVideoBuffer.begin());
            }
            
            std::this_thread::sleep_until(deadline);
            deadline += period;
        }
    });

    while (!quit) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_CONTROLLERDEVICEADDED) {
                controller.reset(SDL_GameControllerOpen(event.cdevice.which));
                if (controller != nullptr) {
                    printf("Controller added: %s\n", SDL_GameControllerName(controller.get()));
                }
                else {
                    printf("SDL_GameControllerOpen Error: %s\n", SDL_GetError());
                }
            }

            if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                controller.reset(nullptr);
                printf("Controller removed: %s\n", SDL_GameControllerName(controller.get() ? controller.get() : nullptr));
            }


            if (event.type == SDL_QUIT) {
                quit = true;
            }
        }

        if (controller) {
            // Update controller state
            controllerState = 0x03FF; // Reset state
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_START)) {controllerState &= ~(1 << GBA_KEY_START);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_BACK)) {controllerState &= ~(1 << GBA_KEY_SELECT);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_DPAD_UP)) {controllerState &= ~(1 << GBA_KEY_UP);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {controllerState &= ~(1 << GBA_KEY_DOWN);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {controllerState &= ~(1 << GBA_KEY_LEFT);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {controllerState &= ~(1 << GBA_KEY_RIGHT);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_A)) {controllerState &= ~(1 << GBA_KEY_A);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_B)) {controllerState &= ~(1 << GBA_KEY_B);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {controllerState &= ~(1 << GBA_KEY_L);}
            if (SDL_GameControllerGetButton(controller.get(), SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {controllerState &= ~(1 << GBA_KEY_R);}
            if (controllerState != lastControllerState) {
                printf("Controller state changed: 0x%04X -> 0x%04X\n", lastControllerState, controllerState);
            }
            lastControllerState = controllerState;
            }
        

        

        if (SDL_RenderClear(renderer.get()) != 0) {
            printf("SDL_RenderClear Error: %s\n", SDL_GetError());
            return 1;
        }

        {
            std::lock_guard<std::mutex> lock(videoBufferMutex);
            SDL_UpdateTexture(texture.get(), nullptr, safeVideoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS * sizeof(mColor));
        }
        SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
        

        SDL_RenderPresent(renderer.get());
    }

    return 0; 
}