#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>

#ifdef __APPLE__
#include <cstdint>
extern "C" bool CGEventSourceKeyState(int32_t stateID, uint16_t key);
namespace MacKeys {
    constexpr int32_t STATE_COMBINED = 0;
    constexpr uint16_t SPACE = 49;
    constexpr uint16_t W     = 13;
    constexpr uint16_t UP    = 126;
    constexpr uint16_t S     = 1;
    constexpr uint16_t DOWN  = 125;
}
#endif

// ==========================================
// Hardware Key State Polling (macOS Native + Fallback)
// ==========================================
inline bool is_key_physically_down(uint16_t keycode) {
#ifdef __APPLE__
    return CGEventSourceKeyState(MacKeys::STATE_COMBINED, keycode);
#else
    (void)keycode;
    return false;
#endif
}

// ==========================================
// ANSI Color Constants (100% 1-cell width)
// ==========================================
namespace Style {
    const std::string RESET       = "\033[0m";
    const std::string BOLD        = "\033[1m";
    const std::string DIM         = "\033[2m";

    const std::string WHITE       = "\033[1;38;5;255m";
    const std::string GRAY        = "\033[38;5;246m";
    const std::string DARK_GRAY   = "\033[38;5;240m";
    const std::string DINO_GREEN  = "\033[1;38;5;46m";
    const std::string DINO_EYE    = "\033[1;38;5;196m";
    const std::string CACTUS      = "\033[1;38;5;34m";
    const std::string PTERO       = "\033[1;38;5;214m";
    const std::string FRAME_DINO  = "\033[1;38;5;220m"; // Amber gold
    const std::string FRAME_GEO   = "\033[1;38;5;51m";  // Cyber cyan
    const std::string GROUND_LINE = "\033[1;38;5;178m";
    const std::string SOIL_TEXT   = "\033[1;38;5;172m"; // High-contrast warm amber
    const std::string ROCK_TEXT   = "\033[1;38;5;248m"; // Bright visible stone
    const std::string CLOUD       = "\033[38;5;252m";
    const std::string SUN         = "\033[1;38;5;220m";
    const std::string STAR        = "\033[1;38;5;226m";
    const std::string MOON        = "\033[1;38;5;230m";
    const std::string MOUNTAIN    = "\033[38;5;241m";

    // Geometry Dash Neon
    const std::string NEON_CYAN   = "\033[1;38;5;51m";
    const std::string NEON_YELLOW = "\033[1;38;5;226m";
    const std::string NEON_PINK   = "\033[1;38;5;201m";
    const std::string NEON_GREEN  = "\033[1;38;5;46m";
    const std::string NEON_RED    = "\033[1;38;5;196m";
    const std::string CYBER_GRID  = "\033[1;38;5;39m";
    const std::string CYBER_DEEP  = "\033[38;5;27m";
    const std::string HINT_TEXT   = "\033[1;38;5;222m";
}

// ==========================================
// Terminal Safe I/O
// ==========================================
void safe_write_all(const std::string& str) {
    const char* ptr = str.data();
    size_t remaining = str.size();
    while (remaining > 0) {
        ssize_t written = write(STDOUT_FILENO, ptr, remaining);
        if (written <= 0) {
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            break;
        }
        ptr += written;
        remaining -= (size_t)written;
    }
}

// ==========================================
// Terminal Raw Mode Control
// ==========================================
struct termios orig_termios;
bool raw_mode_enabled = false;
volatile sig_atomic_t g_running = 1;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
    }
}

void disable_raw_mode() {
    if (raw_mode_enabled) {
        std::cout << "\033[?7h"       // Re-enable auto-wrap
                  << "\033[?25h"      // Show cursor
                  << "\033[?1049l"    // Leave alternate screen buffer
                  << "\033[0m"        // Reset styles
                  << std::flush;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = false;
    }
}

void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) return;
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    // VMIN = 0, VTIME = 0 provides non-blocking read natively in termios
    // without touching fcntl/O_NONBLOCK (which would corrupt STDOUT)!
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_enabled = true;

    std::cout << "\033[?1049h\033[?25l\033[?7l\033[2J\033[3J\033[H" << std::flush;
}

// ==========================================
// Audio Support (macOS afplay)
// ==========================================
bool g_sound_enabled = true;

void play_sound(const std::string& sound_name) {
    if (!g_sound_enabled) return;
    std::string cmd = "afplay -v 0.35 /System/Library/Sounds/" + sound_name + ".aiff >/dev/null 2>&1 &";
    system(cmd.c_str());
}

// ==========================================
// UTF-8 Helper (1-cell character splitter)
// ==========================================
std::vector<std::string> split_utf8(const std::string& str) {
    std::vector<std::string> glyphs;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = (unsigned char)str[i];
        size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;

        if (i + len <= str.size()) {
            glyphs.push_back(str.substr(i, len));
        }
        i += len;
    }
    return glyphs;
}

// ==========================================
// Screen Buffer with Direct Cursor Addressing
// ==========================================
class Screen {
public:
    int width = 80;
    int height = 24;
    std::vector<std::string> chars;
    std::vector<std::string> styles;

    void resize(int w, int h) {
        width = std::max(55, w);
        height = std::max(18, h);
        chars.assign(width * height, " ");
        styles.assign(width * height, Style::RESET);
    }

    void clear() {
        std::fill(chars.begin(), chars.end(), " ");
        std::fill(styles.begin(), styles.end(), Style::RESET);
    }

    void set_cell(int x, int y, const std::string& ch, const std::string& style) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            int idx = y * width + x;
            chars[idx] = ch;
            if (!style.empty()) styles[idx] = style;
        }
    }

    void print_str(int x, int y, const std::string& str, const std::string& style) {
        if (y < 0 || y >= height) return;
        auto glyphs = split_utf8(str);
        for (size_t i = 0; i < glyphs.size(); ++i) {
            int tx = x + (int)i;
            if (tx >= 0 && tx < width) {
                set_cell(tx, y, glyphs[i], style);
            }
        }
    }

    void print_centered(int y, const std::string& str, const std::string& style) {
        if (y < 0 || y >= height) return;
        auto glyphs = split_utf8(str);
        int x = (width - (int)glyphs.size()) / 2;
        print_str(x, y, str, style);
    }

    void draw_sprite(int x, int y, const std::vector<std::string>& sprite, const std::string& style) {
        for (size_t r = 0; r < sprite.size(); ++r) {
            int row_y = y + (int)r;
            if (row_y < 0 || row_y >= height) continue;
            auto glyphs = split_utf8(sprite[r]);
            for (size_t c = 0; c < glyphs.size(); ++c) {
                int col_x = x + (int)c;
                if (col_x > 0 && col_x < width - 1) {
                    if (glyphs[c] != " ") {
                        set_cell(col_x, row_y, glyphs[c], style);
                    }
                }
            }
        }
    }

    // Direct cursor positioning: guarantees 0% terminal scrolling
    void flush_to_terminal() {
        std::string buffer;
        buffer.reserve(width * height * 8);
        buffer += "\033[?25l\033[?7l";

        std::string current_style = "";
        for (int y = 0; y < height; ++y) {
            buffer += "\033[" + std::to_string(y + 1) + ";1H";

            for (int x = 0; x < width; ++x) {
                int idx = y * width + x;
                if (styles[idx] != current_style) {
                    current_style = styles[idx];
                    buffer += current_style;
                }
                buffer += chars[idx];
            }
        }
        buffer += Style::RESET;
        safe_write_all(buffer);
    }
};

