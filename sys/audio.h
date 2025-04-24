#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <filesystem>
#include <random>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <fstream>
#include <cstdlib>  // For std::exit
#include <functional> // For std::function
#ifdef _WIN32
#include <windows.h> // For ExitProcess
#include <stringapiset.h> // For UTF-8 conversion
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace Audio {

// Helper function for UTF-8 path handling
#ifdef _WIN32
std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    
    // First, get the required buffer size
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (size_needed <= 0) return std::wstring();
    
    // Allocate buffer and convert
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &result[0], size_needed);
    return result;
}
#endif

class Player {
public:
    // Define the type for the end of playback callback
    using PlaybackEndCallback = std::function<void()>;
    
    Player() {
        config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        
        // Auto-detect system channels instead of hardcoding to stereo
        ma_device_info* pPlaybackDeviceInfos;
        ma_uint32 playbackDeviceCount;
        ma_context context;
        
        if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
            // Fall back to stereo if we can't initialize context
            config.playback.channels = 2;
        } else {
            if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, NULL, NULL) != MA_SUCCESS) {
                // Fall back to stereo if we can't get device info
                config.playback.channels = 2;
            } else if (playbackDeviceCount > 0) {
                // Get default device channels
                ma_device_id defaultDeviceId = pPlaybackDeviceInfos[0].id;
                ma_device_info deviceInfo;
                
                if (ma_context_get_device_info(&context, ma_device_type_playback, &defaultDeviceId, &deviceInfo) == MA_SUCCESS) {
                    // Use system channels count, or fall back to stereo if unable to get specific info
                    if (deviceInfo.nativeDataFormatCount > 0) {
                        // Use channels from the first native format
                        config.playback.channels = deviceInfo.nativeDataFormats[0].channels;
                        std::cout << "Detected " << deviceInfo.nativeDataFormats[0].channels << " system audio channels\n";
                    } else {
                        // Default to stereo if no native format info available
                        config.playback.channels = 2;
                        std::cout << "No channel info detected, defaulting to stereo\n";
                    }
                } else {
                    config.playback.channels = 2;
                }
            } else {
                config.playback.channels = 2;
            }
            
            ma_context_uninit(&context);
        }
        
        config.sampleRate = 44100;
        config.dataCallback = dataCallback;
        config.pUserData = this;

        if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
            throw std::runtime_error("Failed to initialize audio device");
        }

        ma_device_start(&device);
    }

    ~Player() {
        stop();
        ma_device_uninit(&device);
    }

    // Play a file or directory
    void play(const std::string& path) {
        std::scoped_lock lock(mutex);
        stop_nolock();

        playlist.clear();
        playlistIndex = 0;
        currentPath.clear();
        buffer.clear();

        namespace fs = std::filesystem;
        fs::path p(path);
        
        try {
            if (fs::exists(p) && fs::is_directory(p)) {
                // Play directory
                for (const auto& entry : fs::directory_iterator(p)) {
                    if (entry.is_regular_file() && isAudioFile(entry.path().string())) {
                        playlist.push_back(entry.path().string());
                    }
                }

                if (!playlist.empty()) {
                    std::cout << "Playing " << playlist.size() << " tracks from directory\n";
                    std::shuffle(playlist.begin(), playlist.end(), std::mt19937{std::random_device{}()});
                    playlistIndex = 0;
                    currentPath = playlist[playlistIndex];
                    loadFromFile(currentPath);
                } else {
                    std::cout << "No audio files found in directory: " << path << "\n";
                    paused = true; // Set paused to prevent callback access to non-existent decoder
                    return;
                }
            } else {
                // Play single file
                currentPath = path;
                loadFromFile(currentPath);
            }
        } catch (const fs::filesystem_error& e) {
            // Fall back to treating as a single file
            std::cerr << "Filesystem error: " << e.what() << std::endl;
            currentPath = path;
            loadFromFile(currentPath);
        }

        paused = false;
    }

    // Play from memory buffer
    void play(const std::vector<uint8_t>& raw) {
        std::scoped_lock lock(mutex);
        stop_nolock();
        
        buffer = raw;
        decoder = std::make_unique<ma_decoder>();
        
        if (ma_decoder_init_memory(buffer.data(), buffer.size(), NULL, decoder.get()) != MA_SUCCESS) {
            decoder.reset();
        }
        
        paused = false;
    }

    void pause() {
        std::scoped_lock lock(mutex);
        paused = true;
    }

    void stop() {
        std::scoped_lock lock(mutex);
        stop_nolock();
    }

    void next() {
        std::scoped_lock lock(mutex);
        if (playlist.empty()) return;

        playlistIndex = (playlistIndex + 1) % playlist.size();
        currentPath = playlist[playlistIndex];
        loadFromFile(currentPath);
    }

    void prev() {
        std::scoped_lock lock(mutex);
        if (playlist.empty()) return;

        if (playlistIndex == 0)
            playlistIndex = playlist.size() - 1;
        else
            --playlistIndex;

        currentPath = playlist[playlistIndex];
        loadFromFile(currentPath);
    }

    void setVolume(float v) {
        std::scoped_lock lock(mutex);
        volume = std::clamp(v, 0.0f, 1.0f);
    }

    // Ensure application exits properly when quit is called
    void quit() {
        std::cout << "Quit signal received, exiting application\n";
        
        // First stop any playback and clean up resources
        stop();
        
        // Uninitialize audio device before exit to ensure a clean shutdown
        ma_device_uninit(&device);
        
        // Force exit the application with success code
        #ifdef _WIN32
        // For Windows, use ExitProcess for a cleaner immediate exit
        ExitProcess(0);
        #else
        _exit(0); // POSIX direct exit call, bypasses atexit handlers
        #endif
    }

    // Set callback for when playback reaches the end of a track
    void setOnPlaybackEnd(PlaybackEndCallback callback) {
        std::scoped_lock lock(mutex);
        onPlaybackEndCallback = callback;
    }

