static int
LuaPlatformReadall(lua_State* l)
{
   int top = lua_gettop(l);
   if (!lua_isstring(l, top)) {
      lua_pushnil(l);
      return 1;
   }

   const char* path = lua_tostring(l, top);

   char* data = ReadEntireFile(path);
   if (!data) {
      lua_pushnil(l);
      return 1;
   }

   lua_pushstring(l, data);
   return 1;
}

static void
OpenPlatform(lua_State* l, bool bundle)
{
   luaL_register(l, "platform", (luaL_Reg[]){
      { "readall", LuaPlatformReadall },
      { 0, 0 },
   });

   lua_pushstring(l, OS_NAME);
   lua_setfield(l, -2, "os");

   lua_pushstring(l, ARCH_NAME);
   lua_setfield(l, -2, "arch");

   lua_pushboolean(l, bundle);
   lua_setfield(l, -2, "bundle");
}