// ==========================================
// Sprites (Strictly 1-Cell Characters)
// ==========================================
namespace Sprites {
    const std::vector<std::string> DINO_STAND = {
        " ▄███",
        " ████",
        "█████",
        " █  █"
    };

    const std::vector<std::string> DINO_RUN_1 = {
        " ▄███",
        " ████",
        "█████",
        " █  █"
    };

    const std::vector<std::string> DINO_RUN_2 = {
        " ▄███",
        " ████",
        "█████",
        " ▀  █"
    };

    const std::vector<std::string> DINO_RUN_3 = {
        " ▄███",
        " ████",
        "█████",
        " █  ▀"
    };

    const std::vector<std::string> DINO_JUMP = {
        " ▄███",
        " ████",
        "█████",
        " ▀  ▀"
    };

    const std::vector<std::string> DINO_DEAD = {
        " ▄X██",
        " ████",
        "█████",
        " █  █"
    };

    const std::vector<std::string> DINO_DUCK_1 = {
        " ▄██████",
        "████████",
        " █    █ "
    };

    const std::vector<std::string> DINO_DUCK_2 = {
        " ▄██████",
        "████████",
        " ▀    █ "
    };

    const std::vector<std::string> CACTUS_SMALL = {
        "▄█▄",
        " █ "
    };

    const std::vector<std::string> CACTUS_TALL = {
        "▄█▄",
        " █ ",
        " █ "
    };

    const std::vector<std::string> CACTUS_DOUBLE = {
        "▄█▄ ▄█▄",
        " █   █ ",
        " █   █ "
    };

    const std::vector<std::string> CACTUS_TRIPLE = {
        "▄█▄▄█▄▄█▄",
        " █  █  █ ",
        " █  █  █ "
    };

    const std::vector<std::string> PTERO_WINGS_UP = {
        "  ▄▀  ",
        "▄████>"
    };

    const std::vector<std::string> PTERO_WINGS_DOWN = {
        "▄████>",
        "  ▀▄  "
    };

    const std::vector<std::string> GEO_CUBE_0 = {
        "┌──┐",
        "│●●│"
    };

    const std::vector<std::string> GEO_CUBE_45 = {
        " ╱╲ ",
        " ╲╱ "
    };

    const std::vector<std::string> GEO_CUBE_90 = {
        "┌●─┐",
        "│●─│"
    };

    const std::vector<std::string> GEO_CUBE_135 = {
        " ╳╳ ",
        " ╳╳ "
    };

    const std::vector<std::string> GEO_CUBE_180 = {
        "│●●│",
        "└──┘"
    };

    const std::vector<std::string> GEO_CUBE_225 = {
        " ╲╱ ",
        " ╱╲ "
    };

    const std::vector<std::string> GEO_CUBE_270 = {
        "│─●│",
        "└─●┘"
    };

    const std::vector<std::string> GEO_SPIKE_1 = {
        " ▲ ",
        "/█\\"
    };

    const std::vector<std::string> GEO_SPIKE_2 = {
        " ▲  ▲ ",
        "/█\\/█\\"
    };

    const std::vector<std::string> GEO_SPIKE_3 = {
        " ▲  ▲  ▲ ",
        "/█\\/█\\/█\\"
    };

    const std::vector<std::string> GEO_PAD = {
        "[▲▲]"
    };

    const std::vector<std::string> GEO_RING = {
        "(★)"
    };
}

// ==========================================
// Gameplay Hints Collection
// ==========================================
const std::vector<std::string> GAME_TIPS = {
    "Удерживайте [ПРОБЕЛ] или [W] для высокого прыжка, короткий тап — для низкого!",
    "Нажмите [S] или [↓] в воздухе для быстрого падения вниз (Fast-Fall)!",
    "Приседайте [S] или [↓], чтобы проскальзывать под летающими птеродактилями!",
    "В режиме Geometry Dash жмите [ПРОБЕЛ] возле сфер (★) для двойного прыжка!",
    "Розовые батуты [▲▲] катапультируют куб высоко над опасными шипами!",
    "Нажмите [M], чтобы переключить режим: Динозаврик / Geometry Dash!",
    "Каждые 500 очков день сменяется ночью, а скорость бега плавно возрастает!",
    "Нажмите [T], чтобы включить или отключить звуковые эффекты (SFX)!"
};

// ==========================================
// Game Modes & States
// ==========================================
enum GameMode {
    MODE_DINO = 0,
    MODE_GEODASH = 1
};

enum GameState {
    STATE_READY,     // Respawned / Waiting for player action to start!
    STATE_PLAYING,   // Active running
    STATE_PAUSED,
    STATE_GAMEOVER
};

enum ObstacleType {
    OBS_CACTUS_SMALL,
    OBS_CACTUS_TALL,
    OBS_CACTUS_DOUBLE,
    OBS_CACTUS_TRIPLE,
    OBS_PTERO_LOW,
    OBS_PTERO_MID,
    OBS_PTERO_HIGH,

    OBS_GEO_SPIKE_1,
    OBS_GEO_SPIKE_2,
    OBS_GEO_SPIKE_3,
    OBS_GEO_PAD,
    OBS_GEO_RING
};

struct Obstacle {
    ObstacleType type;
    float x;
    float y;
    int width;
    int height;
    bool passed = false;
    bool triggered = false;
};

struct Particle {
    float x, y;
    float vx, vy;
    float life;
    float max_life;
    std::string ch;
    std::string style;
};

struct Cloud {
    float x;
    int y;
    float speed_factor;
    int type;
};

struct Star {
    int x;
    int y;
    int phase;
};

// ==========================================
// High Score Storage
// ==========================================
int load_high_score(GameMode mode) {
    const char* home = getenv("HOME");
    if (!home) return 0;
    std::string fname = std::string(home) + (mode == MODE_DINO ? "/.dino_highscore.txt" : "/.geodash_highscore.txt");
    std::ifstream file(fname);
    int score = 0;
    if (file >> score) {
        return score;
    }
    return 0;
}

void save_high_score(GameMode mode, int score) {
    const char* home = getenv("HOME");
    if (!home) return;
    std::string fname = std::string(home) + (mode == MODE_DINO ? "/.dino_highscore.txt" : "/.geodash_highscore.txt");
    std::ofstream file(fname);
    file << score << std::endl;
}

// ==========================================
// Main Game Engine
// ==========================================
class GameEngine {
public:
    Screen screen;
    GameState state = STATE_READY;
    GameMode mode = MODE_DINO;

    // Layout
    int ground_y = 16;
    float player_x = 10.0f;
    float jump_height_cells = 8.0f;
    float gravity = 65.0f;
    float jump_velocity = -25.0f;

    // Player State
    float player_y = 0.0f;
    float player_vy = 0.0f;
    bool is_grounded = true;
    bool is_ducking = false;
    bool is_jumping = false;
    float duck_timer = 0.0f;
    float jump_buffer_timer = 0.0f; // Stores jump intent for 180ms
    float coyote_timer = 0.0f;      // 80ms grace window after leaving ground
    float jump_hold_timer = 0.0f;   // Hold duration for full height jump
    float rotation = 0.0f;
    int anim_frame = 0;
    float anim_timer = 0.0f;

