#include "net/udpr.h"
#include "sys/audio.h"
#include <string>
#include <thread>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <queue>
#include <deque>
#include <vector>
#include <algorithm>
#include <random>
#include <windows.h>
#include <chrono>

// Note: Console window is hidden by compiling with -mwindows flag

std::atomic<bool> running{true};
// Queue for audio files
std::deque<std::string> audioQueue;
// Track whether we're currently playing from the queue
std::atomic<bool> playingFromQueue{false};
// Track what's currently playing for scrobbling
std::string currentlyPlaying;
// Scrobble history
std::deque<std::string> playHistory;
const size_t MAX_HISTORY = 20;

// Signal handler for clean shutdown
void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
    }
}

// Function prototypes
void handleStopCommand(Audio::Player& player);
void handleNextCommand(Audio::Player& player);
void handlePrevCommand(Audio::Player& player);
void handleQuitCommand(Audio::Player& player);
void handlePlayCommand(const std::string& filePath, Audio::Player& player);
void handleQueueCommand(const std::string& filePath, Audio::Player& player);
void handleLegacyCommand(const std::string& msg, Audio::Player& player);
void playNextFromQueue(Audio::Player& player);
void addToHistory(const std::string& track);
int collectAudioFiles(const std::filesystem::path& dirPath, std::vector<std::string>& outFiles);

// Play the next track from the queue
void playNextFromQueue(Audio::Player& player) {
    if (!audioQueue.empty()) {
        std::string nextTrack = audioQueue.front();
        audioQueue.pop_front();
        
        try {
            namespace fs = std::filesystem;
            fs::path path(nextTrack);
            
            if (fs::exists(path)) {
                
                // Add current track to history before starting new one
                if (!currentlyPlaying.empty()) {
                    playHistory.push_front(currentlyPlaying);
                    // Keep history size limited
                    if (playHistory.size() > MAX_HISTORY) {
                        playHistory.pop_back();
                    }
                }
                
                // Update currently playing track
                currentlyPlaying = nextTrack;
                
                // Play the file
                player.play(nextTrack);
                playingFromQueue = true;
            } else {
                // If this file failed, try the next one
                if (!audioQueue.empty()) {
                    playNextFromQueue(player);
                } else {
                    playingFromQueue = false;
                }
            }
        } catch (const std::exception&) {
            if (!audioQueue.empty()) {
                playNextFromQueue(player);
            } else {
                playingFromQueue = false;
            }
        }
    } else {
        playingFromQueue = false;
    }
}

// int main(int argc, char** argv) {
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Set up signal handling
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    
    Audio::Player player;
    
    
    // Set up callback to handle end of playback
    player.setOnPlaybackEnd([&]() {
        if (playingFromQueue && !audioQueue.empty()) {
            playNextFromQueue(player);
        } else {
            playingFromQueue = false;
        }
    });

    UDP::Receiver receiver(7001, [&](const std::string& msg) {
        // Handle empty message - stop playback
        if (msg.empty()) {
            handleStopCommand(player);
            return;
        }
        
        // Handle direct commands
        if (msg == "n") {
            handleNextCommand(player);
            return;
        }
        
        if (msg == "p") {
            handlePrevCommand(player);
            return;
        }
        
        if (msg == "q") {
            handleQuitCommand(player);
            return;
        }
        
        // Handle prefixed commands
        if (msg.rfind("play:", 0) == 0) {
            handlePlayCommand(msg.substr(5), player);
            return;
        }
        
        if (msg.rfind("q:", 0) == 0) {
            handleQueueCommand(msg.substr(2), player);
            return;
        }
        
        // Handle legacy direct filepath
        handleLegacyCommand(msg, player);
    });
    // Main event loop
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    player.stop();
    
    return 0;
}

// Handler function implementations
void handleStopCommand(Audio::Player& player) {
    player.stop();
    playingFromQueue = false; // Reset queue state when manually stopped
}

void handleNextCommand(Audio::Player& player) {
    if (audioQueue.empty()) {
        return; // Nothing to play next
    }
    
    // Play next track from queue regardless of current playback state
    playNextFromQueue(player);
}

void handlePrevCommand(Audio::Player& player) {
    if (playHistory.empty()) {
        return; // No previous tracks
    }
    
    // Get previous track from history
    std::string prevTrack = playHistory.front();
    playHistory.pop_front();
    
    // Put current track back at front of queue if it exists
    if (!currentlyPlaying.empty()) {
        audioQueue.push_front(currentlyPlaying);
    }
    
    // Update currently playing and play it
    currentlyPlaying = prevTrack;
    player.play(prevTrack);
    playingFromQueue = true;
}

