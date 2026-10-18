
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <stdlib.h>
#include <lua/lua.h>
#include <lua/lauxlib.h>
#include <lua/lualib.h>

// Pico W / SPI0 wiring
static constexpr uint8_t SD_CS_PIN = 17;
static constexpr uint8_t SD_SCK_PIN = 18;
static constexpr uint8_t SD_MOSI_PIN = 19;
static constexpr uint8_t SD_MISO_PIN = 16;

// Keep the bytecode buffer bounded on the RP2040.
// Increase only after checking available RAM.
static constexpr size_t MAX_CHUNK_SIZE = 96 * 1024;
static constexpr const char *CHUNK_PATH = "/main.luac";

static lua_State *vm = nullptr;

// Route Lua print() to the Arduino USB serial console.
static int luaSerialPrint(lua_State *L) {
    int n = lua_gettop(L);

    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char *s = luaL_tolstring(L, i, &len);

        if (i > 1) Serial.print('\t');
        if (s && len) {
            Serial.write(
                reinterpret_cast<const uint8_t *>(s), len
            );
        }

        lua_pop(L, 1); // remove luaL_tolstring result
    }

    Serial.println();
    return 0;
}

static void openSafeLibraries(lua_State *L) {
    // Base language functions, including assert, pairs, pcall, etc.
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);

    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);

    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);

    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);

    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);

    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);

    // Replace the default C stdout-based print().
    lua_pushcfunction(L, luaSerialPrint);
    lua_setglobal(L, "print");
}

static bool initSD() {
    SPI.setRX(SD_MISO_PIN);
    SPI.setTX(SD_MOSI_PIN);
    SPI.setSCK(SD_SCK_PIN);
    SPI.begin();

    // Uses SPI0 configured above.
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD] Initialization failed.");
        return false;
    }

    Serial.println("[SD] Ready.");
    return true;
}

static bool runBytecode(const char *path) {
    File file = SD.open(path, FILE_READ);

    if (!file) {
        Serial.print("[VM] Cannot open ");
        Serial.println(path);
        return false;
    }

    const size_t size = file.size();

    if (size == 0 || size > MAX_CHUNK_SIZE) {
        Serial.print("[VM] Invalid chunk size: ");
        Serial.println(size);
        file.close();
        return false;
    }

    uint8_t *buffer = static_cast<uint8_t *>(malloc(size));

    if (!buffer) {
        Serial.println("[VM] Not enough RAM for bytecode.");
        file.close();
        return false;
    }

    size_t received = 0;

    while (received < size) {
        int count = file.read(buffer + received, size - received);

        if (count <= 0) break;
        received += static_cast<size_t>(count);
    }

    file.close();

    if (received != size) {
        Serial.println("[SD] Short read.");
        free(buffer);
        return false;
    }

    Serial.print("[VM] Loading ");
    Serial.print(path);
    Serial.print(" (");
    Serial.print(size);
    Serial.println(" bytes)");

    // "b" accepts binary chunks only, not source text.
    int status = luaL_loadbufferx(
        vm,
        reinterpret_cast<const char *>(buffer),
        size,
        path,
        "b"
    );

    free(buffer);

    if (status != LUA_OK) {
        Serial.print("[VM] Load error: ");
        Serial.println(lua_tostring(vm, -1)
                       ? lua_tostring(vm, -1)
                       : "(unknown)");
        lua_pop(vm, 1);
        return false;
    }

    Serial.println("[VM] Executing...");

    status = lua_pcall(vm, 0, LUA_MULTRET, 0);

    if (status != LUA_OK) {
        Serial.print("[VM] Runtime error: ");
        Serial.println(lua_tostring(vm, -1)
                       ? lua_tostring(vm, -1)
                       : "(unknown)");
        lua_pop(vm, 1);
        return false;
    }

    // Discard any returned values and reclaim temporary objects.
    lua_settop(vm, 0);
    lua_gc(vm, LUA_GCCOLLECT);

    Serial.println("[VM] Execution finished.");
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println();
    Serial.println("PicoLuaVM - RP2040 Lua 5.4");
    Serial.println("--------------------------");

    if (!initSD()) {
        Serial.println("[BOOT] Check SD wiring and FAT format.");
        return;
    }

    vm = luaL_newstate();

    if (!vm) {
        Serial.println("[VM] Failed to allocate Lua state.");
        return;
    }

    openSafeLibraries(vm);

    Serial.println("[BOOT] Lua VM initialized.");
    runBytecode(CHUNK_PATH);
}

void loop() {
    // MVP: execute one program at boot.
    // A later version can add a command shell and reload command.
    delay(1000);
}