    // Input state machine
    int esc_state = 0;
    float esc_timer = 0.0f;
    unsigned char utf8_lead = 0;

    // Movement & Parallax
    float speed = 34.0f;
    float base_speed = 34.0f;
    float ground_scroll = 0.0f;
    float mountain_scroll = 0.0f;

    // Obstacles & Spawning
    std::vector<Obstacle> obstacles;
    float spawn_timer = 0.0f;

    // State query helpers
    float get_rightmost_obstacle_x() const {
        if (obstacles.empty()) return -999.0f;
        float max_x = -999.0f;
        for (const auto& obs : obstacles) {
            float right_edge = obs.x + (float)obs.width;
            if (right_edge > max_x) {
                max_x = right_edge;
            }
        }
        return max_x;
    }

    bool is_jump_pressed_or_held() const {
#ifdef __APPLE__
        if (is_key_physically_down(MacKeys::SPACE) ||
            is_key_physically_down(MacKeys::W) ||
            is_key_physically_down(MacKeys::UP)) {
            return true;
        }
#endif
        return (jump_hold_timer > 0.0f);
    }

    bool is_duck_pressed_or_held() const {
#ifdef __APPLE__
        if (is_key_physically_down(MacKeys::S) ||
            is_key_physically_down(MacKeys::DOWN)) {
            return true;
        }
#endif
        return (duck_timer > 0.0f);
    }

    // Particles & Visuals
    std::vector<Particle> particles;
    std::vector<Cloud> clouds;
    std::vector<Star> stars;
    std::vector<float> eq_bars;
    float beat_timer = 0.0f;
    bool beat_flash = false;

    // Hints
    int current_tip_idx = 0;
    float ready_pulse = 0.0f;

    // Score & Day/Night
    float score_float = 0.0f;
    int score = 0;
    int high_score = 0;
    bool new_high_score = false;
    bool is_night = false;
    float day_night_timer = 0.0f;

    GameEngine() {
        update_screen_size(true);
        high_score = load_high_score(mode);
        init_decorations();
    }

    void update_screen_size(bool initial = false) {
        struct winsize ws;
        int cols = 80, rows = 24;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            cols = ws.ws_col;
            rows = ws.ws_row;
        }

        if (!initial && cols == screen.width && rows == screen.height) {
            return;
        }

        if (!initial) {
            std::cout << "\033[2J\033[3J\033[H" << std::flush;
        }

        screen.resize(cols, rows);

        // Ground positioning guarantees at least 3 rows of visible earth strata at bottom
        if (screen.height <= 22) {
            ground_y = screen.height - 6;
        } else if (screen.height <= 30) {
            ground_y = screen.height - 7;
        } else {
            ground_y = (int)(screen.height * 0.72f);
        }

        player_x = std::clamp((float)screen.width * 0.12f, 7.0f, 22.0f);

        float sky_height = (float)(ground_y - 3);
        jump_height_cells = std::clamp(sky_height * 0.44f, 6.0f, 16.0f);
        gravity = 18.0f + jump_height_cells * 8.0f;
        jump_velocity = -sqrtf(2.0f * gravity * jump_height_cells);

        float distance = (float)screen.width - player_x;
        base_speed = std::clamp(distance / 2.2f, 28.0f, 90.0f);
        speed = base_speed + std::min(45.0f, (float)score * 0.03f);

