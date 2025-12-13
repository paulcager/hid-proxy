#include "macros.h"
#include "keydef_store.h"
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
    ['\''] = {0x00, 0x34}, // Apostrophe/At key
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

// Parse a trigger (before the '{')
static uint8_t parse_trigger(const char** p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;

    // Check for hex
    if ((*p)[0] == '0' && (*p)[1] == 'x') {
        uint8_t code = (uint8_t)strtol(*p, (char**)p, 16);
        return code;
    }

    // Check for single ASCII char followed by space or '{'
    if ((*p)[1] && (isspace((unsigned char)(*p)[1]) || (*p)[1] == '{')) {
        uint8_t code = ascii_to_hid[(int)(*p)[0]].key;
        (*p)++;
        return code;
    }

    // Check for mnemonic
    char mnemonic[32];
    int i = 0;
    while ((*p)[i] && !isspace((unsigned char)(*p)[i]) && (*p)[i] != '{' && i < 31) {
        mnemonic[i] = (*p)[i];
        i++;
    }
    mnemonic[i] = '\0';
    *p += i;

    return lookup_mnemonic_keycode(mnemonic);
}

// Add a HID report action to the current definition
static bool add_report(keydef_t* current_def, uint8_t mod, uint8_t key, void* limit) {
    if ((void*)&current_def->actions[current_def->count + 1] >= limit) {
        return false;
    }
    current_def->actions[current_def->count].type = ACTION_HID_REPORT;
    current_def->actions[current_def->count].data.hid.modifier = mod;
    current_def->actions[current_def->count].data.hid.keycode[0] = key;
    for (int i = 1; i < 6; i++) {
        current_def->actions[current_def->count].data.hid.keycode[i] = 0;
    }
    current_def->count++;
    return true;
}

// Add an MQTT publish action to the current definition
static bool add_mqtt_action(keydef_t* current_def, const char* topic, const char* message, void* limit) {
    if ((void*)&current_def->actions[current_def->count + 1] >= limit) {
        return false;
    }
    current_def->actions[current_def->count].type = ACTION_MQTT_PUBLISH;
    strncpy(current_def->actions[current_def->count].data.mqtt.topic, topic, 63);
    current_def->actions[current_def->count].data.mqtt.topic[63] = '\0';
    strncpy(current_def->actions[current_def->count].data.mqtt.message, message, 63);
    current_def->actions[current_def->count].data.mqtt.message[63] = '\0';
    current_def->count++;
    return true;
}

