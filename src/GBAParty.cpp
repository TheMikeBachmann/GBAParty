#include <stdio.h>
#include <SDL3/SDL.h> 
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <vector>
#include <memory>
#include <thread>
#include <cstdarg>
#include <chrono>
#include <mutex>
#include <cstring>
#include "logger.h"
#include "machine.h"
#include "players.h"
#include "layout.h"



// Created to enable automatic SDL cleanup on program exit
struct SDLSession {
    ~SDLSession() {
        SDL_Quit();
    }
};

struct AudioDevice {
    SDL_AudioDeviceID id = 0;
    ~AudioDevice() {
        if (id != 0) {
            SDL_CloseAudioDevice(id);
        }
    }
};

int main(void) {
    bool quit = false;
    SDLSession SDL;
    setvbuf(stdout, nullptr, _IONBF, 0);

    // SDL window block
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    // Create a window, outside main loop
    //declared this way because it is now an object that contains a pointer, rather than a pointer. cleans itself up i think because it knows how now?
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("GBA Party!", 640, 480, SDL_WINDOW_RESIZABLE), &SDL_DestroyWindow);
    if (window.get() == nullptr) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        return 1;
    }
    // Create a renderer. The window was just the address, this will display that address
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(), nullptr), &SDL_DestroyRenderer);
    if (renderer.get() == nullptr) {
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer.get(), 1);
    if (!SDL_SetRenderDrawColor(renderer.get(), 255, 0, 0, 255)) {
            printf("SDL_SetRenderDrawColor Error: %s\n", SDL_GetError());
            return 1;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 48000;

    AudioDevice audioDevice;
    audioDevice.id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (audioDevice.id == 0) {
        printf("SDL_OpenAudioDevice Error: %s\n", SDL_GetError());
        return 1;
    }

    // mgba core block. runs each GBA machine in its own thread
    int machineCount = 2;
    GBAMachine gbaMachines[kMaxPlayers];
    const char* romPath = "tools/Mario Kart - Super Circuit (USA).gba";
    
    static mLogger loggerInstance = { mGBALogger, nullptr };
    mLogSetDefaultLogger(&loggerInstance);

    for (int i = 0; i < machineCount; i++)
    {
        GBAMachine& m = gbaMachines[i];
        if (!m.open(romPath))
        {
            printf("Failed to open GBA machine: %s\n", romPath);
            return 1;
        }
        m.texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_VIDEO_HORIZONTAL_PIXELS, GBA_VIDEO_VERTICAL_PIXELS));
        if (m.texture.get() == nullptr)
        {
            printf("SDL_CreateTexture Error: %s\n", SDL_GetError());
            return 1;
        }

        // machine now owns its own audio stream. no main loop mixing needed
        m.audioStream.reset(SDL_CreateAudioStream(&spec, &spec));
        if (m.audioStream.get() == nullptr) {
            printf("SDL_CreateAudioStream Error: %s\n", SDL_GetError());
            return 1;
        }
        if (!SDL_BindAudioStream(audioDevice.id, m.audioStream.get())) {
            printf("SDL_BindAudioStream Error: %s\n", SDL_GetError());
            return 1;
        }

        m.runFrameThread = std::jthread([&m](std::stop_token st)    {
            auto period = std::chrono::duration<double>(1.0 / 59.7275); // keeps the gba framerate exactly hardware spec
            auto deadline = std::chrono::steady_clock::now() + period;
            std::vector<int16_t> scratch(4096 * 2);

            while (!st.stop_requested()) {
                m.core->setKeys(m.core.get(), ~m.controllerState.load() & 0x03FF);
                m.core->runFrame(m.core.get());

                auto got = m.drainAudio(scratch.data(), scratch.size() / 2);
                SDL_PutAudioStreamData(m.audioStream.get(), scratch.data(), got * 4);
                //take the lock
                //copy video buffer to a safe location for rendering
                {
                    std::lock_guard<std::mutex> lock(m.videoBufferMutex);
                    std::copy(m.videoBuffer.begin(), m.videoBuffer.end(), m.safeVideoBuffer.begin());
                }
                
                deadline += period;
                auto now = std::chrono::steady_clock::now();
                if (deadline < now)
                    deadline = now;
                std::this_thread::sleep_until(deadline);
            
        } });
    }


    while (!quit) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                gbaMachines[0].controller.reset(SDL_OpenGamepad(event.gdevice.which));
                if (gbaMachines[0].controller != nullptr) {
                    printf("Controller added: %s\n", SDL_GetGamepadName(gbaMachines[0].controller.get()));
                }
                else {
                    printf("SDL_OpenGamepad Error: %s\n", SDL_GetError());
                }
            }

            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                const char* name = SDL_GetGamepadName(gbaMachines[0].controller.get());
                printf("Controller removed: %s\n", name ? name : "Unknown");
                gbaMachines[0].controllerState.store(0x03FF);
                gbaMachines[0].controller.reset(nullptr);
            }


            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
        }

        if (gbaMachines[0].controller != nullptr) {
            // Update controller state
            uint16_t tempControllerState = 0x03FF;
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_START)) {tempControllerState &= ~(1 << GBA_KEY_START);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_BACK)) {tempControllerState &= ~(1 << GBA_KEY_SELECT);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_DPAD_UP)) {tempControllerState &= ~(1 << GBA_KEY_UP);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {tempControllerState &= ~(1 << GBA_KEY_DOWN);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {tempControllerState &= ~(1 << GBA_KEY_LEFT);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {tempControllerState &= ~(1 << GBA_KEY_RIGHT);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_EAST)) {tempControllerState &= ~(1 << GBA_KEY_A);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_SOUTH)) {tempControllerState &= ~(1 << GBA_KEY_B);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {tempControllerState &= ~(1 << GBA_KEY_L);}
            if (SDL_GetGamepadButton(gbaMachines[0].controller.get(), SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {tempControllerState &= ~(1 << GBA_KEY_R);}
            gbaMachines[0].controllerState.store(tempControllerState);
            }

        if (!SDL_RenderClear(renderer.get())) {
            printf("SDL_RenderClear Error: %s\n", SDL_GetError());
            return 1;
        }
        
        for (int i = 0; i < machineCount; ++i) {
                GBAMachine& m = gbaMachines[i];
                
                if (!m.opened) { continue; }

                {
                    std::lock_guard<std::mutex> lock(m.videoBufferMutex);
                    SDL_UpdateTexture(m.texture.get(), nullptr, m.safeVideoBuffer.data(), GBA_VIDEO_HORIZONTAL_PIXELS * sizeof(mColor));
                }
            SDL_RenderTexture(renderer.get(), m.texture.get(), nullptr, nullptr);
        }
        SDL_RenderPresent(renderer.get());
    }

    return 0; 
}