// Copyright (c) 2024 Judah Caruso
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

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
   free(data);

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