// DEPRECATED: Old store_t-based version - FOR TESTING ONLY
// Use parse_macros_to_kvstore() in production code
bool parse_macros(const char* input_buffer, store_t* temp_store) {
    const char* p = input_buffer;
    void* ptr = temp_store->keydefs;
    void* limit = (void*)temp_store + FLASH_STORE_SIZE;

    while (*p) {
        // Skip whitespace and comments
        while (*p && (isspace((unsigned char)*p) || *p == '#')) {
            if (*p == '#') {
                while (*p && *p != '\n') p++;
            } else {
                p++;
            }
        }

        if (!*p) break;

        // Parse optional [public] or [private] modifier
        bool require_unlock = true;  // Default to private
        if (*p == '[') {
            p++;
            if (strncmp(p, "public", 6) == 0) {
                require_unlock = false;
                p += 6;
            } else if (strncmp(p, "private", 7) == 0) {
                require_unlock = true;
                p += 7;
            }
            // Skip to ']'
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
            // Skip whitespace after ]
            while (*p && isspace((unsigned char)*p)) p++;
        }

        // Parse trigger
        uint8_t trigger = parse_trigger(&p);

        // Skip to '{'
        while (*p && *p != '{') p++;
        if (*p != '{') break;
        p++; // skip '{'

        // Create new keydef
        keydef_t* current_def = (keydef_t*)ptr;
        current_def->trigger = trigger;
        current_def->count = 0;
        current_def->require_unlock = require_unlock;

        // Parse commands until '}'
        while (*p && *p != '}') {
            // Skip whitespace
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '}') break;

            if (*p == '"') {
                // Quoted string
                p++;
                while (*p && *p != '"') {
                    char ch = *p;
                    if (ch == '\\' && p[1]) {
                        p++;
                        ch = *p;
                    }

                    hid_mapping_t mapping = ascii_to_hid[(int)ch];
                    if (!add_report(current_def, mapping.mod, mapping.key, limit)) {
                        panic("Buffer overflow in parser");
                    }
                    if (!add_report(current_def, 0x00, 0x00, limit)) {
                        panic("Buffer overflow in parser");
                    }
                    p++;
                }
                if (*p == '"') p++;

            } else if (*p == '^') {
                // Ctrl+key shorthand
                p++;
                if (*p >= 'a' && *p <= 'z') {
                    uint8_t key = 0x04 + (*p - 'a');
                    if (!add_report(current_def, 0x01, key, limit)) {
                        panic("Buffer overflow in parser");
                    }
                    p++;
                } else if (*p >= 'A' && *p <= 'Z') {
                    uint8_t key = 0x04 + (*p - 'A');
                    if (!add_report(current_def, 0x01, key, limit)) {
                        panic("Buffer overflow in parser");
                    }
                    p++;
                }

            } else if (*p == '[') {
                // Explicit report [mod:key]
                p++;
                uint8_t mod = (uint8_t)strtol(p, (char**)&p, 16);
                if (*p == ':') p++;
                uint8_t key = (uint8_t)strtol(p, (char**)&p, 16);
                if (*p == ']') p++;

                if (!add_report(current_def, mod, key, limit)) {
                    panic("Buffer overflow in parser");
                }

            } else {
                // Mnemonic
                char mnemonic[32];
                int i = 0;
                while (*p && !isspace((unsigned char)*p) && *p != '}' && *p != '"' && *p != '^' && *p != '[' && i < 31) {
                    mnemonic[i] = *p;
                    i++;
                    p++;
                }
                mnemonic[i] = '\0';

                if (i > 0) {
                    uint8_t key = lookup_mnemonic_keycode(mnemonic);
                    if (key) {
                        if (!add_report(current_def, 0x00, key, limit)) {
                            panic("Buffer overflow in parser");
                        }
                    }
                }
            }
        }

        if (*p == '}') p++;

        // Move to next keydef slot
        ptr += sizeof(keydef_t) + (current_def->count * sizeof(hid_keyboard_report_t));
    }

    // Terminator
    ((keydef_t*)ptr)->trigger = 0;

    return true;
}

// Reverse lookup: find mnemonic for a keycode
const char* keycode_to_mnemonic(uint8_t keycode) {
    for (size_t i = 0; i < sizeof(mnemonic_to_hid) / sizeof(mnemonic_to_hid[0]); i++) {
        if (mnemonic_to_hid[i].keycode == keycode) {
            return mnemonic_to_hid[i].name;
        }
    }
    return NULL;
}

// Reverse lookup: find ASCII char for a keycode (without modifier)
char keycode_to_ascii(uint8_t keycode, uint8_t modifier) {
    for (size_t c = 0; c < sizeof(ascii_to_hid) / sizeof(ascii_to_hid[0]); c++) {
        if (ascii_to_hid[c].key == keycode && ascii_to_hid[c].mod == modifier) {
            return (char)c;
        }
    }
    return 0;
}

