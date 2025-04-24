#include <winsock2.h> 
#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <shellapi.h>
#include <tlhelp32.h>
#include "net/udps.h"

// Note: Console window is hidden by compiling with -mwindows flag

bool isLoudRunning() {
    // Use process enumeration instead of command execution
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (!Process32First(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        return false;
    }
    
    bool found = false;
    do {
        if (_stricmp(pe32.szExeFile, "loud.exe") == 0) {
            found = true;
            break;
        }
    } while (Process32Next(hSnapshot, &pe32));
    
    CloseHandle(hSnapshot);
    return found;
}

void startLoudSilently() {
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hide the window completely

    // Start loud.exe with CREATE_NO_WINDOW flag to prevent console window
    CreateProcessA(
        NULL,
        (LPSTR)"loud.exe",
        NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, // Add this flag to prevent console window
        NULL, NULL,
        &si, &pi
    );

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    // Wait for loud to initialize properly with more reliable checking
    for (int i = 0; i < 20; i++) {
        if (isLoudRunning()) {
            Sleep(1000);
            break;
        }
        Sleep(1000);
    }
}

// Wait for loud to be ready by testing UDP connection
bool waitForLoudReady() {
    const int MAX_ATTEMPTS = 5;
    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        try {
            // Try to open a connection to see if loud is accepting UDP
            UDP::Socket testSock("127.0.0.1", 7001);
            
            // Send an empty message (which just stops playback if something is playing)
            // This is harmless but verifies the connection works
            testSock.send(""); 
            
            // If we got here, the connection worked
            Sleep(500);
            return true;
        }
        catch (const std::exception&) {
            // Socket connection failed, wait and try again
            Sleep(1000);
        }
    }
    return false;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    try {
        bool justStarted = false;
        
        // Start loud if it's not running
        if (!isLoudRunning()) {
            startLoudSilently();
            justStarted = true;
        }
        
        // If we just started loud, make sure it's ready for commands
        if (justStarted) {
            waitForLoudReady();
        }
        
        // Create socket
        UDP::Socket sock("127.0.0.1", 7001);
        
        // Get first command line argument, skipping program name
        std::string arg;
        
        // Parse command line arguments (skip program name)
        LPWSTR *szArglist;
        int nArgs;
        
        szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
        if (szArglist != NULL && nArgs > 1) {
            // Convert wide string to utf8
            int size = WideCharToMultiByte(CP_UTF8, 0, szArglist[1], -1, NULL, 0, NULL, NULL);
            if (size > 0) {
                std::vector<char> buffer(size);
                WideCharToMultiByte(CP_UTF8, 0, szArglist[1], -1, buffer.data(), size, NULL, NULL);
                arg = buffer.data();
            }
            LocalFree(szArglist);
        }
        
        // Handle command based on argument
        if (arg.empty()) {
            sock.send(""); // Stop playback
        } else if (arg == "q") {
            sock.send("q"); // Quit
            Sleep(1000);   // Wait for quit
        } else {
            // Everything else is added to queue
            sock.send("q:" + arg);
        }

    } catch (const std::exception&) {
        return 1;
    }

    return 0;
} 