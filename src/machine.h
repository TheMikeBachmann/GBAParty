#pragma once
#include <cstdio>
#include <atomic>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <SDL3/SDL.h>
#include <mgba/core/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>

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

    mAudioBuffer audioOut;
    mAudioResampler audioResampler;
    unsigned sourceRate = 0;

    std::jthread runFrameThread;

    GBAMachine() {
        mAudioBufferInit(&audioOut, 48000, 2);
        mAudioResamplerInit(&audioResampler, mINTERPOLATOR_SINC);
        mAudioResamplerSetDestination(&audioResampler, &audioOut, 48000);
    }

    ~GBAMachine() {
        runFrameThread.request_stop();
        if (runFrameThread.joinable()) {
            runFrameThread.join();
        }
        mAudioResamplerDeinit(&audioResampler);
        mAudioBufferDeinit(&audioOut);
    }

    size_t drainAudio(int16_t* buffer, size_t length) {
        unsigned rate = core->audioSampleRate(core.get());
        if (rate != 0 && rate != sourceRate) {
            sourceRate = rate;
            mAudioResamplerSetSource(&audioResampler, core->getAudioBuffer(core.get()), sourceRate, true);
        }
        mAudioResamplerProcess(&audioResampler);
        return mAudioBufferRead(&audioOut, buffer, length);
    }

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
    }
};