// New kvstore-based version
bool parse_macros_to_kvstore(const char* input_buffer) {
    const char* p = input_buffer;
    int keydefs_saved = 0;

    // First pass: Delete all existing keydefs
    printf("parse_macros_to_kvstore: Deleting existing keydefs...\n");
    uint8_t triggers[256];
    int count = keydef_list(triggers, 256);
    for (int i = 0; i < count; i++) {
        keydef_delete(triggers[i]);
    }
    printf("parse_macros_to_kvstore: Deleted %d existing keydefs\n", count);

    // Parse and save each keydef
    while (*p) {
        // Skip whitespace and comments
        while (*p && (isspace((unsigned char)*p) || *p == '#')) {
            if (*p == '#') {
                while (*p && *p != '\n') p++;
            } else {
                p++;
            }
        }

        if (!*p) break;

        // Parse optional [public] or [private] modifier
        bool require_unlock = true;  // Default to private
        if (*p == '[') {
            p++;
            if (strncmp(p, "public", 6) == 0) {
                require_unlock = false;
                p += 6;
            } else if (strncmp(p, "private", 7) == 0) {
                require_unlock = true;
                p += 7;
            }
            // Skip to ']'
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
            // Skip whitespace after ]
            while (*p && isspace((unsigned char)*p)) p++;
        }

        // Parse trigger
        uint8_t trigger = parse_trigger(&p);
        if (trigger == 0) {
            printf("parse_macros_to_kvstore: Invalid trigger, skipping\n");
            // Skip to next line
            while (*p && *p != '\n') p++;
            continue;
        }

        // Skip to '{'
        while (*p && *p != '{') p++;
        if (*p != '{') break;
        p++; // skip '{'

        // Allocate temporary keydef (max 64 reports)
        keydef_t* def = keydef_alloc(trigger, 64);
        if (def == NULL) {
            printf("parse_macros_to_kvstore: Failed to allocate keydef\n");
            return false;
        }
        def->count = 0;
        def->require_unlock = require_unlock;

        // Parse commands until '}'
        void* limit = ((void*)def) + sizeof(keydef_t) + (64 * sizeof(action_t));
        while (*p && *p != '}') {
            // Skip whitespace
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '}') break;

            if (*p == '"') {
                // Quoted string
                p++;
                while (*p && *p != '"') {
                    char ch = *p;
                    if (ch == '\\' && p[1]) {
                        p++;
                        ch = *p;
                    }

                    hid_mapping_t mapping = ascii_to_hid[(int)ch];
                    if (!add_report(def, mapping.mod, mapping.key, limit)) {
                        printf("parse_macros_to_kvstore: Buffer overflow\n");
                        free(def);
                        return false;
                    }
                    if (!add_report(def, 0x00, 0x00, limit)) {
                        printf("parse_macros_to_kvstore: Buffer overflow\n");
                        free(def);
                        return false;
                    }
                    p++;
                }
                if (*p == '"') p++;

            } else if (*p == '^') {
                // Ctrl+key shorthand
                p++;
                if (*p >= 'a' && *p <= 'z') {
                    uint8_t key = 0x04 + (*p - 'a');
                    if (!add_report(def, 0x01, key, limit)) {
                        printf("parse_macros_to_kvstore: Buffer overflow\n");
                        free(def);
                        return false;
                    }
                    p++;
                } else if (*p >= 'A' && *p <= 'Z') {
                    uint8_t key = 0x04 + (*p - 'A');
                    if (!add_report(def, 0x01, key, limit)) {
                        printf("parse_macros_to_kvstore: Buffer overflow\n");
                        free(def);
                        return false;
                    }
                    p++;
                }

            } else if (*p == '[') {
                // Explicit report [mod:key]
                p++;
                uint8_t mod = (uint8_t)strtol(p, (char**)&p, 16);
                if (*p == ':') p++;
                uint8_t key = (uint8_t)strtol(p, (char**)&p, 16);
                if (*p == ']') p++;

                if (!add_report(def, mod, key, limit)) {
                    printf("parse_macros_to_kvstore: Buffer overflow\n");
                    free(def);
                    return false;
                }

            } else {
                // Mnemonic or MQTT command
                char mnemonic[32];
                int i = 0;
                while (*p && !isspace((unsigned char)*p) && *p != '}' && *p != '"' && *p != '^' && *p != '[' && *p != '(' && i < 31) {
                    mnemonic[i] = *p;
                    i++;
                    p++;
                }
                mnemonic[i] = '\0';

                if (i > 0) {
                    // Check for MQTT command: MQTT("topic", "message")
                    if (strcmp(mnemonic, "MQTT") == 0 && *p == '(') {
                        p++; // Skip '('
                        while (*p && isspace((unsigned char)*p)) p++; // Skip whitespace

                        // Parse topic string
                        char topic[64] = {0};
                        if (*p == '"') {
                            p++; // Skip opening quote
                            int topic_len = 0;
                            while (*p && *p != '"' && topic_len < 63) {
                                if (*p == '\\' && p[1]) {
                                    p++; // Skip escape character
                                }
                                topic[topic_len++] = *p++;
                            }
                            topic[topic_len] = '\0';
                            if (*p == '"') p++; // Skip closing quote
                        }

                        while (*p && isspace((unsigned char)*p)) p++; // Skip whitespace
                        if (*p == ',') p++; // Skip comma
                        while (*p && isspace((unsigned char)*p)) p++; // Skip whitespace

                        // Parse message string
                        char message[64] = {0};
                        if (*p == '"') {
                            p++; // Skip opening quote
                            int msg_len = 0;
                            while (*p && *p != '"' && msg_len < 63) {
                                if (*p == '\\' && p[1]) {
                                    p++; // Skip escape character
                                }
                                message[msg_len++] = *p++;
                            }
                            message[msg_len] = '\0';
                            if (*p == '"') p++; // Skip closing quote
                        }

                        while (*p && isspace((unsigned char)*p)) p++; // Skip whitespace
                        if (*p == ')') p++; // Skip closing paren

                        // Add MQTT action
                        if (!add_mqtt_action(def, topic, message, limit)) {
                            printf("parse_macros_to_kvstore: Buffer overflow adding MQTT action\n");
                            free(def);
                            return false;
                        }
                        printf("parse_macros_to_kvstore: Added MQTT action: topic='%s' msg='%s'\n", topic, message);
                    } else {
                        // Regular mnemonic (keycode)
                        uint8_t key = lookup_mnemonic_keycode(mnemonic);
                        if (key) {
                            if (!add_report(def, 0x00, key, limit)) {
                                printf("parse_macros_to_kvstore: Buffer overflow\n");
                                free(def);
                                return false;
                            }
                        }
                    }
                }
            }
        }

        if (*p == '}') p++;

        // Save this keydef to kvstore
        if (keydef_save(def)) {
            keydefs_saved++;
            printf("parse_macros_to_kvstore: Saved keydef 0x%02X (%s, %d reports)\n",
                   trigger, require_unlock ? "private" : "public", def->count);
        } else {
            printf("parse_macros_to_kvstore: Failed to save keydef 0x%02X\n", trigger);
            free(def);
            return false;
        }

        free(def);
    }

    printf("parse_macros_to_kvstore: Successfully saved %d keydefs\n", keydefs_saved);
    return true;
}

