#pragma once

namespace EQConstants
{
    // Add numEQBands to the namespace
    constexpr int numEQBands = 7;
    
    // Add fftSize constant
    constexpr int fftSize = 1024;
    
    // Your other constants
    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;
    constexpr float minGain = -24.0f;
    constexpr float maxGain = 24.0f;
    constexpr float minQ = 0.1f;
    constexpr float maxQ = 10.0f;
}
