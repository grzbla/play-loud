x86_64-w64-mingw32-windres playloud/play.rc -O coff -o playloud/play.res
g++ -Wall -Wno-narrowing playloud/play.res -std=c++17 -O0 -pipe play.cpp -o play.exe -lws2_32 -mwindows
g++ -Wall -Wno-narrowing playloud/play.res -std=c++17 -O0 -pipe q.cpp -o q.exe -lws2_32 -mwindows
g++ -Wall -Wno-narrowing playloud/play.res -std=c++17 -O0 -pipe loud.cpp -o loud.exe -lws2_32 -mwindows
::g++ loud.cpp playloud/play.res -std=c++17 -o loud.exe -lws2_32 -mwindows -I./net -I./sys
::g++ play.cpp playloud/play.res -std=c++17 -o play.exe -lws2_32 -mwindows -I./net -I./sys
::g++ q.cpp playloud/play.res -std=c++17 -o q.exe -lws2_32 -mwindows -I./net -I./sys