void handleQuitCommand(Audio::Player& player) {
    // Stop any playing audio and clear state
    player.stop();
    audioQueue.clear();
    playHistory.clear();
    currentlyPlaying.clear();
    
    // Exit the application
    player.quit();
}

void handlePlayCommand(const std::string& filePath, Audio::Player& player) {
    try {
        namespace fs = std::filesystem;
        fs::path path(filePath);
        
        if (!fs::exists(path)) {
            return;
        }
        
        // Clear the queue when starting with a direct play command
        audioQueue.clear();
        
        if (fs::is_directory(path)) {
            // For directories, add all files to the queue
            std::vector<std::string> dirFiles;
            collectAudioFiles(path, dirFiles);
            
            if (dirFiles.empty()) {
                return;
            }
            
            // Shuffle the files
            std::shuffle(dirFiles.begin(), dirFiles.end(), std::mt19937{std::random_device{}()});
            
            // Save current track to history
            addToHistory(currentlyPlaying);
            
            // Update currently playing track
            currentlyPlaying = dirFiles[0];
            
            // Play first track
            player.play(dirFiles[0]);
            
            // Add rest to queue
            for (size_t i = 1; i < dirFiles.size(); i++) {
                audioQueue.push_back(dirFiles[i]);
            }
            
            // We're playing from the queue now
            playingFromQueue = true;
        } else {
            // Add current track to history
            addToHistory(currentlyPlaying);
            
            // Update currently playing
            currentlyPlaying = filePath;
            
            // Play the file
            player.play(filePath);
            
            // Since we're creating a new play command, we're now playing from queue
            playingFromQueue = true;
        }
    } catch (const std::exception&) {
        // Handle exception silently
    }
}

void handleQueueCommand(const std::string& filePath, Audio::Player& player) {
    try {
        namespace fs = std::filesystem;
        fs::path path(filePath);
        
        if (!fs::exists(path)) {
            return;
        }
        
        if (fs::is_directory(path)) {
            // For directories, add all files to the queue
            std::vector<std::string> dirFiles;
            collectAudioFiles(path, dirFiles);
            
            if (dirFiles.empty()) {
                return;
            }
            
            // Shuffle the files before adding to queue
            std::shuffle(dirFiles.begin(), dirFiles.end(), std::mt19937{std::random_device{}()});
            
            // Add all files to queue
            for (const auto& file : dirFiles) {
                audioQueue.push_back(file);
            }
            
            // If nothing is currently playing, start playing from queue
            if (currentlyPlaying.empty()) {
                playNextFromQueue(player);
            } else {
                // If something is already playing, make sure we're in queue mode
                playingFromQueue = true;
            }
        } else {
            audioQueue.push_back(filePath);
            
            // If nothing is currently playing, start playing this file
            if (currentlyPlaying.empty()) {
                playNextFromQueue(player);
            } else {
                // If something is already playing, make sure we're in queue mode
                playingFromQueue = true;
            }
        }
    } catch (const std::exception&) {
        // Handle exception silently
    }
}

void handleLegacyCommand(const std::string& msg, Audio::Player& player) {
    try {
        namespace fs = std::filesystem;
        fs::path path(msg);
        
        if (fs::exists(path)) {
            if (fs::is_directory(path)) {
                // Directory processing
            } else {
                // File processing
            }
            
            // Add to history if we're switching tracks
            addToHistory(currentlyPlaying);
            
            // Update currently playing
            currentlyPlaying = msg;
            
            // Since we're manually playing something, we're not playing from queue
            playingFromQueue = false;
        }
        
        player.play(msg);
    } catch (const std::exception&) {
        // Handle exception silently
    }
}

// Helper function to add a track to history
void addToHistory(const std::string& track) {
    if (track.empty()) return;
    
    playHistory.push_front(track);
    // Keep history size limited
    if (playHistory.size() > MAX_HISTORY) {
        playHistory.pop_back();
    }
}

// Helper function to collect audio files from a directory
int collectAudioFiles(const std::filesystem::path& dirPath, std::vector<std::string>& outFiles) {
    int count = 0;
    namespace fs = std::filesystem;
    
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            // Check for audio files
            std::string entryPath = entry.path().string();
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            // Simple audio extension check
            if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac" || 
                ext == ".aac" || ext == ".wma" || ext == ".m4a") {
                outFiles.push_back(entryPath);
                count++;
            }
        }
    }
    
    return count;
}

