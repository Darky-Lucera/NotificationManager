#if defined(_WIN32) && !defined(__CYGWIN32__)
    #include <windows.h>
#endif
#include <cmath>
#include <stdarg.h>
#include "agent.h"
#include "notifications/NotificationManager.h"
#include "NotificationId.h"

//-------------------------------------
using MindShake::NotificationManager;
using MindShake::NotificationId;

#if __cplusplus >= 201703L
    using namespace std::chrono_literals;
#else
    constexpr std::chrono::milliseconds operator"" ms(unsigned long long value) noexcept {
        return std::chrono::milliseconds(value);
    }
#endif

//-------------------------------------
void
Clear() {
#if defined(_WIN32) && !defined(__CYGWIN32__)
    //clrscr(); // Does not exists anymore
    COORD topLeft  = { 0, 0 };
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO screen;
    DWORD written;

    //SetConsoleTextAttribute(console, 0);
    GetConsoleScreenBufferInfo(console, &screen);
    FillConsoleOutputCharacterA(console, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    FillConsoleOutputAttribute(console, 0, screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(console, topLeft);
#else
    fprintf(stdout, "\033[2J\033[1;1H");
#endif
}

//-------------------------------------
struct TextBuffer {
    char    buffer[80 * 50] { 0 };
    size_t  index = 0;

    void    clear() { index = 0; }
    int     printf(const char* msg, ...) {
        int size;
        va_list arg;
        va_start(arg, msg);
        size = vsnprintf(buffer + index, sizeof(buffer) - index - 1, msg, arg);
        index += size;
        va_end(arg);

        return size;
    }
};

//-------------------------------------
void
DrawHPBar(const Agent &agent, TextBuffer &text) {
    int32_t i;

    int size = text.printf("%s:", agent.mName.c_str());
    for (i = size; i < 12; ++i) {
        text.printf(" ");
    }
    text.printf("[");
    for (i = 0; i < ceil(agent.mHP); ++i) {
        text.printf("#");
    }
    for (; i < int32_t(agent.mMaxHP); ++i) {
        text.printf(" ");
    }
    text.printf("]\n");
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    bool        keepRunning = true;
    bool        drawConsole = true;
    Agent       orc("Orc", 20, 4, 3, 1000);
    Agent       goblinA("Goblin A", 12, 1.1f, 1.1f, 250);
    Agent       goblinB("Goblin B", 10, 1.0f, 1.1f, 250);
    Agent       goblinC("Goblin C", 10, 1.1f, 1.0f, 250);
    std::string console[16];
    int32_t     consoleIndex = 0;
    int32_t     consoleLines = 0;
    TextBuffer  text;

    // This is a mono-thread application
    NotificationManager::SetMT(false);

    NotificationManager::GetDelegateForThisThread(NotificationId::Log)
        .Add([&](NotificationId id, const any &data) {
            std::string msg = any_cast<std::string>(data);
            console[consoleIndex] = msg;
            consoleIndex = (consoleIndex + 1) & 0x0F;
            text.clear();
            text.printf("Orc fighting against 3 goblins!\n");
            text.printf("-------------------------------\n");
            DrawHPBar(orc, text);
            DrawHPBar(goblinA, text);
            DrawHPBar(goblinB, text);
            DrawHPBar(goblinC, text);
            text.printf("-------------------------------\n");
            if (consoleLines < 16) {
                ++consoleLines;
                for (int32_t i = 0; i < consoleLines; ++i) {
                    text.printf("%s\n", console[i].c_str());
                }
            }
            else {
                for (int32_t i = consoleIndex-16; i < consoleIndex; ++i) {
                    text.printf("%s\n", console[i & 0x0F].c_str());
                }
            }
            text.printf("-------------------------------\n");
            drawConsole = true;
        }
    );

    NotificationManager::GetDelegateForThisThread(NotificationId::Kill)
        .Add([&](NotificationId id, const any &data) {
            Agent *dead = any_cast<Agent *>(data);
            if (dead == &goblinA) {
                orc.SetEnemy(&goblinB);
            }
            else if (dead == &goblinB) {
                orc.SetEnemy(&goblinC);
            }
            else {
                keepRunning = false;
            }
        }
    );

    orc.SetEnemy(&goblinA);
    goblinA.SetEnemy(&orc);
    goblinB.SetEnemy(&orc);
    goblinC.SetEnemy(&orc);
    while(keepRunning) {
        if (drawConsole) {
            drawConsole = false;
            Clear();
            printf("%s", text.buffer);
            fflush(NULL);
        }

        orc.Run();
        goblinA.Run();
        goblinB.Run();
        goblinC.Run();
        std::this_thread::sleep_for(50ms);
    }

    Clear();
    printf("%s", text.buffer);
    fflush(NULL);

    if (orc.mHP > 0) {
        printf("Orc wins!\n");
    }
    else {
        printf("Goblins win!\n");
    }

    NotificationManager::Clear();

    return 0;
}
