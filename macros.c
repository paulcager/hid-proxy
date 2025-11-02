#include "macros.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

typedef struct {
    uint8_t mod;
    uint8_t key;
} hid_mapping_t;

typedef struct {
    const char* name;
    uint8_t keycode;
} mnemonic_map_t;

static const hid_mapping_t ascii_to_hid[164] = {
    [' '] = {0x00, 0x2c}, // Space
    ['!'] = {0x02, 0x1e}, // Shift+1
    ['"'] = {0x02, 0x1f}, // Shift+2
    ['#'] = {0x00, 0x32}, // Hash/Tilde key (ISO key next to Enter)
    ['$'] = {0x02, 0x21}, // Shift+4
    ['%'] = {0x02, 0x22}, // Shift+5
    ['&'] = {0x02, 0x24}, // Shift+7
    [0x27] = {0x00, 0x34}, // Apostrophe/At key
    ['('] = {0x02, 0x26}, // Shift+9
    [')'] = {0x02, 0x27}, // Shift+0
    ['*'] = {0x02, 0x25}, // Shift+8
    ['+'] = {0x02, 0x2e}, // Shift+Equals
    [','] = {0x00, 0x36}, // Comma
    ['-'] = {0x00, 0x2d}, // Minus
    ['.'] = {0x00, 0x37}, // Period
    ['/'] = {0x00, 0x38}, // Slash
    ['0'] = {0x00, 0x27}, // 0
    ['1'] = {0x00, 0x1e}, // 1
    ['2'] = {0x00, 0x1f}, // 2
    ['3'] = {0x00, 0x20}, // 3
    ['4'] = {0x00, 0x21}, // 4
    ['5'] = {0x00, 0x22}, // 5
    ['6'] = {0x00, 0x23}, // 6
    ['7'] = {0x00, 0x24}, // 7
    ['8'] = {0x00, 0x25}, // 8
    ['9'] = {0x00, 0x26}, // 9
    [':'] = {0x02, 0x33}, // Shift+Semicolon
    [';'] = {0x00, 0x33}, // Semicolon
    ['<'] = {0x02, 0x36}, // Shift+Comma
    ['='] = {0x00, 0x2e}, // Equals
    ['>'] = {0x02, 0x37}, // Shift+Period
    ['?'] = {0x02, 0x38}, // Shift+Slash
    ['@'] = {0x02, 0x34}, // Shift+Apostrophe
    ['A'] = {0x02, 0x04}, // Shift+A
    ['B'] = {0x02, 0x05}, // Shift+B
    ['C'] = {0x02, 0x06}, // Shift+C
    ['D'] = {0x02, 0x07}, // Shift+D
    ['E'] = {0x02, 0x08}, // Shift+E
    ['F'] = {0x02, 0x09}, // Shift+F
    ['G'] = {0x02, 0x0a}, // Shift+G
    ['H'] = {0x02, 0x0b}, // Shift+H
    ['I'] = {0x02, 0x0c}, // Shift+I
    ['J'] = {0x02, 0x0d}, // Shift+J
    ['K'] = {0x02, 0x0e}, // Shift+K
    ['L'] = {0x02, 0x0f}, // Shift+L
    ['M'] = {0x02, 0x10}, // Shift+M
    ['N'] = {0x02, 0x11}, // Shift+N
    ['O'] = {0x02, 0x12}, // Shift+O
    ['P'] = {0x02, 0x13}, // Shift+P
    ['Q'] = {0x02, 0x14}, // Shift+Q
    ['R'] = {0x02, 0x15}, // Shift+R
    ['S'] = {0x02, 0x16}, // Shift+S
    ['T'] = {0x02, 0x17}, // Shift+T
    ['U'] = {0x02, 0x18}, // Shift+U
    ['V'] = {0x02, 0x19}, // Shift+V
    ['W'] = {0x02, 0x1a}, // Shift+W
    ['X'] = {0x02, 0x1b}, // Shift+X
    ['Y'] = {0x02, 0x1c}, // Shift+Y
    ['Z'] = {0x02, 0x1d}, // Shift+Z
    ['['] = {0x00, 0x2f}, // Left Bracket
    [0x5c] = {0x00, 0x31}, // Backslash/Pipe key (ISO key next to Shift)
    [']'] = {0x00, 0x30}, // Right Bracket
    ['^'] = {0x02, 0x23}, // Shift+6
    ['_'] = {0x02, 0x2d}, // Shift+Minus
    ['`'] = {0x00, 0x35}, // Grave Accent/Backtick
    ['a'] = {0x00, 0x04}, // a
    ['b'] = {0x00, 0x05}, // b
    ['c'] = {0x00, 0x06}, // c
    ['d'] = {0x00, 0x07}, // d
    ['e'] = {0x00, 0x08}, // e
    ['f'] = {0x00, 0x09}, // f
    ['g'] = {0x00, 0x0a}, // g
    ['h'] = {0x00, 0x0b}, // h
    ['i'] = {0x00, 0x0c}, // i
    ['j'] = {0x00, 0x0d}, // j
    ['k'] = {0x00, 0x0e}, // k
    ['l'] = {0x00, 0x0f}, // l
    ['m'] = {0x00, 0x10}, // m
    ['n'] = {0x00, 0x11}, // n
    ['o'] = {0x00, 0x12}, // o
    ['p'] = {0x00, 0x13}, // p
    ['q'] = {0x00, 0x14}, // q
    ['r'] = {0x00, 0x15}, // r
    ['s'] = {0x00, 0x16}, // s
    ['t'] = {0x00, 0x17}, // t
    ['u'] = {0x00, 0x18}, // u
    ['v'] = {0x00, 0x19}, // v
    ['w'] = {0x00, 0x1a}, // w
    ['x'] = {0x00, 0x1b}, // x
    ['y'] = {0x00, 0x1c}, // y
    ['z'] = {0x00, 0x1d}, // z
    ['{'] = {0x02, 0x2f}, // Shift+Left Bracket
    ['|'] = {0x02, 0x31}, // Shift+Backslash/Pipe key
    ['}'] = {0x02, 0x30}, // Shift+Right Bracket
    ['~'] = {0x02, 0x32}, // Shift+Hash/Tilde key
    [0xa3] = {0x02, 0x20}, // Pound sign (Shift+3)
};

