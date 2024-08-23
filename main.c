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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "luajit.h"

#define STB_DS_IMPLEMENTATION
#define STBDS_NO_SHORT_NAMES
#include "stb_ds.h"

#define BRUT_FILE "brut.dat"
#define BRUT_FILE_MAJOR 1
#define BRUT_FILE_MINOR 0

#define BRUT_VERSION "1.0.0"
#define BRUT_MIN_COMPRESS_SIZE 16

#if defined(_WIN32)
   #define OS_NAME "windows"
#elif defined(__APPLE__)
   #define OS_NAME "darwin"
#elif defined(__unix__)
   #define OS_NAME "unix"
#else
   #error "Unsupported platform"
#endif

#if defined(__x86_64__)
   #define ARCH_NAME "x86-64"
#elif defined(__i386__)
   #define ARCH_NAME "x86"
#elif defined(__arm__)
   #define ARCH_NAME "arm32"
#elif defined(__aarch64__)
   #define ARCH_NAME "arm64"
#else
   #error "Unsupported architecture"
#endif

#include "base64.c"
#include "fastlz.c"
#include "util.c"

#include "lib_platform.c"

static char* LoadBrutFile();
static bool CreateBrutFile();
static char* GetChunk(const char*);
static int LuaLoadChunkFromBundle(lua_State*);

const char* LUA_REQUIRE_OVERLOAD_SOURCE =
   "local __oldrequire = require\n"
   "require = function(name)\n"
   "  local mod = ___loadchunkfrombundle___(name)\n"
   "  if mod ~= nil then\n"
   "     package.preload[name] = loadstring(mod)()\n"
   "     return package.preload[name]\n"
   "  end\n"
   "  return __oldrequire(name)\n"
   "end"
;


char** MODULES = 0;
char** CHUNKS  = 0;

int
main(int argc, char* argv[])
{
   char* exe_name = "brut";
   if (argc > 0)
      { exe_name = argv[0]; }

   bool ship = false;
   while (argc > 0) {
      int len = strlen(argv[0]);
      if (strncmp(argv[0], "ship", len) == 0)
         { ship = true; }

      if (strncmp(argv[0], "-h", len) == 0) {
         printf("brut version %s (%d.%d)\n   usage: %s [-h] -- <args>\n", BRUT_VERSION, BRUT_FILE_MINOR, BRUT_FILE_MAJOR, exe_name);
         return 0;
      }

      if (strncmp(argv[0], "--", len) == 0) {
         argc -= 1;
         argv += 1;
         break;
      }

      argc -= 1;
      argv += 1;
   }

   if (ship) {
      if (!CreateBrutFile()) {
         Log("unable to create brutfile");
         return 1;
      }

      Log("wrote %s", BRUT_FILE);
      return 0;
   }

   lua_State* L = luaL_newstate();
   bool bundled = FileExists(BRUT_FILE);

   {
      luaL_openlibs(L);
      OpenPlatform(L, bundled);
   }

   char* chunk = 0;
   if (bundled) {
      chunk = LoadBrutFile();

      // preload overloaded version of 'require'
      lua_pushcclosure(L, LuaLoadChunkFromBundle, 1);
      lua_setfield(L, LUA_GLOBALSINDEX, "___loadchunkfrombundle___");
      luaL_loadstring(L, LUA_REQUIRE_OVERLOAD_SOURCE);
      lua_call(L, 0, 0);
   }
   else {
      chunk = ReadEntireFile("main.lua");
   }

   int exit_code = 0;

   if (!chunk) {
      Log("no brut.dat or main.lua found");
      exit_code = 1;
      goto cleanup;
   }

   luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_MAX);
   if (luaL_loadbuffer(L, chunk, strlen(chunk), "main.lua") != 0) {
      if (bundled) {
         Log("failed to load entrypoint chunk");
      }

      exit_code = 1;
      goto cleanup;
   }

   for (int i = 0; i < argc; i += 1)
      { lua_pushstring(L, argv[i]); }

   if (lua_pcall(L, argc, 0, 0) != 0) {
      Log("error: %s", lua_tostring(L, -1));
      exit_code = 1;
      goto cleanup;
   }

cleanup:
   lua_close(L);
   return exit_code;
}

