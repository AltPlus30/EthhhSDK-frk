extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <cstdio>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushstring(L, argv[2]); lua_setglobal(L, "trainerSource");
    const int result = luaL_dofile(L, argv[1]);
    if (result) std::fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_close(L);
    return result ? 1 : 0;
}