static const mnemonic_map_t mnemonic_to_hid[] = {
    {"ENTER", 0x28},
    {"ESC", 0x29},
    {"BACKSPACE", 0x2a},
    {"TAB", 0x2b},
    {"SPACE", 0x2c},
    {"CAPSLOCK", 0x39},
    {"F1", 0x3a},
    {"F2", 0x3b},
    {"F3", 0x3c},
    {"F4", 0x3d},
    {"F5", 0x3e},
    {"F6", 0x3f},
    {"F7", 0x40},
    {"F8", 0x41},
    {"F9", 0x42},
    {"F10", 0x43},
    {"F11", 0x44},
    {"F12", 0x45},
    {"PRINTSCREEN", 0x46},
    {"SCROLLLOCK", 0x47},
    {"PAUSE", 0x48},
    {"INSERT", 0x49},
    {"HOME", 0x4a},
    {"PAGEUP", 0x4b},
    {"DELETE", 0x4c},
    {"END", 0x4d},
    {"PAGEDOWN", 0x4e},
    {"RIGHT_ARROW", 0x4f},
    {"LEFT_ARROW", 0x50},
    {"DOWN_ARROW", 0x51},
    {"UP_ARROW", 0x52},
    {"NUMLOCK", 0x53},
    {"KP_DIVIDE", 0x54},
    {"KP_MULTIPLY", 0x55},
    {"KP_MINUS", 0x56},
    {"KP_PLUS", 0x57},
    {"KP_ENTER", 0x58},
    {"KP_1", 0x59},
    {"KP_2", 0x5a},
    {"KP_3", 0x5b},
    {"KP_4", 0x5c},
    {"KP_5", 0x5d},
    {"KP_6", 0x5e},
    {"KP_7", 0x5f},
    {"KP_8", 0x60},
    {"KP_9", 0x61},
    {"KP_0", 0x62},
    {"KP_DOT", 0x63},
    {"APPLICATION", 0x65},
    {"POWER", 0x66},
    {"KP_EQUALS", 0x67},
    {"F13", 0x68},
    {"F14", 0x69},
    {"F15", 0x6a},
    {"F16", 0x6b},
    {"F17", 0x6c},
    {"F18", 0x6d},
    {"F19", 0x6e},
    {"F20", 0x6f},
    {"F21", 0x70},
    {"F22", 0x71},
    {"F23", 0x72},
    {"F24", 0x73},
    {"EXECUTE", 0x74},
    {"HELP", 0x75},
    {"MENU", 0x76},
    {"SELECT", 0x77},
    {"STOP", 0x78},
    {"AGAIN", 0x79},
    {"UNDO", 0x7a},
    {"CUT", 0x7b},
    {"COPY", 0x7c},
    {"PASTE", 0x7d},
    {"FIND", 0x7e},
    {"MUTE", 0x7f},
    {"VOLUME_UP", 0x80},
    {"VOLUME_DOWN", 0x81},
    {"LOCKING_CAPS_LOCK", 0x82},
    {"LOCKING_NUM_LOCK", 0x83},
    {"LOCKING_SCROLL_LOCK", 0x84},
    {"KP_COMMA", 0x85},
    {"KP_EQUALS_AS400", 0x86},
    {"LEFT_CTRL", 0xe0},
    {"LEFT_SHIFT", 0xe1},
    {"LEFT_ALT", 0xe2},
    {"LEFT_GUI", 0xe3},
    {"RIGHT_CTRL", 0xe4},
    {"RIGHT_SHIFT", 0xe5},
    {"RIGHT_ALT", 0xe6},
    {"RIGHT_GUI", 0xe7},
};

