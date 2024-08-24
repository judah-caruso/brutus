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

#if defined(_WIN32) || defined(_WIN64)
   #define WIN32_LEAN_AND_MEAN
   #include <windows.h>
   #include <shlwapi.h>
#elif defined(__APPLE__) || defined(__unix__)
   #include <unistd.h>
   #include <dirent.h>
   #include <sys/stat.h>
   #include <sys/param.h>
#endif

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "luajit.h"

#define STB_DS_IMPLEMENTATION
#define STBDS_NO_SHORT_NAMES
#include "stb_ds.h"

#define BRUTUS_VERSION "1.0.0"

#define BRUT_FILE "brut.dat"
#define BRUT_FILE_MAJOR 1
#define BRUT_FILE_MINOR 0
#define BRUT_FILE_MIN_COMPRESS_SIZE 16

#if defined(_WIN32) || defined(_WIN64)
   #define OS_NAME "windows"
   #define PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
   #define OS_NAME "darwin"
   #define PLATFORM_DARWIN 1
#elif defined(__unix__)
   #define OS_NAME "unix"
   #define PLATFORM_UNIX 1
#else
   #error "Unsupported platform"
#endif

#if defined(__x86_64__) || defined(_WIN64)
   #define ARCH_NAME "x86-64"
#elif defined(__i386__) || defined(_WIN32)
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

static char* LoadBrutFile(const char*, int* out_len);
static bool CreateBrutFile(const char*);
static char* GetChunk(const char*, int*);
static int LuaLoadChunkFromBundle(lua_State*);

const char* LUA_REQUIRE_OVERLOAD_SOURCE =
   "local __oldrequire = require\n"
   "require = function(name)\n"
   "  if package.preload[name] ~= nil then\n"
   "     return package.preload[name]\n"
   "  end\n"
   "  local mod = ___loadchunkfrombundle___(name)\n"
   "  if mod ~= nil then\n"
   "     package.preload[name] = loadstring(mod)()\n"
   "     return package.preload[name]\n"
   "  end\n"
   "  return __oldrequire(name)\n"
   "end"
;

#define __BRUT_RUN_TESTS 0
#if __BRUT_RUN_TESTS
   #include "test_runner.c"
#endif

char** MODULES = 0;
char** CHUNKS  = 0;
int*   LENGTHS = 0;

