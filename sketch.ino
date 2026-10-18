
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "src/lua/lua.h"
#include "src/lua/lauxlib.h"
#include "src/lua/lualib.h"

#include <stdlib.h>

// --------------------------------------------------
// Raspberry Pi Pico W - SPI0 SD card connections
// --------------------------------------------------

static constexpr uint8_t SD_CS_PIN   = 17;
static constexpr uint8_t SD_SCK_PIN  = 18;
static constexpr uint8_t SD_MOSI_PIN = 19;
static constexpr uint8_t SD_MISO_PIN = 16;

// Maximum bytecode file size, not a guarantee that
// a program of this size can execute within available RAM.
static constexpr size_t MAX_CHUNK_SIZE = 96 * 1024;

static constexpr char PROGRAM_PATH[] = "/main.luac";

static lua_State *vm = nullptr;

// --------------------------------------------------
// Lua print() -> USB Serial
// --------------------------------------------------

static int luaSerialPrint(lua_State *L) {
    const int count = lua_gettop(L);

    for (int i = 1; i <= count; ++i) {
        size_t length = 0;
        const char *value = luaL_tolstring(L, i, &length);

        if (i > 1) {
            Serial.write('\t');
        }

        if (value != nullptr && length > 0) {
            Serial.write(
                reinterpret_cast<const uint8_t *>(value),
                length
            );
        }

        lua_pop(L, 1);
    }

    Serial.println();
    return 0;
}

// --------------------------------------------------
// Initialize selected Lua libraries
// --------------------------------------------------

static void registerLibrary(
    lua_State *L,
    const char *name,
    lua_CFunction openFunction
) {
    luaL_requiref(L, name, openFunction, 1);
    lua_pop(L, 1);
}

static void initializeLuaLibraries(lua_State *L) {
    registerLibrary(L, "_G", luaopen_base);
    registerLibrary(L, LUA_TABLIBNAME, luaopen_table);
    registerLibrary(L, LUA_STRLIBNAME, luaopen_string);
    registerLibrary(L, LUA_MATHLIBNAME, luaopen_math);
    registerLibrary(L, LUA_UTF8LIBNAME, luaopen_utf8);
    registerLibrary(L, LUA_COLIBNAME, luaopen_coroutine);

    // Use USB Serial instead of the default Lua print().
    lua_pushcfunction(L, luaSerialPrint);
    lua_setglobal(L, "print");
}

// --------------------------------------------------
// SD card initialization
// --------------------------------------------------

static bool initializeSD() {
    SPI.setRX(SD_MISO_PIN);
    SPI.setTX(SD_MOSI_PIN);
    SPI.setSCK(SD_SCK_PIN);
    SPI.begin();

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD] Initialization failed.");
        return false;
    }

    Serial.println("[SD] Ready.");
    return true;
}

// --------------------------------------------------
// Read and execute a precompiled Lua binary
// --------------------------------------------------

static bool executeProgram(const char *path) {
    File file = SD.open(path, FILE_READ);

    if (!file) {
        Serial.print("[VM] Cannot open: ");
        Serial.println(path);
        return false;
    }

    const size_t fileSize = file.size();

    if (fileSize == 0 || fileSize > MAX_CHUNK_SIZE) {
        Serial.print("[VM] Invalid file size: ");
        Serial.println(fileSize);
        file.close();
        return false;
    }

    uint8_t *buffer =
        static_cast<uint8_t *>(malloc(fileSize));

    if (buffer == nullptr) {
        Serial.println("[VM] Bytecode allocation failed.");
        file.close();
        return false;
    }

    size_t bytesRead = 0;

    while (bytesRead < fileSize) {
        const int result =
            file.read(buffer + bytesRead, fileSize - bytesRead);

        if (result <= 0) {
            break;
        }

        bytesRead += static_cast<size_t>(result);
    }

    file.close();

    if (bytesRead != fileSize) {
        Serial.println("[SD] Incomplete file read.");
        free(buffer);
        return false;
    }

    Serial.print("[VM] Loading ");
    Serial.print(path);
    Serial.print(" (");
    Serial.print(fileSize);
    Serial.println(" bytes)");

    // Binary-only mode: reject source text.
    const int loadStatus = luaL_loadbufferx(
        vm,
        reinterpret_cast<const char *>(buffer),
        fileSize,
        path,
        "b"
    );

    free(buffer);

    if (loadStatus != LUA_OK) {
        const char *message = lua_tostring(vm, -1);

        Serial.print("[VM] Load error: ");
        Serial.println(message ? message : "Unknown error");

        lua_pop(vm, 1);
        return false;
    }

    Serial.println("[VM] Executing...");

    const int runStatus = lua_pcall(
        vm,
        0,
        LUA_MULTRET,
        0
    );

    if (runStatus != LUA_OK) {
        const char *message = lua_tostring(vm, -1);

        Serial.print("[VM] Runtime error: ");
        Serial.println(message ? message : "Unknown error");

        lua_pop(vm, 1);
        lua_settop(vm, 0);
        return false;
    }

    // Discard any return values from the executed chunk.
    lua_settop(vm, 0);
    lua_gc(vm, LUA_GCCOLLECT);

    Serial.println("[VM] Execution finished.");
    return true;
}

// --------------------------------------------------
// Arduino entry points
// --------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println();
    Serial.println("PicoLuaVM - Lua 5.4");
    Serial.println("===================");

    if (!initializeSD()) {
        Serial.println("[BOOT] Check card, format and wiring.");
        return;
    }

    vm = luaL_newstate();

    if (vm == nullptr) {
        Serial.println("[VM] Failed to create Lua state.");
        return;
    }

    initializeLuaLibraries(vm);

    Serial.println("[BOOT] Lua VM initialized.");

    if (!executeProgram(PROGRAM_PATH)) {
        Serial.println("[BOOT] Program failed.");
    }
}

void loop() {
    // MVP: run one program at boot.
    delay(1000);
}
