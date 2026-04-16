#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>

class KeyStateDetector {
public:
    KeyStateDetector() {
        tcgetattr(STDIN_FILENO, &old_t);
        new_t = old_t;
        new_t.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_t);
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    ~KeyStateDetector() {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    }

    int getKeyState() {
        unsigned char ch;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            last_key = ch;
            last_time = std::chrono::steady_clock::now();
            return last_key;
        }

        // 如果超过 100ms 没有收到新字符，认为松开了
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
        if (duration.count() > 10) { 
            last_key = -1;
        }

        return last_key;
    }

private:
    struct termios old_t, new_t;
    int last_key = -1;
    std::chrono::steady_clock::time_point last_time;
};

int main() {
    KeyStateDetector detector;
    while (true) {
        int state = detector.getKeyState();
        std::cout << "\r当前状态: " << state << "      " << std::flush;
        if (state == 'q') break;
        usleep(20000); // 20ms 采样一次
    }
    return 0;
}