int
main(int argc, char* argv[])
{
   char* exe_name = "brutus";
   if (argc > 0)
      { exe_name = argv[0]; }

   // process command line arguments
   bool ship = false;
   while (argc > 0) {
      int len = strlen(argv[0]);
      if (strncmp(argv[0], "ship", len) == 0)
         { ship = true; }

   #if __BRUT_RUN_TESTS
      if (strncmp(argv[0], "test", len) == 0) {
         return RunLoadTests();
      }
   #endif

      if (strncmp(argv[0], "-h", len) == 0) {
         printf("brutus version %s (%d.%d)\n   usage: %s [-h] -- <args>\n", BRUTUS_VERSION, BRUT_FILE_MINOR, BRUT_FILE_MAJOR, exe_name);
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

   // if 'ship' was passed we should create a brut file rather than run one.
   if (ship) {
      if (!CreateBrutFile(BRUT_FILE)) {
         Log("unable to create %s", BRUT_FILE);
         return 2;
      }

      Log("wrote %s", BRUT_FILE);
      return 0;
   }

   char* exe_path = GetExePath();
   #if defined(PLATFORM_WINDOWS)
      char path_sep = '\\';
   #else
      char path_sep = '/';
   #endif

   for (int i = strlen(exe_path); i >= 0; i -= 1) {
      if (exe_path[i] == path_sep) {
         exe_path[i] = '\0';
         break;
      }
   }

   if (!SetWorkingDirectory(exe_path)) {
      Log("unable to set working directory");
      return 1;
   }

   lua_State* L = luaL_newstate();
   bool bundled = FileExists(BRUT_FILE);

   char* chunk   = 0;
   int chunk_len = 0;

   // setup the runtime and open all extension libraries
   {
      luaL_openlibs(L);
      OpenPlatform(L, bundled);
   }

   // try to load brut.dat or main.lua
   if (bundled) {
      chunk = LoadBrutFile(BRUT_FILE, &chunk_len);

      // if we're in a bundled context, overload 'require' to look
      // for modules contained within the bundle.
      lua_pushcclosure(L, LuaLoadChunkFromBundle, 1);
      lua_setfield(L, LUA_GLOBALSINDEX, "___loadchunkfrombundle___");

      luaL_loadstring(L, LUA_REQUIRE_OVERLOAD_SOURCE);
      lua_call(L, 0, 0);
   }
   else {
      chunk = ReadEntireFile("main.lua");
      if (chunk)
         { chunk_len = strlen(chunk); }
   }


   int exit_code = 0;

   if (!chunk || chunk_len == 0) {
      // If we're bundled with no main chunk, the brut file
      // didn't contain one, and that's not necessarily an error.
      if (bundled) {
         exit_code = 0;
         goto cleanup;
      }

      Log("no %s or main.lua found", BRUT_FILE);
      exit_code = 1;
      goto cleanup;
   }

   // load and run the entrypoint chunk.
   luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_MAX);
   if (luaL_loadbuffer(L, chunk, chunk_len, "main.lua") != 0) {
      if (bundled) {
         Log("failed to load entrypoint chunk");
      }

      exit_code = 2;
      goto cleanup;
   }

   // push command-line arguments and run the chunk.
   for (int i = 0; i < argc; i += 1)
      { lua_pushstring(L, argv[i]); }

   if (lua_pcall(L, argc, 0, 0) != 0) {
      Log("error: %s", lua_tostring(L, -1));
      exit_code = 2;
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

   int chunk_len = 0;
   char* chunk = GetChunk(module, &chunk_len);
   if (!chunk || chunk_len == 0) {
      lua_pushnil(l);
      return 1;
   }

   lua_pushlstring(l, chunk, chunk_len);
   return 1;
}

static char*
GetChunk(const char* module, int* out_len)
{
   int len = strlen(module);
   for (int i = 0; i < stbds_arrlen(MODULES); i += 1) {
      if (strncmp(module, MODULES[i], len) == 0) {
         *out_len = LENGTHS[i];
         return CHUNKS[i];
      }
   }

   return 0;
}

static char*
LoadBrutFile(const char* path, int* out_len)
{
   char* datfile = ReadEntireFile(path);
   if (!datfile)
      { return 0; }

   // check the magic number
   int len = strlen(datfile);
   if (len < 4 || strncmp(datfile, "brut", 4) != 0) {
      Log("malformed header");
      return 0;
   }

   unsigned int off = 4;

   // ensure version number matches the current runtime
   unsigned char major = datfile[off];
   unsigned char minor = datfile[off+1];
   if (major != BRUT_FILE_MAJOR || minor != BRUT_FILE_MINOR) {
      Log("unsupported version %d.%d", major, minor);
      return 0;
   }

   off += 2;

   // get number of entries in the brut file
   unsigned short total_entries = *((unsigned short*)&datfile[off]);
   off += 2;

   // decode and decompress each chunk in the file
   for (int i = 0; i < total_entries; i += 1) {
      char* name = &datfile[off];
      off += strlen(name) + 1;

      bool compressed = datfile[off];
      off += 1;

      unsigned int entry_length = *((unsigned int*)&datfile[off]);
      off += 4;

      char* encoded = &datfile[off];

      int decoded_length = 0;
      char* decoded = Decode(encoded, entry_length, &decoded_length);
      if (!decoded) {
         Log("failed to decode entry %d", i);
         return 0;
      }

      int chunk_len = decoded_length;

      char* chunk = decoded;
      if (compressed) {
         char* decomp = Decompress(decoded, decoded_length, &chunk_len);
         if (!decomp) {
            Log("failed to decompress entry %d (%d, %d)", i, entry_length, decoded_length);
            return 0;
         }

         chunk = decomp;
         free(decoded);
      }

      stbds_arrput(MODULES, CopyString(name));
      stbds_arrput(CHUNKS, chunk);
      stbds_arrput(LENGTHS, chunk_len);

      off += entry_length;
   }

   // the entrypoint chunk will always be called 'main'
   return GetChunk("main", out_len);
}

static int
BytecodeWriter(lua_State* l, const void* p, size_t len, void* ud)
{
   struct { char** buffer; int count; } *data = ud;
   BufPushLen(data->buffer, (char *)p, (int)len);
   data->count += len;
   return 0;
}

static char*
SourceToBytecode(const char* name, const char* source, int* out_len)
{
   char* buffer = 0;
   struct { char** buffer; int count; } user_data;

   lua_State* l = luaL_newstate();
   if (luaL_loadbuffer(l, source, strlen(source), name) != 0)
      { return 0; }

   user_data.buffer = &buffer;
   user_data.count  = 0;

   if (lua_dump(l, BytecodeWriter, &user_data) != 0)
      { return 0; }

   *out_len = user_data.count;

   lua_close(l);
   return buffer;
}

static bool
CreateBrutFile(const char* path)
{
   char** files = 0;
   char** names = 0;

   // mark each .lua file for processing.
   // the order of files does not matter.
   char** entries = 0;

   #if PLATFORM_WINDOWS
      ListDirectory("*.*", &entries);
   #else
      ListDirectory(".", &entries);
   #endif

   for (int i = 0; i < stbds_arrlen(entries); i += 1) {
      char* entry = entries[i];
      if (!EndsWith(entry, ".lua"))
         { continue; }

      char* data = ReadEntireFile(entry);
      if (!data) {
         Log("unable add '%s' to %s", entry, BRUT_FILE);
         return false;
      }

      char* name = CopyString(entry);
      for (int i = strlen(name) - 1; i >= 0; i -= 1) {
         if (name[i] == '.') {
            name[i] = '\0';
            break;
         }
      }

      stbds_arrput(files, data);
      stbds_arrput(names, name);
   }

   stbds_arrfree(entries);

   // a brut file (little-endian) starts with the following structure:
   // magic number (4-byte 'brut')
   // major version (byte > 0)
   // minor version (byte >= 0)
   // total entries (unsigned 16-bit integer)
   char* buffer = 0;
   BufPrint(&buffer, "brut%c%c", BRUT_FILE_MAJOR, BRUT_FILE_MINOR);

   unsigned short total_names = stbds_arrlen(names);
   BufPushLen(&buffer, (char *)&total_names, 2);

   // entries are placed sequentially and have the following structure:
   // name (null-terminated string)
   // compression marker (byte 0-1)
   // payload size (unsigned 32-bit integer)
   // payload (null-terminated string)
   //    this will always be base64 encoded.
   //    if the compression marker is 1, the
   //    payload is lz4 compressed.
   for (int i = 0; i < total_names; i += 1) {
      char* name = names[i];
      Log("processing '%s.lua'", name);

      int bc_len = 0;
      char* bc = SourceToBytecode(name, files[i], &bc_len);

      bool did_comp = false;
      int comp_len = 0;
      char* comp = Compress(bc, bc_len, &comp_len, &did_comp);

      int enc_len = 0;
      char* enc = Encode(comp, comp_len, &enc_len);

      BufPush(&buffer, name);
      BufPushLen(&buffer, "\0", 1);
      BufPushLen(&buffer, (char *)&did_comp, 1);
      BufPushLen(&buffer, (char *)&enc_len, 4);
      BufPushLen(&buffer, enc, enc_len);

      free(comp);
      free(enc);
   }

   stbds_arrfree(files);
   stbds_arrfree(names);

   if (!WriteEntireFile(path, buffer, stbds_arrlen(buffer))) {
      stbds_arrfree(buffer);
      Log("failed to create %s", BRUT_FILE);
      return false;
   }

   stbds_arrfree(buffer);
   return true;
}