private:
    ma_device_config config;
    ma_device device;
    std::mutex mutex;

    std::unique_ptr<ma_decoder> decoder;
    std::vector<std::string> playlist;
    std::vector<uint8_t> buffer;
    size_t playlistIndex = 0;
    std::string currentPath;
    
    float volume = 1.0f;
    bool paused = false;
    PlaybackEndCallback onPlaybackEndCallback;

    void stop_nolock() {
        decoder.reset();
        buffer.clear();
        currentPath.clear();
        paused = false;
    }

    void loadFromFile(const std::string& path) {
        decoder = std::make_unique<ma_decoder>();
        
        // Check if path exists before attempting to decode
        namespace fs = std::filesystem;
        if (!fs::exists(path)) {
            std::cerr << "File not found: " << path << "\n";
            decoder.reset();
            return;
        }
        
        // Get the current device channel count for proper channel mapping
        ma_uint32 deviceChannels = device.playback.channels;
        
        // Configure decoder to match output device settings
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, deviceChannels, device.sampleRate);
        
        // Handle paths with Unicode characters
        ma_result result;
        #ifdef _WIN32
        // On Windows, convert UTF-8 to wide string for proper Unicode support
        std::wstring widePath = utf8_to_wstring(path);
        if (!widePath.empty()) {
            result = ma_decoder_init_file_w(widePath.c_str(), &decoderConfig, decoder.get());
        } else {
            // Fallback to direct path if conversion failed
            result = ma_decoder_init_file(path.c_str(), &decoderConfig, decoder.get());
        }
        #else
        // On other platforms, standard UTF-8 path should work
        result = ma_decoder_init_file(path.c_str(), &decoderConfig, decoder.get());
        #endif
        
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to load: " << path << "\n";
            decoder.reset();
        } else {
            std::cout << "Playing: " << path << "\n";
            std::cout << "  Channels: " << deviceChannels << ", Sample rate: " << device.sampleRate << " Hz\n";
        }
    }

    // Check if file is a supported audio format
    bool isAudioFile(const std::string& path) {
        auto pos = path.find_last_of(".");
        if (pos == std::string::npos) return false;
        
        std::string ext = path.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        static const std::vector<std::string> audioExts = {
            "mp3", "wav", "ogg", "flac", "aac", "wma", "m4a", "aiff", "opus"
        };
        
        return std::find(audioExts.begin(), audioExts.end(), ext) != audioExts.end();
    }

    static void dataCallback(ma_device* device, void* out, const void* in, ma_uint32 frames) {
        Player* self = static_cast<Player*>(device->pUserData);
        std::scoped_lock lock(self->mutex);

        if (!self->decoder || self->paused) {
            std::memset(out, 0, frames * ma_get_bytes_per_frame(device->playback.format, device->playback.channels));
            return;
        }

        // Create a temporary buffer for initial decoded data
        // Most audio files are stereo, so we'll read in stereo format first
        const ma_uint32 tempChannels = 2; // Most audio files are stereo
        float tempBuffer[4096 * tempChannels]; // 4096 frames should be enough for a chunk
        ma_uint64 framesRead;

        // Read from decoder in its native channel count
        ma_uint32 decoderChannels = self->decoder->outputChannels;
        if (frames * decoderChannels > sizeof(tempBuffer) / sizeof(float)) {
            frames = sizeof(tempBuffer) / sizeof(float) / decoderChannels;
        }

        ma_decoder_read_pcm_frames(self->decoder.get(), tempBuffer, frames, &framesRead);

        // Map the audio channels properly to the output device
        float* outputBuffer = reinterpret_cast<float*>(out);
        ma_uint32 outputChannels = device->playback.channels;

        // Clear output buffer first
        std::memset(out, 0, frames * ma_get_bytes_per_frame(device->playback.format, outputChannels));

        // Upmix/downmix channels
        for (ma_uint64 frame = 0; frame < framesRead; frame++) {
            if (decoderChannels == 1 && outputChannels >= 1) {
                // Mono to multi-channel (duplicate to all channels)
                float sample = tempBuffer[frame];
                for (ma_uint32 channel = 0; channel < outputChannels; channel++) {
                    outputBuffer[frame * outputChannels + channel] = sample;
                }
            }
            else if (decoderChannels == 2 && outputChannels >= 2) {
                // Stereo to multi-channel (common case)
                float leftSample = tempBuffer[frame * 2];
                float rightSample = tempBuffer[frame * 2 + 1];
                
                // Front left and right (first two channels)
                outputBuffer[frame * outputChannels + 0] = leftSample; // Front Left
                outputBuffer[frame * outputChannels + 1] = rightSample; // Front Right
                
                // Handle 5.1, 7.1, etc.
                if (outputChannels >= 6) {
                    // Center = (left+right)/2 with slight attenuation to prevent clipping
                    outputBuffer[frame * outputChannels + 2] = (leftSample + rightSample) * 0.7f;
                    
                    // LFE (subwoofer) - low frequencies only, reduce volume
                    outputBuffer[frame * outputChannels + 3] = (leftSample + rightSample) * 0.3f;
                    
                    // Surround/rear channels - lower volume to prevent overwhelming sound
                    if (outputChannels >= 5) {
                        outputBuffer[frame * outputChannels + 4] = leftSample * 0.5f; // Rear Left
                        if (outputChannels >= 6) {
                            outputBuffer[frame * outputChannels + 5] = rightSample * 0.5f; // Rear Right
                        }
                    }
                }
            }
            else if (decoderChannels >= 2 && outputChannels >= 1) {
                // Multi-channel to fewer channels - simple downmix
                for (ma_uint32 outChannel = 0; outChannel < outputChannels; outChannel++) {
                    float sum = 0;
                    for (ma_uint32 inChannel = 0; inChannel < decoderChannels; inChannel++) {
                        sum += tempBuffer[frame * decoderChannels + inChannel];
                    }
                    outputBuffer[frame * outputChannels + outChannel] = sum / decoderChannels;
                }
            }
        }

        // Apply volume if needed
        if (self->volume != 1.0f) {
            size_t count = framesRead * outputChannels;
            for (size_t i = 0; i < count; ++i) {
                outputBuffer[i] *= self->volume;
            }
        }

        // Auto-advance to next track if this one ended and we have a playlist
        if (framesRead < frames) {
            // Call user-defined callback if set
            bool handledByCallback = false;
            if (self->onPlaybackEndCallback) {
                // Make a local copy of the callback to avoid issues if it's modified during execution
                auto callback = self->onPlaybackEndCallback;
                
                // Release the lock while calling the callback to avoid deadlocks
                self->mutex.unlock();
                callback();
                self->mutex.lock();
                
                // The callback has handled the end of playback
                handledByCallback = true;
            }
            
            // Only proceed with built-in playlist handling if callback didn't handle it
            if (!handledByCallback && !self->playlist.empty() && self->playlist.size() > 1) {
                self->playlistIndex = (self->playlistIndex + 1) % self->playlist.size();
                self->currentPath = self->playlist[self->playlistIndex];
                
                // Check if the file exists before creating a new decoder
                namespace fs = std::filesystem;
                if (!fs::exists(self->currentPath)) {
                    std::cerr << "Next track file not found: " << self->currentPath << "\n";
                    
                    // Try to find the next valid track in playlist
                    bool foundValid = false;
                    
                    // Try each track in the playlist until we find one that exists
                    for (size_t i = 0; i < self->playlist.size(); i++) {
                        self->playlistIndex = (self->playlistIndex + 1) % self->playlist.size();
                        self->currentPath = self->playlist[self->playlistIndex];
                        
                        if (fs::exists(self->currentPath)) {
                            foundValid = true;
                            break;
                        }
                    }
                    
                    // If no valid tracks found, reset the decoder and return silence
                    if (!foundValid) {
                        std::cerr << "No valid tracks found in playlist\n";
                        self->decoder.reset();
                        
                        // Fill the rest of the buffer with silence
                        std::memset(out, 0, frames * ma_get_bytes_per_frame(device->playback.format, device->playback.channels));
                        return;
                    }
                }
                
                // Create a new decoder for the next file
                auto oldDecoder = std::move(self->decoder);
                self->decoder = std::make_unique<ma_decoder>();
                
                // Get current device parameters for proper channel mapping
                ma_uint32 deviceChannels = device->playback.channels;
                ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, deviceChannels, device->sampleRate);
                
                // Load next track with UTF-8 support
                ma_result result;
                #ifdef _WIN32
                // On Windows, convert UTF-8 to wide string for proper Unicode support
                std::wstring widePath = utf8_to_wstring(self->currentPath);
                if (!widePath.empty()) {
                    result = ma_decoder_init_file_w(widePath.c_str(), &decoderConfig, self->decoder.get());
                } else {
                    // Fallback to direct path if conversion failed
                    result = ma_decoder_init_file(self->currentPath.c_str(), &decoderConfig, self->decoder.get());
                }
                #else
                result = ma_decoder_init_file(self->currentPath.c_str(), &decoderConfig, self->decoder.get());
                #endif
                
                if (result == MA_SUCCESS) {
                    std::cout << "Next track: " << self->currentPath << "\n";
                    std::cout << "  Channels: " << deviceChannels << ", Sample rate: " << device->sampleRate << " Hz\n";
                    
                    // Fill remainder of buffer with beginning of new track using the same channel mapping logic
                    if (framesRead < frames) {
                        // Clear remainder of buffer first
                        size_t offsetBytes = framesRead * ma_get_bytes_per_frame(device->playback.format, outputChannels);
                        size_t remainingBytes = (frames - framesRead) * ma_get_bytes_per_frame(device->playback.format, outputChannels);
                        std::memset(static_cast<char*>(out) + offsetBytes, 0, remainingBytes);
                        
                        // Read and map channels for the new track
                        ma_uint64 additionalFramesRead;
                        decoderChannels = self->decoder->outputChannels;
                        
                        float additionalTempBuffer[4096 * 8]; // Support up to 8 channels for reading
                        ma_uint64 remainingFrames = frames - framesRead;
                        if (remainingFrames * decoderChannels > sizeof(additionalTempBuffer) / sizeof(float)) {
                            remainingFrames = sizeof(additionalTempBuffer) / sizeof(float) / decoderChannels;
                        }
                        
                        ma_decoder_read_pcm_frames(self->decoder.get(), additionalTempBuffer, remainingFrames, &additionalFramesRead);
                        
                        // Map additional frames
                        for (ma_uint64 frame = 0; frame < additionalFramesRead; frame++) {
                            if (decoderChannels == 1 && outputChannels >= 1) {
                                // Mono to multi-channel
                                float sample = additionalTempBuffer[frame];
                                for (ma_uint32 channel = 0; channel < outputChannels; channel++) {
                                    outputBuffer[(framesRead + frame) * outputChannels + channel] = sample;
                                }
                            }
                            else if (decoderChannels == 2 && outputChannels >= 2) {
                                // Stereo to multi-channel
                                float leftSample = additionalTempBuffer[frame * 2];
                                float rightSample = additionalTempBuffer[frame * 2 + 1];
                                
                                outputBuffer[(framesRead + frame) * outputChannels + 0] = leftSample;
                                outputBuffer[(framesRead + frame) * outputChannels + 1] = rightSample;
                                
                                if (outputChannels >= 6) {
                                    outputBuffer[(framesRead + frame) * outputChannels + 2] = (leftSample + rightSample) * 0.7f;
                                    outputBuffer[(framesRead + frame) * outputChannels + 3] = (leftSample + rightSample) * 0.3f;
                                    
                                    if (outputChannels >= 5) {
                                        outputBuffer[(framesRead + frame) * outputChannels + 4] = leftSample * 0.5f;
                                        if (outputChannels >= 6) {
                                            outputBuffer[(framesRead + frame) * outputChannels + 5] = rightSample * 0.5f;
                                        }
                                    }
                                }
                            }
                            else if (decoderChannels >= 2 && outputChannels >= 1) {
                                // Multi-channel to fewer channels
                                for (ma_uint32 outChannel = 0; outChannel < outputChannels; outChannel++) {
                                    float sum = 0;
                                    for (ma_uint32 inChannel = 0; inChannel < decoderChannels; inChannel++) {
                                        sum += additionalTempBuffer[frame * decoderChannels + inChannel];
                                    }
                                    outputBuffer[(framesRead + frame) * outputChannels + outChannel] = sum / decoderChannels;
                                }
                            }
                        }
                        
                        framesRead += additionalFramesRead;
                    }
                } else {
                    self->decoder.reset();
                }
            }
        }

        // Fill remainder with silence if needed
        if (framesRead < frames) {
            size_t offsetBytes = framesRead * ma_get_bytes_per_frame(device->playback.format, device->playback.channels);
            size_t remainingBytes = (frames - framesRead) * ma_get_bytes_per_frame(device->playback.format, device->playback.channels);
            std::memset(static_cast<char*>(out) + offsetBytes, 0, remainingBytes);
        }
    }
};

} // namespace Audio 