        init_decorations();
    }

    void init_decorations() {
        clouds.clear();
        int num_clouds = std::max(4, screen.width / 18);
        for (int i = 0; i < num_clouds; ++i) {
            clouds.push_back({
                (float)(rand() % (screen.width - 4) + 2),
                3 + (rand() % 2), // High clouds in rows 3-4
                0.10f + ((float)rand() / (float)RAND_MAX) * 0.15f,
                rand() % 2
            });
        }

        stars.clear();
        int num_stars = std::max(20, (screen.width * ground_y) / 50);
        for (int i = 0; i < num_stars; ++i) {
            stars.push_back({
                1 + (rand() % (screen.width - 2)),
                3 + (rand() % std::max(2, ground_y - 5)),
                rand() % 3
            });
        }

        int num_bars = std::max(16, (screen.width - 4) / 4);
        eq_bars.assign(num_bars, 2.0f);
    }

    void respawn_to_ready() {
        update_screen_size();
        player_y = 0.0f;
        player_vy = 0.0f;
        is_grounded = true;
        is_ducking = false;
        is_jumping = false;
        duck_timer = 0.0f;
        jump_buffer_timer = 0.0f;
        coyote_timer = 0.0f;
        jump_hold_timer = 0.0f;
        esc_state = 0;
        esc_timer = 0.0f;
        utf8_lead = 0;
        rotation = 0.0f;
        anim_frame = 0;
        anim_timer = 0.0f;

        obstacles.clear();
        particles.clear();
        spawn_timer = 1.0f;

        score_float = 0.0f;
        score = 0;
        high_score = load_high_score(mode);
        new_high_score = false;
        is_night = false;
        day_night_timer = 0.0f;

        current_tip_idx = (current_tip_idx + 1) % GAME_TIPS.size();

        // Game waits for user action to start running!
        state = STATE_READY;
    }

    void start_gameplay() {
        state = STATE_PLAYING;
    }

    void switch_mode() {
        mode = (mode == MODE_DINO) ? MODE_GEODASH : MODE_DINO;
        high_score = load_high_score(mode);
        play_sound("Pop");
    }

    void spawn_particles(float x, float y, int count, const std::string& style, const std::string& ch = "*") {
        for (int i = 0; i < count; ++i) {
            float angle = ((float)rand() / (float)RAND_MAX) * 2.0f * 3.14159f;
            float spd = 5.0f + ((float)rand() / (float)RAND_MAX) * 18.0f;
            particles.push_back({
                x, y,
                cosf(angle) * spd,
                sinf(angle) * spd - 3.0f,
                0.0f,
                0.3f + ((float)rand() / (float)RAND_MAX) * 0.45f,
                ch,
                style
            });
        }
    }

    void spawn_obstacle() {
        if (mode == MODE_DINO) {
            Obstacle obs;
            obs.x = (float)(screen.width - 2);
            obs.passed = false;
            obs.triggered = false;

            int r = rand() % 100;

            // Pterodactyls begin appearing at score > 150
            if (score > 150 && r < 35) {
                int alt = rand() % 3;
                if (alt == 0) {
                    obs.type = OBS_PTERO_LOW;
                    obs.y = (float)(ground_y - 3); // Low: must jump over!
                } else if (alt == 1) {
                    obs.type = OBS_PTERO_MID;
                    obs.y = (float)(ground_y - 5); // Mid: MUST duck under!
                } else {
                    obs.type = OBS_PTERO_HIGH;
                    obs.y = (float)(ground_y - 7); // High: safe to run under!
                }
                obs.width = 6;
                obs.height = 2;
            } else {
                int cr = rand() % 100;
                if (score < 120) {
                    if (cr < 65) {
                        obs.type = OBS_CACTUS_SMALL;
                        obs.y = (float)(ground_y - 2);
                        obs.width = 3;
                        obs.height = 2;
                    } else {
                        obs.type = OBS_CACTUS_TALL;
                        obs.y = (float)(ground_y - 3);
                        obs.width = 3;
                        obs.height = 3;
                    }
                } else if (score < 300) {
                    if (cr < 35) {
                        obs.type = OBS_CACTUS_SMALL;
                        obs.y = (float)(ground_y - 2);
                        obs.width = 3;
                        obs.height = 2;
                    } else if (cr < 70) {
                        obs.type = OBS_CACTUS_TALL;
                        obs.y = (float)(ground_y - 3);
                        obs.width = 3;
                        obs.height = 3;
                    } else {
                        obs.type = OBS_CACTUS_DOUBLE;
                        obs.y = (float)(ground_y - 3);
                        obs.width = 7;
                        obs.height = 3;
                    }
                } else {
                    if (cr < 25) {
                        obs.type = OBS_CACTUS_SMALL;
                        obs.y = (float)(ground_y - 2);
                        obs.width = 3;
                        obs.height = 2;
                    } else if (cr < 55) {
                        obs.type = OBS_CACTUS_TALL;
                        obs.y = (float)(ground_y - 3);
                        obs.width = 3;
                        obs.height = 3;
                    } else if (cr < 80) {
                        obs.type = OBS_CACTUS_DOUBLE;
                        obs.y = (float)(ground_y - 3);
                        obs.width = 7;
                        obs.height = 3;
                    } else {
                        obs.type = OBS_CACTUS_TRIPLE;
                        obs.y = (float)(ground_y - 3);
                        obs.width = 9;
                        obs.height = 3;
                    }
                }
            }
            obstacles.push_back(obs);
        } else {
            int r = rand() % 100;
            Obstacle obs;
            obs.x = (float)(screen.width - 2);
            obs.passed = false;
            obs.triggered = false;

            if (r < 35) {
                obs.type = OBS_GEO_SPIKE_1;
                obs.y = (float)(ground_y - 2);
                obs.width = 3;
                obs.height = 2;
                obstacles.push_back(obs);
            } else if (r < 60) {
                obs.type = OBS_GEO_SPIKE_2;
                obs.y = (float)(ground_y - 2);
                obs.width = 6;
                obs.height = 2;
                obstacles.push_back(obs);
            } else if (r < 75) {
                obs.type = OBS_GEO_SPIKE_3;
                obs.y = (float)(ground_y - 2);
                obs.width = 9;
                obs.height = 2;
                obstacles.push_back(obs);
            } else if (r < 88) {
                obs.type = OBS_GEO_PAD;
                obs.y = (float)(ground_y - 1);
                obs.width = 4;
                obs.height = 1;
                obstacles.push_back(obs);

                Obstacle spike_after;
                spike_after.type = OBS_GEO_SPIKE_2;
                spike_after.x = obs.x + 10.0f;
                spike_after.y = (float)(ground_y - 2);
                spike_after.width = 6;
                spike_after.height = 2;
                spike_after.passed = false;
                spike_after.triggered = false;
                obstacles.push_back(spike_after);
            } else {
                Obstacle ring;
                ring.type = OBS_GEO_RING;
                ring.x = (float)(screen.width + 6);
                ring.y = (float)(ground_y - (int)(jump_height_cells * 0.65f));
                ring.width = 3;
                ring.height = 1;
                ring.passed = false;
                ring.triggered = false;
                obstacles.push_back(ring);

                Obstacle pit;
                pit.type = OBS_GEO_SPIKE_3;
                pit.x = ring.x - 3.0f;
                pit.y = (float)(ground_y - 2);
                pit.width = 9;
                pit.height = 2;
                pit.passed = false;
                pit.triggered = false;
                obstacles.push_back(pit);
            }
        }
    }

    void handle_input() {
        char buf[128];
        int n;
        while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
            for (int i = 0; i < n; ++i) {
                unsigned char c = (unsigned char)buf[i];

                // 1. Escape Sequence State Machine (Arrow Keys)
                if (esc_state == 1) {
                    if (c == '[') {
                        esc_state = 2;
                    } else if (c == 'O') {
                        esc_state = 3;
                    } else {
                        esc_state = 0;
                    }
                    continue;
                } else if (esc_state == 2 || esc_state == 3) {
                    esc_state = 0;
                    if (c == 'A') action_jump();      // Up arrow
                    else if (c == 'B') action_duck(); // Down arrow
                    continue;
                }

                if (c == '\033') {
                    esc_state = 1;
                    esc_timer = 0.08f;
                    continue;
                }

                // 2. Multi-byte UTF-8 Cyrillic Decoding (RU keyboard layout support)
                if (c >= 0xC0) {
                    utf8_lead = c;
                    continue;
                }
                if (utf8_lead != 0) {
                    uint16_t unicode = ((uint16_t)(utf8_lead & 0x1F) << 6) | (c & 0x3F);
                    utf8_lead = 0;

                    if (unicode == 0x0446 || unicode == 0x0426) {        // ц / Ц (Key W)
                        action_jump();
                    } else if (unicode == 0x044B || unicode == 0x042B) { // ы / Ы (Key S)
                        action_duck();
                    } else if (unicode == 0x044C || unicode == 0x042C) { // ь / Ь (Key M)
                        if (state != STATE_PLAYING) switch_mode();
                    } else if (unicode == 0x0435 || unicode == 0x0415) { // е / Е (Key T)
                        g_sound_enabled = !g_sound_enabled;
                        if (g_sound_enabled) play_sound("Tink");
                    } else if (unicode == 0x0437 || unicode == 0x0417) { // з / З (Key P)
                        if (state == STATE_PLAYING) state = STATE_PAUSED;
                        else if (state == STATE_PAUSED) state = STATE_PLAYING;
                    } else if (unicode == 0x043A || unicode == 0x041A) { // к / К (Key R)
                        if (state == STATE_GAMEOVER || state == STATE_PAUSED) respawn_to_ready();
                    } else if (unicode == 0x0439 || unicode == 0x0419) { // й / Й (Key Q)
                        g_running = 0;
                    }
                    continue;
                }

                // 3. Standard ASCII Keys
                if (c == ' ' || c == 'w' || c == 'W') {
                    action_jump();
                } else if (c == 's' || c == 'S') {
                    action_duck();
                } else if (c == 'm' || c == 'M') {
                    if (state != STATE_PLAYING) switch_mode();
                } else if (c == 't' || c == 'T') {
                    g_sound_enabled = !g_sound_enabled;
                    if (g_sound_enabled) play_sound("Tink");
                } else if (c == 'p' || c == 'P') {
                    if (state == STATE_PLAYING) state = STATE_PAUSED;
                    else if (state == STATE_PAUSED) state = STATE_PLAYING;
                } else if (c == 'r' || c == 'R') {
                    if (state == STATE_GAMEOVER || state == STATE_PAUSED) respawn_to_ready();
                } else if (c == 'q' || c == 'Q') {
                    g_running = 0;
                }
            }
        }
    }

    void action_jump() {
        jump_buffer_timer = 0.18f; // Buffer jump intent for 180ms
        jump_hold_timer = 0.28f;   // Hold window for full height jump
        try_execute_jump();
    }

    void try_execute_jump() {
        if (state == STATE_GAMEOVER) {
            respawn_to_ready();
            jump_buffer_timer = 0.0f;
            return;
        }

        if (state == STATE_READY) {
            start_gameplay();
            is_grounded = false;
            is_jumping = true;
            player_vy = jump_velocity;
            jump_buffer_timer = 0.0f;
            duck_timer = 0.0f;
            is_ducking = false;
            play_sound("Tink");
            spawn_particles(player_x + 2.0f, (float)(ground_y - 1), 6, Style::GRAY, ".");
            return;
        }

        if (state == STATE_PAUSED) {
            state = STATE_PLAYING;
            jump_buffer_timer = 0.0f;
            return;
        }

        // Geometry Dash: Check Jump Ring activation in air
        if (mode == MODE_GEODASH && !is_grounded) {
            for (auto& obs : obstacles) {
                if (obs.type == OBS_GEO_RING && !obs.triggered) {
                    float dist = fabsf((player_x + 2.0f) - (obs.x + 1.5f));
                    float dist_y = fabsf(player_y - (ground_y - obs.y));
                    if (dist < 4.5f && dist_y < 4.5f) {
                        obs.triggered = true;
                        player_vy = jump_velocity * 0.95f;
                        jump_buffer_timer = 0.0f;
                        play_sound("Ping");
                        spawn_particles(obs.x + 1.5f, obs.y, 18, Style::NEON_YELLOW, "★");
                        return;
                    }
                }
            }
        }

        if (is_grounded || coyote_timer > 0.0f) {
            is_grounded = false;
            is_jumping = true;
            coyote_timer = 0.0f;
            jump_buffer_timer = 0.0f;
            duck_timer = 0.0f;
            is_ducking = false;
            player_vy = jump_velocity;
            play_sound("Tink");
            spawn_particles(player_x + 2.0f, (float)(ground_y - 1), 6, Style::GRAY, ".");
        }
    }

    void action_duck() {
        if (state == STATE_READY) {
            start_gameplay();
            is_ducking = true;
            duck_timer = 0.65f;
            return;
        }
        if (state != STATE_PLAYING) return;

        if (!is_grounded) {
            // Fast fall downwards!
            if (player_vy < 0.0f) player_vy = 0.0f;
            player_vy += gravity * 0.85f;
            duck_timer = 0.45f;
        } else {
            is_ducking = true;
            duck_timer = 0.65f;
        }
    }

    void update(float dt) {
        update_screen_size();

        ready_pulse += dt * 3.5f;

        // Escape sequence timeout
        if (esc_state != 0) {
            esc_timer -= dt;
            if (esc_timer <= 0.0f) {
                esc_state = 0;
            }
        }

        beat_timer += dt;
        if (beat_timer > 0.42f) {
            beat_timer = 0.0f;
            beat_flash = true;
            for (auto& bar : eq_bars) {
                bar = 2.0f + (rand() % std::max(4, (ground_y / 2)));
            }
        } else {
            beat_flash = false;
            for (auto& bar : eq_bars) {
                bar = std::max(1.0f, bar - dt * 8.0f);
            }
        }

        if (state == STATE_PLAYING) {
            // 1. Decay jump buffer & coyote timers
            if (jump_buffer_timer > 0.0f) {
                jump_buffer_timer -= dt;
                if (is_grounded || coyote_timer > 0.0f) {
                    try_execute_jump();
                }
            }

            if (is_grounded) {
                coyote_timer = 0.08f;
            } else {
                if (coyote_timer > 0.0f) coyote_timer -= dt;
            }

            if (jump_hold_timer > 0.0f) {
                jump_hold_timer -= dt;
            }

            // 2. Ducking state handling
            if (mode == MODE_DINO) {
#ifdef __APPLE__
                if (is_key_physically_down(MacKeys::S) || is_key_physically_down(MacKeys::DOWN)) {
                    duck_timer = 0.25f; // Keep topped up while key is held down
                }
#endif
                if (duck_timer > 0.0f) {
                    duck_timer -= dt;
                    is_ducking = true;
                } else {
                    is_ducking = false;
                }
            } else {
                is_ducking = false;
            }

            // Geometry Dash auto-jump when holding jump on ground
            if (mode == MODE_GEODASH && is_grounded && is_jump_pressed_or_held()) {
                action_jump();
            }

            speed = base_speed + std::min(45.0f, (float)score * 0.03f);

            ground_scroll += speed * dt;
            mountain_scroll += (speed * 0.08f) * dt;

            day_night_timer += dt;
            if (day_night_timer > 20.0f) {
                day_night_timer = 0.0f;
                is_night = !is_night;
                if (is_night) play_sound("Purr");
            }

            score_float += (speed * 0.35f) * dt;
            int prev_score = score;
            score = (int)score_float;

            if (score / 100 > prev_score / 100) {
                play_sound("Glass");
            }

            if (score > high_score) {
                high_score = score;
                new_high_score = true;
            }

            // Player Physics
            if (!is_grounded) {
                float effective_gravity = gravity;

                // Variable jump height:
                // While moving upwards, if jump is held, normal gravity allows full arc.
                // If not held and hold window expired, increased gravity cuts jump short!
                if (player_vy < 0.0f) {
                    if (!is_jump_pressed_or_held()) {
                        effective_gravity = gravity * 2.2f;
                    }
                } else {
                    // Falling downward:
                    // If duck is active, apply fast-fall
                    if (is_ducking) {
                        effective_gravity = gravity * 2.0f;
                    }
                }

                player_vy += effective_gravity * dt;
                player_y -= player_vy * dt;

                if (mode == MODE_GEODASH) {
                    rotation += dt * 680.0f;
                    if (rotation >= 360.0f) rotation -= 360.0f;
                }

                if (player_y <= 0.0f) {
                    player_y = 0.0f;
                    player_vy = 0.0f;
                    is_grounded = true;
                    is_jumping = false;
                    rotation = 0.0f;
                    spawn_particles(player_x + 2.0f, (float)(ground_y - 1), 5, Style::GRAY, ".");

                    // If a jump was buffered during the fall, execute it immediately on touchdown!
                    if (jump_buffer_timer > 0.0f) {
                        try_execute_jump();
                    } else if (mode == MODE_DINO && duck_timer > 0.0f) {
                        is_ducking = true;
                    }
                }
            } else {
                player_y = 0.0f;
                rotation = 0.0f;
            }

            anim_timer += dt;
            if (anim_timer > 0.085f) {
                anim_timer = 0.0f;
                anim_frame = (anim_frame + 1) % 4;
            }

            // Geometry Dash Neon Trail
            if (mode == MODE_GEODASH) {
                particles.push_back({
                    player_x,
                    (float)(ground_y - 2 - (int)player_y),
                    -speed * 0.25f,
                    0.0f,
                    0.0f,
                    0.28f,
                    "•",
                    is_grounded ? Style::NEON_CYAN : Style::NEON_PINK
                });
            }

            // Obstacle Spawning (CONTINUOUS!)
            spawn_timer -= dt;
            float rightmost_x = get_rightmost_obstacle_x();
            float dist_from_right = (float)screen.width - rightmost_x;
            float min_dist = speed * 0.75f + (float)(rand() % 12);

            if (spawn_timer <= 0.0f && (obstacles.empty() || dist_from_right >= min_dist)) {
                spawn_obstacle();
                spawn_timer = 0.25f + ((float)rand() / (float)RAND_MAX) * 0.5f;
            }

            // Obstacle Movement & Collision
            for (auto it = obstacles.begin(); it != obstacles.end(); ) {
                it->x -= speed * dt;

                if (mode == MODE_GEODASH && it->type == OBS_GEO_PAD && !it->triggered) {
                    if (fabsf((player_x + 2.0f) - (it->x + 2.0f)) < 3.2f && player_y <= 2.5f) {
                        it->triggered = true;
                        player_vy = jump_velocity * 1.35f;
                        is_grounded = false;
                        play_sound("Hero");
                        spawn_particles(it->x + 2.0f, it->y, 16, Style::NEON_PINK, "▲");
                    }
                }

                if (check_collision(*it)) {
                    trigger_game_over();
                    return;
                }

                if (it->x + (float)it->width < 0.0f) {
                    it = obstacles.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Clouds scroll in background
        for (auto& cloud : clouds) {
            cloud.x -= (speed * cloud.speed_factor) * dt;
            if (cloud.x < 1.0f) {
                cloud.x = (float)(screen.width - 10 + (rand() % 8));
                cloud.y = 3 + (rand() % 2);
            }
        }

        // Update Particles
        for (auto it = particles.begin(); it != particles.end(); ) {
            it->life += dt;
            it->x += it->vx * dt;
            it->y += it->vy * dt;
            it->vy += 22.0f * dt;
            if (it->life >= it->max_life) {
                it = particles.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool check_collision(const Obstacle& obs) {
        if (obs.type == OBS_GEO_PAD || obs.type == OBS_GEO_RING) {
            return false;
        }

        float px = player_x;
        float py = (float)(ground_y - (int)player_y - (is_ducking ? 3 : 4));
        float pw = (mode == MODE_DINO && is_ducking) ? 8.0f : 4.0f;
        float ph = (mode == MODE_DINO && is_ducking) ? 2.8f : 3.8f;

        float ox = obs.x;
        float oy = obs.y;
        float ow = (float)obs.width;
        float oh = (float)obs.height;

        float inset_x = 0.8f;
        float inset_y = 0.25f;
        if (px + inset_x < ox + ow && px + pw - inset_x > ox &&
            py + inset_y < oy + oh && py + ph - inset_y > oy) {
            return true;
        }
        return false;
    }

    void trigger_game_over() {
        state = STATE_GAMEOVER;
        play_sound("Basso");
        save_high_score(mode, high_score);

        float px = player_x + 2.0f;
        float py = (float)(ground_y - (int)player_y - 2);
        std::string explode_style = (mode == MODE_DINO) ? Style::DINO_EYE : Style::NEON_PINK;
        spawn_particles(px, py, 36, explode_style, "■");
        spawn_particles(px, py, 20, Style::NEON_YELLOW, "•");
    }

    void render() {
        screen.clear();

        // 1. Background Sky & Horizon
        render_background();

        // 2. High-contrast visible textured ground strata
        render_ground();

        // 3. Obstacles
        render_obstacles();

        // 4. Particles
        for (const auto& p : particles) {
            int px = (int)p.x;
            int py = (int)p.y;
            if (px > 0 && px < screen.width - 1 && py > 2 && py < ground_y) {
                screen.set_cell(px, py, p.ch, p.style);
            }
        }

        // 5. Player
        render_player();

        // 6. Header HUD inside Frame
        render_hud();

        // 7. Hint Bar at the bottom
        render_hint_bar();

        // 8. Box Frame Borders (Top, Sides, Dividers, Bottom)
        render_frame_borders();

        // 9. Centered Opaque Overlay Dialogs
        if (state == STATE_READY) {
            render_ready_dialog();
        } else if (state == STATE_PAUSED) {
            render_pause_dialog();
        } else if (state == STATE_GAMEOVER) {
            render_gameover_dialog();
        }

        screen.flush_to_terminal();
    }

    void render_frame_borders() {
        std::string frame_col = (mode == MODE_DINO) ? Style::FRAME_DINO : Style::FRAME_GEO;
        std::string div_col   = (mode == MODE_DINO) ? Style::DARK_GRAY  : Style::CYBER_GRID;

        // Row 0: Top Frame Border ╔═════════╗
        std::string top_border = "╔";
        for (int x = 1; x < screen.width - 1; ++x) top_border += "═";
        top_border += "╗";
        screen.print_str(0, 0, top_border, frame_col);

        // Row 2: Header Divider ╟─────────╢
        std::string head_div = "╟";
        for (int x = 1; x < screen.width - 1; ++x) head_div += "─";
        head_div += "╢";
        screen.print_str(0, 2, head_div, div_col);

        // Row ground_y: Ground Line Divider ╠═════════╣
        std::string ground_div = "╠";
        for (int x = 1; x < screen.width - 1; ++x) {
            if (mode == MODE_DINO) {
                int pattern = ((int)ground_scroll + x) % 15;
                if (pattern == 0) ground_div += "▀";
                else if (pattern == 6) ground_div += "▄";
                else if (pattern == 11) ground_div += ".";
                else ground_div += "═";
            } else {
                int pattern = ((int)ground_scroll + x) % 8;
                ground_div += (pattern == 0) ? "╩" : "═";
            }
        }
        ground_div += "╣";
        screen.print_str(0, ground_y, ground_div, frame_col);

        // Row screen.height - 3: Bottom Divider ╟─────────╢
        std::string bottom_div = "╟";
        for (int x = 1; x < screen.width - 1; ++x) bottom_div += "─";
        bottom_div += "╢";
        screen.print_str(0, screen.height - 3, bottom_div, div_col);

        // Row screen.height - 1: Bottom Frame Border ╚═════════╝
        std::string bottom_border = "╚";
        for (int x = 1; x < screen.width - 1; ++x) bottom_border += "═";
        bottom_border += "╝";
        screen.print_str(0, screen.height - 1, bottom_border, frame_col);

        // Left & Right Vertical Frame Borders: ║
        for (int y = 1; y < screen.height - 1; ++y) {
            if (y == 2 || y == ground_y || y == screen.height - 3) continue;
            screen.set_cell(0, y, "║", frame_col);
            screen.set_cell(screen.width - 1, y, "║", frame_col);
        }
    }

    void render_background() {
        if (mode == MODE_DINO) {
            if (is_night) {
                screen.print_str(screen.width - 12, 3, "( ) MOON", Style::MOON);
                for (const auto& s : stars) {
                    if (s.y > 2 && s.y < ground_y) {
                        std::string star_sym = (s.phase == 0) ? "★" : (s.phase == 1 ? "·" : "+");
                        screen.set_cell(s.x, s.y, star_sym, Style::STAR);
                    }
                }
            } else {
                screen.print_str(screen.width - 13, 3, " \\ | / ", Style::SUN);
                screen.print_str(screen.width - 14, 4, "--( O )--", Style::SUN);
                screen.print_str(screen.width - 13, 5, " / | \\ ", Style::SUN);

                // Only draw moving clouds when actively playing or pause, not behind dialogs
                if (state == STATE_PLAYING) {
                    for (const auto& c : clouds) {
                        if (c.y > 2 && c.y < ground_y - 2) {
                            if (c.type == 0) {
                                screen.print_str((int)c.x, c.y,     "  ▄██▄  ", Style::CLOUD);
                                screen.print_str((int)c.x, c.y + 1, "▄██████▄", Style::CLOUD);
                            } else {
                                screen.print_str((int)c.x, c.y,     " ▄▄███▄ ", Style::CLOUD);
                                screen.print_str((int)c.x, c.y + 1, "████████", Style::CLOUD);
                            }
                        }
                    }
                }
            }

            // Horizon stars / dust in night mode
            if (is_night) {
                // Subtle horizon stars
                for (int x = 4; x < screen.width - 4; x += 12) {
                    screen.set_cell(x, ground_y - 2, "·", Style::STAR);
                }
            }
        } else {
            // Geometry Dash Cyber Equalizer Visualizer
            for (size_t i = 0; i < eq_bars.size(); ++i) {
                int bx = 2 + (int)i * 4;
                int bh = (int)eq_bars[i];
                for (int h = 0; h < bh; ++h) {
                    int by = ground_y - 1 - h;
                    if (by > 2 && bx < screen.width - 2) {
                        screen.set_cell(bx, by, "█", (beat_flash ? Style::NEON_PINK : Style::DARK_GRAY));
                    }
                }
            }

            for (int x = 3; x < screen.width - 3; x += 6) {
                screen.set_cell(x, ground_y - 3, "·", Style::CYBER_GRID);
            }
        }
    }

    void render_ground() {
        int bottom_limit = screen.height - 3;

        if (mode == MODE_DINO) {
            for (int y = ground_y + 1; y < bottom_limit; ++y) {
                int depth = y - ground_y;
                for (int x = 1; x < screen.width - 1; ++x) {
                    int p = ((int)ground_scroll + x + y * 7) % 32;
                    if (depth == 1) {
                        // High-contrast warm soil layer with stones
                        if (p % 4 == 0) screen.set_cell(x, y, "/", Style::SOIL_TEXT);
                        else if (p % 4 == 1) screen.set_cell(x, y, "·", Style::ROCK_TEXT);
                        else if (p % 4 == 2) screen.set_cell(x, y, "▄", Style::SOIL_TEXT);
                        else screen.set_cell(x, y, "·", Style::ROCK_TEXT);
                    } else if (depth == 2) {
                        // Packed gravel & geological strata layer
                        if (p % 5 == 0) screen.set_cell(x, y, "▒", Style::ROCK_TEXT);
                        else if (p % 5 == 1) screen.set_cell(x, y, "=", Style::SOIL_TEXT);
                        else if (p % 5 == 2) screen.set_cell(x, y, "·", Style::SOIL_TEXT);
                        else if (p % 5 == 3) screen.set_cell(x, y, "░", Style::ROCK_TEXT);
                        else screen.set_cell(x, y, "-", Style::SOIL_TEXT);
                    } else {
                        // Deep bedrock fill
                        if (p % 4 == 0) screen.set_cell(x, y, "▓", Style::DARK_GRAY);
                        else if (p % 4 == 1) screen.set_cell(x, y, "▒", Style::DARK_GRAY);
                        else if (p % 4 == 2) screen.set_cell(x, y, "░", Style::DARK_GRAY);
                        else screen.set_cell(x, y, "▓", Style::DARK_GRAY);
                    }
                }
            }
        } else {
            // Geometry Dash Cyber Matrix Ground
            for (int y = ground_y + 1; y < bottom_limit; ++y) {
                int depth = y - ground_y;
                for (int x = 1; x < screen.width - 1; ++x) {
                    if (depth % 2 == 1) {
                        screen.set_cell(x, y, (x % 4 == 0) ? "║" : " ", Style::CYBER_GRID);
                    } else {
                        screen.set_cell(x, y, (x % 4 == 0) ? "╬" : "═", Style::CYBER_DEEP);
                    }
                }
            }
        }
    }

    void render_obstacles() {
        for (const auto& obs : obstacles) {
            int ox = (int)obs.x;
            int oy = (int)obs.y;

            if (ox <= 0 || ox >= screen.width - 2) continue;

            switch (obs.type) {
                case OBS_CACTUS_SMALL:
                    screen.draw_sprite(ox, oy, Sprites::CACTUS_SMALL, Style::CACTUS);
                    break;
                case OBS_CACTUS_TALL:
                    screen.draw_sprite(ox, oy, Sprites::CACTUS_TALL, Style::CACTUS);
                    break;
                case OBS_CACTUS_DOUBLE:
                    screen.draw_sprite(ox, oy, Sprites::CACTUS_DOUBLE, Style::CACTUS);
                    break;
                case OBS_CACTUS_TRIPLE:
                    screen.draw_sprite(ox, oy, Sprites::CACTUS_TRIPLE, Style::CACTUS);
                    break;
                case OBS_PTERO_LOW:
                case OBS_PTERO_MID:
                case OBS_PTERO_HIGH:
                    if (anim_frame % 2 == 0) {
                        screen.draw_sprite(ox, oy, Sprites::PTERO_WINGS_UP, Style::PTERO);
                    } else {
                        screen.draw_sprite(ox, oy, Sprites::PTERO_WINGS_DOWN, Style::PTERO);
                    }
                    break;
                case OBS_GEO_SPIKE_1:
                    screen.draw_sprite(ox, oy, Sprites::GEO_SPIKE_1, Style::NEON_RED);
                    break;
                case OBS_GEO_SPIKE_2:
                    screen.draw_sprite(ox, oy, Sprites::GEO_SPIKE_2, Style::NEON_RED);
                    break;
                case OBS_GEO_SPIKE_3:
                    screen.draw_sprite(ox, oy, Sprites::GEO_SPIKE_3, Style::NEON_RED);
                    break;
                case OBS_GEO_PAD:
                    screen.draw_sprite(ox, oy, Sprites::GEO_PAD, Style::NEON_PINK);
                    break;
                case OBS_GEO_RING:
                    screen.draw_sprite(ox, oy, Sprites::GEO_RING, obs.triggered ? Style::DARK_GRAY : Style::NEON_YELLOW);
                    break;
            }
        }
    }

    void render_player() {
        if (state == STATE_GAMEOVER) {
            if (mode == MODE_DINO) {
                int py = ground_y - 4;
                screen.draw_sprite((int)player_x, py, Sprites::DINO_DEAD, Style::DINO_EYE);
            }
            return;
        }

        int px = (int)player_x;
        int py = ground_y - (int)player_y - 4;

        if (mode == MODE_DINO) {
            std::string dino_col = is_night ? Style::WHITE : Style::DINO_GREEN;
            if (state == STATE_READY) {
                screen.draw_sprite(px, py, Sprites::DINO_STAND, dino_col);
            } else if (!is_grounded) {
                screen.draw_sprite(px, py, Sprites::DINO_JUMP, dino_col);
            } else if (is_ducking) {
                py = ground_y - 3;
                if (anim_frame % 2 == 0) screen.draw_sprite(px, py, Sprites::DINO_DUCK_1, dino_col);
                else screen.draw_sprite(px, py, Sprites::DINO_DUCK_2, dino_col);
            } else {
                if (anim_frame % 3 == 0) screen.draw_sprite(px, py, Sprites::DINO_RUN_1, dino_col);
                else if (anim_frame % 3 == 1) screen.draw_sprite(px, py, Sprites::DINO_RUN_2, dino_col);
                else screen.draw_sprite(px, py, Sprites::DINO_RUN_3, dino_col);
            }
        } else {
            // Mode Geometry Dash Cube
            py = ground_y - (int)player_y - 2;
            std::string cube_col = Style::NEON_CYAN;

            if (is_grounded) {
                screen.draw_sprite(px, py, Sprites::GEO_CUBE_0, cube_col);
            } else {
                int rot_deg = (int)rotation % 360;
                if (rot_deg < 45 || rot_deg >= 315) {
                    screen.draw_sprite(px, py, Sprites::GEO_CUBE_0, cube_col);
                } else if (rot_deg < 90) {
                    screen.draw_sprite(px, py, Sprites::GEO_CUBE_45, Style::NEON_YELLOW);
                } else if (rot_deg < 135) {
                    screen.draw_sprite(px, py, Sprites::GEO_CUBE_90, cube_col);
                } else if (rot_deg < 180) {
                    screen.draw_sprite(px, py, Sprites::GEO_CUBE_135, Style::NEON_PINK);
                } else if (rot_deg < 225) {
                    screen.draw_sprite(px, py, Sprites::GEO_CUBE_180, cube_col);
                } else if (rot_deg < 270) {
                    screen.draw_sprite(px, py, Sprites::GEO_CUBE_225, Style::NEON_YELLOW);
                } else {
                    screen.draw_sprite(px, py, Sprites::GEO_CUBE_270, cube_col);
                }
            }
        }
    }

    void render_hud() {
        std::string mode_str = (mode == MODE_DINO) ? "[DINO RUNNER]" : "[GEOMETRY DASH]";
        std::string mode_col = (mode == MODE_DINO) ? Style::DINO_GREEN : Style::NEON_CYAN;
        screen.print_str(3, 1, mode_str, mode_col);

        std::string sound_str = g_sound_enabled ? "SFX:ON" : "SFX:OFF";
        screen.print_str(20, 1, sound_str, Style::GRAY);

        char score_buf[64];
        snprintf(score_buf, sizeof(score_buf), "HI:%05d  SCORE:%05d  SPD:%02d", high_score, score, (int)speed);
        std::string score_col = new_high_score ? Style::NEON_YELLOW : Style::WHITE;
        screen.print_str(screen.width - 33, 1, score_buf, score_col);
    }

    void render_hint_bar() {
        int hint_y = screen.height - 2;
        std::string tip = GAME_TIPS[current_tip_idx];
        std::string hint_text = "[*] СОВЕТ: " + tip;

        auto glyphs = split_utf8(hint_text);
        if ((int)glyphs.size() > screen.width - 6) {
            hint_text = "";
            for (int i = 0; i < screen.width - 9; ++i) hint_text += glyphs[i];
            hint_text += "...";
        }

        screen.print_centered(hint_y, hint_text, Style::HINT_TEXT);
    }

    // Helper: clears the rectangular area of the dialog box so background elements don't bleed through
    void clear_dialog_rect(int x, int y, int w, int h) {
        for (int r = 0; r < h; ++r) {
            int ry = y + r;
            if (ry < 0 || ry >= screen.height) continue;
            for (int c = x; c < x + w; ++c) {
                if (c > 0 && c < screen.width - 1) {
                    screen.set_cell(c, ry, " ", Style::RESET);
                }
            }
        }
    }

    void render_ready_dialog() {
        int w = std::min(58, screen.width - 4);
        int h = 7;
        int x = (screen.width - w) / 2;
        int cy = std::max(3, (ground_y - h) / 2);

        clear_dialog_rect(x, cy - 1, w, h);

        bool pulse = ((int)(ready_pulse) % 2 == 0);
        std::string pulse_style = pulse ? (Style::BOLD + Style::NEON_YELLOW) : (Style::BOLD + Style::WHITE);

        screen.print_centered(cy - 1, "┌────────────────────────────────────────────────────────┐", Style::FRAME_GEO);
        screen.print_centered(cy,     "│                 ГОТОВЫ К СТАРТУ!                       │", Style::WHITE);
        screen.print_centered(cy + 1, "│                                                        │", Style::FRAME_GEO);
        screen.print_centered(cy + 2, "│   ▶ НАЖМИТЕ [ПРОБЕЛ] ИЛИ [W], ЧТОБЫ НАЧАТЬ БЕГАТЬ! ◀   │", pulse_style);
        screen.print_centered(cy + 3, "│           Игра начнется по вашему первому действию     │", Style::GRAY);
        screen.print_centered(cy + 4, "│   [M] Сменить режим  •  [T] Звук SFX  •  [Q] Выход     │", Style::DARK_GRAY);
        screen.print_centered(cy + 5, "└────────────────────────────────────────────────────────┘", Style::FRAME_GEO);
    }

    void render_pause_dialog() {
        int w = 29;
        int h = 6;
        int x = (screen.width - w) / 2;
        int cy = std::max(3, (ground_y - h) / 2);

        clear_dialog_rect(x, cy, w, h);

        screen.print_centered(cy,     "╔═══════════════════════════╗", Style::NEON_YELLOW);
        screen.print_centered(cy + 1, "║        ИГРА НА ПАУЗЕ      ║", Style::NEON_YELLOW);
        screen.print_centered(cy + 2, "║   Нажмите [P] или [SPACE] ║", Style::WHITE);
        screen.print_centered(cy + 3, "║      [R] Начать заново    ║", Style::GRAY);
        screen.print_centered(cy + 4, "║      [Q] Выход            ║", Style::GRAY);
        screen.print_centered(cy + 5, "╚═══════════════════════════╝", Style::NEON_YELLOW);
    }

    void render_gameover_dialog() {
        int w = std::min(58, screen.width - 4);
        int h = 9;
        int x = (screen.width - w) / 2;
        int cy = std::max(3, (ground_y - h) / 2);

        clear_dialog_rect(x, cy - 1, w, h);

        screen.print_centered(cy - 1, "┌────────────────────────────────────────────────────────┐", Style::NEON_RED);
        screen.print_centered(cy,     "│                     ИГРА ОКОНЧЕНА                      │", Style::BOLD + Style::NEON_RED);
        screen.print_centered(cy + 1, "│                                                        │", Style::NEON_RED);

        char score_line[128];
        snprintf(score_line, sizeof(score_line), "│             Ваш счёт: %05d   Рекорд: %05d             │", score, high_score);
        screen.print_centered(cy + 2, score_line, Style::WHITE);

        screen.print_centered(cy + 3, "│                                                        │", Style::NEON_RED);
        screen.print_centered(cy + 4, "│       ▶ Нажмите [ПРОБЕЛ] или [R] для возрождения ◀     │", Style::BOLD + Style::NEON_YELLOW);
        screen.print_centered(cy + 5, "│                                                        │", Style::NEON_RED);
        screen.print_centered(cy + 6, "│             [M] Сменить режим  •  [Q] Выход            │", Style::DARK_GRAY);
        screen.print_centered(cy + 7, "└────────────────────────────────────────────────────────┘", Style::NEON_RED);
    }
};

// ==========================================
// Main Function
// ==========================================
int main() {
    srand((unsigned int)time(nullptr));

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    enable_raw_mode();

    GameEngine engine;

    auto last_time = std::chrono::steady_clock::now();

    while (g_running) {
        auto current_time = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(current_time - last_time).count();
        last_time = current_time;

        if (dt > 0.05f) dt = 0.05f;

        engine.handle_input();
        engine.update(dt);
        engine.render();

        // 60 FPS pacing (~16.66ms per frame)
        auto frame_end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - current_time);
        int sleep_us = 16666 - (int)elapsed.count();
        if (sleep_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        }
    }

    disable_raw_mode();
    std::cout << "\nСпасибо за игру в Dino Dash TTY!\n" << std::endl;
    return 0;
}
