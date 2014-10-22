#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

int  luaopen_stable(lua_State* L);

#define LUA_CODE1  " \
local stable =  stable \
stable.global.hello = 'world' \
"

#define LUA_CODE2 " \
local stable = stable \
print(stable.global.hello) \
" 

int main(int argc, char const *argv[])
{
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  luaopen_stable(L);
  lua_setglobal(L, "stable");

  luaL_dostring(L, LUA_CODE1);
  lua_close(L);

  printf("===================\n");

  L = luaL_newstate();
  luaL_openlibs(L);
  luaopen_stable(L);
  lua_setglobal(L, "stable");

  luaL_dostring(L, LUA_CODE2);
  lua_close(L);

  return 0;
}