bool serialize_macros_from_kvstore(char* output_buffer, size_t buffer_size) {
    char* p = output_buffer;
    char* const limit = output_buffer + buffer_size;

    p += snprintf(p, limit - p, "# Macros file - Format: [public|private] trigger { commands... }\n");
    p += snprintf(p, limit - p, "# Commands: \"text\" MNEMONIC ^C [mod:key]\n");
    p += snprintf(p, limit - p, "# [public] keydefs work when device is locked\n");
    p += snprintf(p, limit - p, "# [private] keydefs require device unlock (default)\n\n");
    if (p >= limit) {
        printf("serialize_macros_from_kvstore: Buffer overflow in header\n");
        return false;
    }

    // Get list of all keydefs from kvstore
    uint8_t triggers[256];
    int count = keydef_list(triggers, 256);

    printf("serialize_macros_from_kvstore: Found %d keydefs\n", count);

    // Serialize each keydef
    for (int idx = 0; idx < count; idx++) {
        keydef_t* def = keydef_load(triggers[idx]);
        if (def == NULL) {
            printf("serialize_macros_from_kvstore: Failed to load keydef 0x%02X\n", triggers[idx]);
            continue;
        }

        // Write [public] or [private] modifier
        const char* privacy = def->require_unlock ? "[private] " : "[public] ";
        int written = snprintf(p, limit - p, "%s", privacy);
        if (written < 0 || p + written >= limit) {
            printf("serialize_macros_from_kvstore: Buffer overflow\n");
            free(def);
            return false;
        }
        p += written;

        // Write trigger (prefer mnemonic or ASCII)
        const char* mnemonic = keycode_to_mnemonic(def->trigger);
        char ascii = keycode_to_ascii(def->trigger, 0);

        if (mnemonic) {
            written = snprintf(p, limit - p, "%s { ", mnemonic);
        } else if (ascii >= 32 && ascii <= 126) {
            written = snprintf(p, limit - p, "%c { ", ascii);
        } else {
            written = snprintf(p, limit - p, "0x%02x { ", def->trigger);
        }
        if (written < 0 || p + written >= limit) {
            printf("serialize_macros_from_kvstore: Buffer overflow\n");
            free(def);
            return false;
        }
        p += written;

        // Serialize reports - detect text sequences and output as quoted strings
        for (int i = 0; i < def->count; ) {
            // Check if this is a printable character sequence (press + release pairs)
            bool is_text_sequence = false;
            int text_start = i;
            int text_len = 0;

            // Look for text sequences: press with ASCII, then release
            while (i + 1 < def->count &&
                   def->actions[i].type == ACTION_HID_REPORT &&
                   def->actions[i + 1].type == ACTION_HID_REPORT &&
                   def->actions[i + 1].data.hid.modifier == 0 &&
                   def->actions[i + 1].data.hid.keycode[0] == 0) {
                char ch = keycode_to_ascii(def->actions[i].data.hid.keycode[0], def->actions[i].data.hid.modifier);
                if (ch >= 32 && ch <= 126) {
                    is_text_sequence = true;
                    text_len++;
                    i += 2; // Skip press and release
                } else {
                    break;
                }
            }

            if (is_text_sequence && text_len > 0) {
                // Output as quoted string
                written = snprintf(p, limit - p, "\"");
                if (written < 0 || p + written >= limit) {
                    printf("serialize_macros_from_kvstore: Buffer overflow\n");
                    free(def);
                    return false;
                }
                p += written;

                for (int j = text_start; j < text_start + text_len * 2; j += 2) {
                    char ch = keycode_to_ascii(def->actions[j].data.hid.keycode[0], def->actions[j].data.hid.modifier);
                    if (ch == '"' || ch == '\\') {
                        written = snprintf(p, limit - p, "\\%c", ch);
                    } else {
                        written = snprintf(p, limit - p, "%c", ch);
                    }
                    if (written < 0 || p + written >= limit) {
                        printf("serialize_macros_from_kvstore: Buffer overflow\n");
                        free(def);
                        return false;
                    }
                    p += written;
                }

                written = snprintf(p, limit - p, "\" ");
                if (written < 0 || p + written >= limit) {
                    printf("serialize_macros_from_kvstore: Buffer overflow\n");
                    free(def);
                    return false;
                }
                p += written;
            } else if (def->actions[i].type == ACTION_HID_REPORT) {
                // Output as raw report or mnemonic or ^X notation
                const hid_keyboard_report_t* rep = &def->actions[i].data.hid;

                // Check for Ctrl+key shorthand
                if (rep->modifier == 0x01 && rep->keycode[0] >= 0x04 && rep->keycode[0] <= 0x1d) {
                    char ctrl_char = 'a' + (rep->keycode[0] - 0x04);
                    written = snprintf(p, limit - p, "^%c ", ctrl_char);
                } else if (rep->modifier == 0 && rep->keycode[0] == 0) {
                    // Release all - use explicit report format
                    written = snprintf(p, limit - p, "[00:00] ");
                } else {
                    const char* key_mnemonic = keycode_to_mnemonic(rep->keycode[0]);
                    if (rep->modifier == 0 && key_mnemonic) {
                        written = snprintf(p, limit - p, "%s ", key_mnemonic);
                    } else {
                        written = snprintf(p, limit - p, "[%02x:%02x] ", rep->modifier, rep->keycode[0]);
                    }
                }

                if (written < 0 || p + written >= limit) {
                    printf("serialize_macros_from_kvstore: Buffer overflow\n");
                    free(def);
                    return false;
                }
                p += written;
                i++;
            } else if (def->actions[i].type == ACTION_MQTT_PUBLISH) {
                // Output MQTT action
                written = snprintf(p, limit - p, "MQTT(\"%s\", \"%s\") ",
                                 def->actions[i].data.mqtt.topic,
                                 def->actions[i].data.mqtt.message);
                if (written < 0 || p + written >= limit) {
                    printf("serialize_macros_from_kvstore: Buffer overflow\n");
                    free(def);
                    return false;
                }
                p += written;
                i++;
            } else {
                // Unknown action type, skip it
                printf("serialize_macros_from_kvstore: Unknown action type %d, skipping\n",
                      def->actions[i].type);
                i++;
            }
        }

        // Close the definition
        written = snprintf(p, limit - p, "}\n");
        if (written < 0 || p + written >= limit) {
            printf("serialize_macros_from_kvstore: Buffer overflow\n");
            free(def);
            return false;
        }
        p += written;

        free(def);
    }

    return true;
}

