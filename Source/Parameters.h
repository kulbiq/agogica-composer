#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Geometry.h"

// Every parameter once: its id, which group it belongs to, and how the host sees it.
// Hosts save projects and automation by these ids, ranges and defaults
// (tests/data/parameters.txt pins them): never change one.
namespace params
{
inline constexpr auto roomSize = "roomSize";             // m, side of the square room
inline constexpr auto listenerX = "listenerX";           // m
inline constexpr auto listenerY = "listenerY";
inline constexpr auto playerX = "playerX";               // m, this track's player
inline constexpr auto playerY = "playerY";
inline constexpr auto outputMode = "outputMode";         // 0 = Stereo, 1 = Binaural
inline constexpr auto proDistanceLaw = "proDistanceLaw"; // dB quieter per doubling of distance
inline constexpr auto listenerYaw = "listenerYaw";       // degrees clockwise the listener faces from +y (Binaural)

// A player's place: each track has its own.
inline const juce::StringArray ofPlayer { playerX, playerY };
// The same for the whole stage: every track carries a copy, and an edit goes to all of them.
inline const juce::StringArray ofStage { roomSize, listenerX, listenerY, listenerYaw, outputMode, proDistanceLaw };
// What a scene (A-H) remembers. Output mode and the Pro values are project-wide instead.
inline const juce::StringArray ofScene { roomSize, listenerX, listenerY, listenerYaw, playerX, playerY };

inline const float dbPerDoublingFreeField = 20.0f * std::log10 (2.0f); // 6.02 dB

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    const auto metres = [] (const char* id, const char* name, float defaultValue)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
                                                            name,
                                                            juce::NormalisableRange<float> { -25.0f, 25.0f, 0.01f },
                                                            defaultValue,
                                                            juce::AudioParameterFloatAttributes{}.withLabel ("m"));
    };

    return {
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { roomSize, 1 },
                                                     "Room size",
                                                     juce::NormalisableRange<float> { 5.0f, 50.0f, 5.0f },
                                                     20.0f,
                                                     juce::AudioParameterFloatAttributes{}.withLabel ("m")),
        metres (listenerX, "Listener X", 0.0f),
        metres (listenerY, "Listener Y", 0.0f),
        metres (playerX, "Player X", 0.0f),
        metres (playerY, "Player Y", 5.0f),
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { outputMode, 1 },
                                                      "Output",
                                                      juce::StringArray { "Stereo", "Binaural" },
                                                      0),

        // Pro panel. No step: the default must be exactly -20·log10(2)/3 dB per doubling.
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { proDistanceLaw, 1 }, "Distance law",
                                                     juce::NormalisableRange<float> { 0.0f, dbPerDoublingFreeField },
                                                     dbPerDoublingFreeField * geometry::distanceLawExponent,
                                                     juce::AudioParameterFloatAttributes{}.withLabel ("dB/doubling")),

        // Added after the first release: new parameters go at the end.
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { listenerYaw, 1 }, "Listener yaw",
                                                     juce::NormalisableRange<float> { -180.0f, 180.0f, 0.1f },
                                                     0.0f,
                                                     juce::AudioParameterFloatAttributes{}.withLabel (juce::String::fromUTF8 ("\xc2\xb0"))), // degree sign
    };
}
} // namespace params
