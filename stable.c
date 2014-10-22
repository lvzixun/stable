
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>
#include <stdint.h>

#define STABLE_CACHE      "__stable_cache"
#define STABLE_ROOT       "__stable_root"
#define STABLE_ROOTIDX    "__stable_rootidx"
#define STABLE_GLOBAL NULL 

#define COLLECT_CLOSURE  " \
local type = type  \
local pairs = pairs  \
  \
local stable_cache = __stable_cache  \
local stable_root = __stable_root  \
local stable_rootidx = __stable_rootidx  \
  \
local function _mark(root, record)  \
  for k,v in pairs(root) do  \
    if type(v) == 'userdata' then  \
      if not record[v] then  \
        record[v] = true  \
        record(stable_cache[v], record)  \
      end  \
    end  \
  end  \
  return record  \
end  \
  \
  \
local function _collect(root)  \
  local record = {} \
  record[stable_rootidx] = true \
  _mark(root, record)  \
  \
  for k,v in pairs(stable_cache) do  \
    if not record[k] then  \
      stable_cache[k] = nil  \
    end  \
  end  \
  \
  collectgarbage 'collect'  \
end  \
  \
  \
function collect()  \
  return _collect(stable_root)  \
end  "


#define MAX_DEEP 16

struct stable {
  char* slot_point;
  lua_State* L;
} _INSTANCE = {0};

static int _set(lua_State* L);

inline static const char*
_lua_2typename(lua_State* L, int idx) {
  return lua_typename(L, lua_type(L, idx));
}

static void
_init_collect(lua_State* src) {
  lua_State* L = _INSTANCE.L;
  if (luaL_dostring(L, COLLECT_CLOSURE)) {
    luaL_error(src, "load collect function error: %s", lua_tostring(L, -1));
  }
}

static void
_init_instance(lua_State* src) {
  lua_State* L = _INSTANCE.L;
  _INSTANCE.slot_point = STABLE_GLOBAL;

  lua_newtable(L);  // cache table
  luaL_openlibs(L);

  lua_pushlightuserdata(L, _INSTANCE.slot_point);
  lua_newtable(L);

  // set root
  lua_pushvalue(L, -1);
  lua_setglobal(L, STABLE_ROOT);

  // set rootidx
  lua_pushlightuserdata(L, STABLE_GLOBAL);
  lua_setglobal(L, STABLE_ROOTIDX);

  // add root table to cache
  lua_settable(L, -3);
  _INSTANCE.slot_point++;

  lua_setglobal(L, STABLE_CACHE);
}


static struct stable*  
_get_instance(lua_State* src){
  if(_INSTANCE.L == NULL){
    _INSTANCE.L = luaL_newstate();
    _init_instance(src);
    _init_collect(src);
  }

  return &_INSTANCE;
}


static void
_check_key(lua_State* L, int idx) {
  int t = lua_type(L, idx);
  switch(t) {
    case LUA_TNUMBER:
    case LUA_TBOOLEAN:
    case LUA_TSTRING:
      break;
    default:
      luaL_error(L, "unexpect key type: %s", _lua_2typename(L, idx));
  }
}
 
static void
_check_value(lua_State* L, int idx) {
  int t = lua_type(L, idx);
  switch(t) {
    case LUA_TNUMBER:
    case LUA_TBOOLEAN:
    case LUA_TSTRING:
    case LUA_TNIL:
      break;
    default:
      luaL_error(L, "unexpect value type: %s", _lua_2typename(L, idx));
  }
}

static void*
_check_table(lua_State* L, int idx) {
  if(lua_type(L, idx) != LUA_TLIGHTUSERDATA)
    luaL_error(L, "error stable type: %s", _lua_2typename(L, idx));
  return lua_touserdata(L, idx);
}

static void
_new_cachevalue(void *p) {
  lua_State* L = _INSTANCE.L;
  int top = lua_gettop(L);
  lua_getglobal(L, STABLE_CACHE);
  lua_pushlightuserdata(L, p);
  lua_newtable(L);
  lua_settable(L, -3);
  lua_settop(L, top);
}

static void
_get_cachevalue(void* kp) {
  lua_State* L = _INSTANCE.L;
  lua_getglobal(L, STABLE_CACHE);
  lua_pushlightuserdata(L, kp);
  lua_gettable(L, -2);
}

static void
_copy_value(lua_State* src, lua_State* des, int idx, lua_State* handle) {
  int tv = lua_type(src, idx);
  switch(tv) {
    case LUA_TBOOLEAN:
      lua_pushboolean(des, lua_toboolean(src, idx));
      break;
    case LUA_TSTRING:
      lua_pushstring(des, lua_tostring(src, idx));
      break;
    case LUA_TNUMBER:
      lua_pushnumber(des, lua_tonumber(src, idx));
      break;
    case LUA_TNIL:
      lua_pushnil(des);
      break;
    case LUA_TLIGHTUSERDATA:
      lua_pushlightuserdata(des, lua_touserdata(src, idx));
      break;
    default:
      luaL_error(handle, "unsport type: %s", _lua_2typename(src, idx));
  }
}


