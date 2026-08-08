#include "pch.h"
#include "Timings.h"

int Timings::getPerSecond(std::vector<std::chrono::steady_clock::time_point>& list) {
    auto now = std::chrono::steady_clock::now();
    std::erase_if(list, [now](std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > 1000;
    });
    return static_cast<int>(list.size());
}

void Timings::update() {
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::high_resolution_clock::now();

    auto currentFrameDuration = now - lastFrameTime;
    frameTime = std::chrono::duration<float, std::milli>(currentFrameDuration).count();
    lastFrameTime = now;

    auto dur = std::chrono::high_resolution_clock::now() - lastFPSTime;
    float dir = std::chrono::duration<float, std::milli>(dur).count();
    if (dir > 1000) {
        fps = frames;
        frames = 0;
        lastFPSTime = std::chrono::high_resolution_clock::now();
    }
    frames++;

    cpsL = getPerSecond(cpsLV);
    cpsR = getPerSecond(cpsRV);
}

void Timings::onClick(int mb, bool isDown) {
    if (mb == 1 && isDown) {
        cpsLV.push_back(std::chrono::steady_clock::now());
    } else if (mb == 2 && isDown) {
        cpsRV.push_back(std::chrono::steady_clock::now());
    }
}