static uint8_t lookup_mnemonic_keycode(const char* name) {
    for (size_t i = 0; i < sizeof(mnemonic_to_hid) / sizeof(mnemonic_to_hid[0]); i++) {
        if (strcmp(name, mnemonic_to_hid[i].name) == 0) {
            return mnemonic_to_hid[i].keycode;
        }
    }
    return 0; // Not found
}

bool parse_macros(const char* input_buffer, store_t* temp_store) {
    char line[256];
    const char* p = input_buffer;
    keydef_t* current_def = NULL;

    void* ptr = temp_store->keydefs;
    void* limit = (void*)temp_store + FLASH_STORE_SIZE;

    while (p && *p) {
        const char* eol = strchr(p, '\n');
        if (eol) {
            memcpy(line, p, eol - p);
            line[eol - p] = '\0';
            p = eol + 1;
        } else {
            strcpy(line, p);
            p = NULL;
        }

        char* trimmed_line = line;
        while (*trimmed_line && isspace((unsigned char)*trimmed_line)) trimmed_line++;

        if (*trimmed_line == '\0' || *trimmed_line == '#') continue;

        if (strncmp(trimmed_line, "define ", 7) == 0) {
            char* key_str = trimmed_line + 7;
            uint8_t keycode = 0;
            if (strlen(key_str) == 1) {
                keycode = ascii_to_hid[(int)key_str[0]].key;
            } else if (strncmp(key_str, "0x", 2) == 0) {
                keycode = (uint8_t)strtol(key_str, NULL, 16);
            } else {
                keycode = lookup_mnemonic_keycode(key_str);
            }

            current_def = (keydef_t*)ptr;
            current_def->keycode = keycode;
            current_def->used = 0;

        } else if (strcmp(trimmed_line, "end") == 0) {
            if (current_def) {
                ptr += sizeof(keydef_t) + (current_def->used * sizeof(hid_keyboard_report_t));
                current_def = NULL;
            }
        } else if (current_def) {
            if ((void*)&current_def->reports[current_def->used + 2] > limit) {
                break;
            }

            if (strncmp(trimmed_line, "type ", 5) == 0) {
                char* str = trimmed_line + 5;
                if (str[0] == '"' && str[strlen(str) - 1] == '"') {
                    str[strlen(str) - 1] = '\0';
                    str++;
                    for (int i = 0; str[i]; i++) {
                        hid_mapping_t mapping = ascii_to_hid[(int)str[i]];
                        current_def->reports[current_def->used].modifier = mapping.mod;
                        current_def->reports[current_def->used].keycode[0] = mapping.key;
                        current_def->used++;
                        current_def->reports[current_def->used].modifier = 0x00;
                        current_def->reports[current_def->used].keycode[0] = 0x00;
                        current_def->used++;
                    }
                }
            } else if (strncmp(trimmed_line, "report ", 7) == 0) {
                char* args = trimmed_line + 7;
                char* mod_str = strstr(args, "mod=0x");
                char* keys_str = strstr(args, "keys=0x");
                if (mod_str && keys_str) {
                    uint8_t mod = (uint8_t)strtol(mod_str + 6, NULL, 16);
                    current_def->reports[current_def->used].modifier = mod;
                    uint8_t key = (uint8_t)strtol(keys_str + 7, NULL, 16);
                    current_def->reports[current_def->used].keycode[0] = key;
                    current_def->used++;
                }
            }
        }
    }
    ((keydef_t*)ptr)->keycode = 0;

    return true;
}

bool serialize_macros(const store_t* store, char* output_buffer, size_t buffer_size) {
    char* p = output_buffer;
    char* const limit = output_buffer + buffer_size;

    p += snprintf(p, limit - p, "# Macros file generated by hid-proxy\n\n");

    for (const keydef_t* def = store->keydefs; def->keycode != 0 && (void*)def < (void*)store + FLASH_STORE_SIZE; def = next_keydef(def)) {
        int written = snprintf(p, limit - p, "define 0x%02x\n", def->keycode);
        if (written < 0 || p + written >= limit) return false; // Buffer full
        p += written;

        for (int i = 0; i < def->used; i++) {
            written = snprintf(p, limit - p, "  report mod=0x%02x keys=0x%02x\n", def->reports[i].modifier, def->reports[i].keycode[0]);
            if (written < 0 || p + written >= limit) return false; // Buffer full
            p += written;
        }

        written = snprintf(p, limit - p, "end\n\n");
        if (written < 0 || p + written >= limit) return false; // Buffer full
        p += written;
    }

    return true;
}