static int
_newtable(lua_State* L) {
  int base = lua_gettop(L);
  void* tp = _check_table(L, base-1);
  _get_cachevalue(tp);  // cache[tp]

  _copy_value(L, _INSTANCE.L, base, L);
  _check_key(_INSTANCE.L, -1);

  void* np = (_INSTANCE.slot_point)++;
  lua_pushlightuserdata(_INSTANCE.L, np);
  lua_settable(_INSTANCE.L, -3);

  _new_cachevalue(np);  // cache[np] = {}

  lua_pushlightuserdata(L, np);
  lua_settop(_INSTANCE.L, 0);
  return 1;
}

static void
_set_table(lua_State* L, int deep) {
  int base = lua_gettop(L);
  _check_table(L, base-2);
  _check_key(L, base-1);
  luaL_checktype(L, base, LUA_TTABLE);

  lua_pushvalue(L, base-2);
  lua_pushvalue(L, base-1);
  _newtable(L);

  void* np = lua_touserdata(L, -1);
  lua_pop(L, 1);

  lua_pushnil(L);
  while(lua_next(L, base)){
    _check_key(L, -2);
    int tv = lua_type(L, -1);
    int top = lua_gettop(L);

    if(tv == LUA_TTABLE){
      if(deep > MAX_DEEP){
        luaL_error(L, "the table is to deep. [%d]", MAX_DEEP);
      }

      lua_pushlightuserdata(L, np);
      lua_pushvalue(L, top-1);
      lua_pushvalue(L, top);
      _set_table(L, deep+1);
      lua_pop(L, 4);
    } else {
      lua_pushlightuserdata(L, np);
      lua_pushvalue(L, top-1);
      lua_pushvalue(L, top);
      _set(L);
      lua_pop(L, 4);
    }
  }

  lua_settop(L, base);
}


static int
_set(lua_State* L) {
  int top = lua_gettop(L);
  void* tp = _check_table(L, top-2);
  _check_key(L, top-1);
  if(lua_type(L, top) == LUA_TTABLE){
    _set_table(L, 0);
    return 0;
  }else {
    _check_value(L, top);
  }

  _get_cachevalue(tp);
  _copy_value(L, _INSTANCE.L, top-1, L);
  
  _copy_value(L, _INSTANCE.L, top, L);

  lua_settable(_INSTANCE.L, -3);

  lua_settop(_INSTANCE.L, 0);
  lua_settop(L, top);

  return 0;
}

static int 
_get(lua_State* L) {
  void* tp = _check_table(L, 1);
  _check_key(L, 2);

  _get_cachevalue(tp);
  _copy_value(L, _INSTANCE.L, 2, L);

  lua_gettable(_INSTANCE.L, -2);
  _copy_value(_INSTANCE.L, L, -1, L);
  lua_settop(_INSTANCE.L, 0);

  return 1;
}

static int
_iter_stable_array(lua_State* L){
  int idx = luaL_checkinteger(L, 2) + 1;
  lua_pop(L, 1);
    
  lua_pushinteger(L, idx);
  _get(L);
  if(lua_type(L, -1) == LUA_TNIL)
    return 0;

  lua_pushinteger(L, idx);
  lua_pushvalue(L, -2);

  return 2;
}

static int
_ipairs(lua_State* L) {
  lua_pushcfunction(L, _iter_stable_array);
  lua_pushvalue(L, 1);
  lua_pushinteger(L, 0);
  return 3;
}


static int
_next_stable(lua_State* L){
  void* p = _check_table(L, 1);

  _get_cachevalue(p);
  _copy_value(L, _INSTANCE.L, 2, L);

  if(0 != lua_next(_INSTANCE.L, -2)){
    _copy_value(_INSTANCE.L, L, -2, L);
    _copy_value(_INSTANCE.L, L, -1, L);
    lua_settop(_INSTANCE.L, 0);
    return 2;
  }
  lua_settop(_INSTANCE.L, 0);
  return 0;
}

static int
_pairs(lua_State* L) {
  lua_pushcfunction(L, _next_stable);
  lua_pushvalue(L, 1);
  lua_pushnil(L);
  return 3;
}

static int
_collect(lua_State* L) {
  lua_getglobal(_INSTANCE.L, "collect");
  if(lua_pcall(_INSTANCE.L, 0, 0, 0)){
    luaL_error(L, "collect error: %s", lua_tostring(_INSTANCE.L, -1));
  }
  return 0;
}

static void
_init_meta(lua_State* L) {
  lua_pushlightuserdata(L, NULL);
  int m = lua_getmetatable(L, -1);

  luaL_Reg l[] = {
    {"__index", _get},
    {"__newindex", _set},
    {"__ipairs", _ipairs},
    {"__pairs", _pairs},
    {NULL, NULL},
  };

  if(m == 0)
    luaL_newlibtable(L, l);

  luaL_setfuncs(L, l, 0);
  lua_setmetatable(L, -2);
  lua_pop(L, 1);
}

int 
luaopen_stable(lua_State* L) {
  _get_instance(L);
  _init_meta(L);

  luaL_Reg l[] = {
    {"get", _get},
    {"set", _set},
    {"newtable", _newtable},
    {"ipairs", _ipairs},
    {"pairs", _pairs},
    {"collect", _collect},
    {NULL, NULL}
  };

  luaL_newlib(L, l);
  lua_pushlightuserdata(L, STABLE_GLOBAL);
  lua_setfield(L, -2, "global");
  return 1;
}


