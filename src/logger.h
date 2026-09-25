#pragma once
#include <cstdio>
#include <cstdarg>
#include <mgba/core/log.h>

//logger to supress less important messages
inline void mGBALogger([[maybe_unused]] struct mLogger* logger, int category, enum mLogLevel level, const char* message, va_list args) {
    if (level & (mLOG_FATAL | mLOG_ERROR | mLOG_WARN | mLOG_GAME_ERROR)) {
        printf("[%d] %d: ", category, level);
        vprintf(message, args);
        printf("\n");
    }
}