static int
LuaLoadChunkFromBundle(lua_State* l)
{
   int top = lua_gettop(l);
   if (!lua_isstring(l, top)) {
      lua_pushnil(l);
      return 1;
   }

   const char* module = lua_tostring(l, top);
   char* chunk = GetChunk(module);
   if (!chunk) {
      lua_pushnil(l);
      return 1;
   }

   lua_pushstring(l, chunk);
   return 1;
}

static char*
GetChunk(const char* module)
{
   int len = strlen(module);
   for (int i = 0; i < stbds_arrlen(MODULES); i += 1) {
      if (strncmp(module, MODULES[i], len) == 0) {
         return CHUNKS[i];
      }
   }

   return 0;
}

static char*
LoadBrutFile()
{
   char* datfile = ReadEntireFile(BRUT_FILE);
   if (!datfile)
      { return 0; }

   int len = strlen(datfile);
   if (len < 4 || strncmp(datfile, "brut", 4) != 0) {
      Log("malformed header");
      return 0;
   }

   unsigned int off = 4;

   unsigned char major = datfile[off];
   unsigned char minor = datfile[off+1];
   if (major != BRUT_FILE_MAJOR || minor != BRUT_FILE_MINOR) {
      Log("unsupported version %d.%d", major, minor);
      return 0;
   }

   off += 2;

   char* raw_entries = datfile + off;
   while (off < len && datfile[off] != '\0')
         { off += 1; }

   char* end = 0;
   int total_entries = strtol(raw_entries, &end, 10);

   off += (int)(end - raw_entries) + 1;

   for (int i = 0; i < total_entries; i += 1) {
      char* name = &datfile[off];
      off += strlen(name) + 1;

      bool compressed = datfile[off];
      off += 1;

      char* raw_entry_length = &datfile[off];
      off += strlen(raw_entry_length) + 1;

      int entry_length = strtol(raw_entry_length, &end, 10);
      char* encoded = &datfile[off];

      int decoded_length = 0;
      char* decoded = Decode(encoded, entry_length, &decoded_length);
      if (!decoded) {
         Log("failed to decode entry %d", i);
         return 0;
      }

      char* chunk = decoded;
      if (compressed) {
         char* decomp = Decompress(decoded, decoded_length);
         if (!decomp) {
            Log("failed to decompress entry %d (%d:%d)", i, entry_length, decoded_length);
            return 0;
         }

         chunk = decomp;
         free(decoded);
      }

      stbds_arrput(MODULES, CopyString(name));
      stbds_arrput(CHUNKS, chunk);

      off += entry_length;
   }

   return GetChunk("main");
}

static bool
CreateBrutFile()
{
   DIR* dir = opendir(".");
   if (!dir)
      { return false; }

   char** files = 0;
   char** names = 0;

   struct dirent* ent = 0;
   while ((ent = readdir(dir)) != 0) {
      if (!EndsWith(ent->d_name, ".lua"))
         { continue; }

      char* data = ReadEntireFile(ent->d_name);
      if (!data) {
         Log("unable add '%s' to brutfile", ent->d_name);
         return false;
      }

      char* name = CopyString(ent->d_name);
      for (int i = strlen(name) - 1; i >= 0; i -= 1) {
         if (name[i] == '.') {
            name[i] = '\0';
            break;
         }
      }

      stbds_arrput(files, data);
      stbds_arrput(names, name);
   }

   char* buffer = 0;

   BufPush(&buffer, "brut");
   stbds_arrput(buffer, BRUT_FILE_MAJOR);
   stbds_arrput(buffer, BRUT_FILE_MINOR);
   BufPrint(&buffer, "%zu", stbds_arrlen(names));

   for (int i = 0; i < stbds_arrlen(names); i += 1) {
      char* name = names[i];
      Log("processing '%s.lua'", name);

      bool did_comp = false;
      int comp_size = 0;
      char* comp = Compress(files[i], &comp_size, &did_comp);

      int enc_size = 0;
      char* enc = Encode(comp, comp_size, &enc_size);

      BufPrint(&buffer, name);
      stbds_arrput(buffer, did_comp ? 1 : 0);
      BufPrint(&buffer, "%zu", enc_size);
      BufPushLen(&buffer, enc, enc_size);

      free(comp);
      free(enc);
   }

   stbds_arrfree(files);
   stbds_arrfree(names);

   if (!WriteEntireFile(BRUT_FILE, buffer, stbds_arrlen(buffer))) {
      stbds_arrfree(buffer);
      Log("failed to create brutfile");
      return false;
   }

   stbds_arrfree(buffer);
   return true;
}