/*
 * =============================================================================
 * LEGACY/DEPRECATED FUNCTIONS - FOR TESTING ONLY
 * =============================================================================
 * The following functions use the old store_t flash storage format and are
 * DEPRECATED for production use. They are kept only for unit tests that validate
 * the macro parser/serializer logic without requiring kvstore initialization.
 *
 * Production code should use:
 * - parse_macros_to_kvstore() instead of parse_macros()
 * - serialize_macros_from_kvstore() instead of serialize_macros()
 * =============================================================================
 */

// Helper for old store_t-based functions (DEPRECATED - for testing only)
// Guard against redefinition when included in test files
#ifndef MACROS_TEST_API_H
static inline keydef_t *next_keydef(const keydef_t *this) {
    const void *t = this;
    t += sizeof(keydef_t) + (this->count * sizeof(action_t));
    return (keydef_t*)t;
}
#endif

// DEPRECATED: Old store_t-based version - FOR TESTING ONLY
// Use serialize_macros_from_kvstore() in production code
bool serialize_macros(const store_t* store, char* output_buffer, size_t buffer_size) {
    char* p = output_buffer;
    char* const limit = output_buffer + buffer_size;

    p += snprintf(p, limit - p, "# Macros file - Format: [public|private] trigger { commands... }\n");
    p += snprintf(p, limit - p, "# Commands: \"text\" MNEMONIC ^C [mod:key]\n");
    p += snprintf(p, limit - p, "# [public] keydefs work when device is locked\n");
    p += snprintf(p, limit - p, "# [private] keydefs require device unlock (default)\n\n");
    if (p >= limit) panic("Buffer overflow in serializer header");

    for (const keydef_t* def = store->keydefs; def->trigger != 0 && (void*)def < (void*)store + FLASH_STORE_SIZE; def = next_keydef(def)) {
        // Write [public] or [private] modifier
        const char* privacy = def->require_unlock ? "[private] " : "[public] ";
        int written = snprintf(p, limit - p, "%s", privacy);
        if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
        p += written;

        // Write trigger (prefer mnemonic or ASCII)
        const char* mnemonic = keycode_to_mnemonic(def->trigger);
        char ascii = keycode_to_ascii(def->trigger, 0);

        if (mnemonic) {
            written = snprintf(p, limit - p, "%s { ", mnemonic);
        } else if (ascii >= 32 && ascii <= 126) {
            written = snprintf(p, limit - p, "%c { ", ascii);
        } else {
            written = snprintf(p, limit - p, "0x%02x { ", def->trigger);
        }
        if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
        p += written;

        // Serialize reports - try to detect text sequences
        for (int i = 0; i < def->count; ) {
            // Check if this is a printable character sequence (press + release pairs)
            bool is_text_sequence = false;
            int text_start = i;
            int text_len = 0;

            // Look for text sequences: press with ASCII, then release
            while (i + 1 < def->count &&
                   def->actions[i].type == ACTION_HID_REPORT &&
                   def->actions[i + 1].type == ACTION_HID_REPORT &&
                   def->actions[i + 1].data.hid.modifier == 0 &&
                   def->actions[i + 1].data.hid.keycode[0] == 0) {
                char ch = keycode_to_ascii(def->actions[i].data.hid.keycode[0], def->actions[i].data.hid.modifier);
                if (ch >= 32 && ch <= 126) {
                    is_text_sequence = true;
                    text_len++;
                    i += 2; // Skip press and release
                } else {
                    break;
                }
            }

            if (is_text_sequence && text_len > 0) {
                // Output as quoted string
                written = snprintf(p, limit - p, "\"");
                if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
                p += written;

                for (int j = text_start; j < text_start + text_len * 2; j += 2) {
                    char ch = keycode_to_ascii(def->actions[j].data.hid.keycode[0], def->actions[j].data.hid.modifier);
                    if (ch == '"' || ch == '\\') {
                        written = snprintf(p, limit - p, "\\%c", ch);
                    } else {
                        written = snprintf(p, limit - p, "%c", ch);
                    }
                    if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
                    p += written;
                }

                written = snprintf(p, limit - p, "\" ");
                if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
                p += written;
            } else if (def->actions[i].type == ACTION_HID_REPORT) {
                // Output as raw report or mnemonic or ^X notation
                const hid_keyboard_report_t* rep = &def->actions[i].data.hid;

                // Check for Ctrl+key shorthand
                if (rep->modifier == 0x01 && rep->keycode[0] >= 0x04 && rep->keycode[0] <= 0x1d) {
                    char ctrl_char = 'a' + (rep->keycode[0] - 0x04);
                    written = snprintf(p, limit - p, "^%c ", ctrl_char);
                } else if (rep->modifier == 0 && rep->keycode[0] == 0) {
                    // Release all - use explicit report format
                    written = snprintf(p, limit - p, "[00:00] ");
                } else {
                    const char* key_mnemonic = keycode_to_mnemonic(rep->keycode[0]);
                    if (rep->modifier == 0 && key_mnemonic) {
                        written = snprintf(p, limit - p, "%s ", key_mnemonic);
                    } else {
                        written = snprintf(p, limit - p, "[%02x:%02x] ", rep->modifier, rep->keycode[0]);
                    }
                }

                if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
                p += written;
                i++;
            } else if (def->actions[i].type == ACTION_MQTT_PUBLISH) {
                // Output MQTT action
                written = snprintf(p, limit - p, "MQTT(\"%s\", \"%s\") ",
                                 def->actions[i].data.mqtt.topic,
                                 def->actions[i].data.mqtt.message);
                if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
                p += written;
                i++;
            } else {
                // Unknown action type, skip
                i++;
            }
        }

        written = snprintf(p, limit - p, "}\n");
        if (written < 0 || p + written >= limit) panic("Buffer overflow in serializer");
        p += written;
    }

    return true;
}
