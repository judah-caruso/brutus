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

static void
Log(const char* fmt, ...)
{
   char str[512] = {0};

   va_list args;
   va_start(args, fmt);
   vsprintf(str, fmt, args);
   va_end(args);

   printf("[brut] %s\n", str);
}

static bool
EndsWith(const char* str, const char* suffix)
{
   int slen = strlen(str);
   int xlen = strlen(suffix);
   if (xlen > slen)
      { return false; }

   if (xlen == slen)
      { return strncmp(str, suffix, slen) == 0; }

   const char* ptr = str + (slen - xlen);
   return strncmp(ptr, suffix, xlen) == 0;
}

static char*
CopyString(const char* str)
{
   int len = strlen(str);
   char* ptr = malloc(len + 1);
   memcpy(ptr, str, len);
   ptr[len] = '\0';
   return ptr;
}

static void
BufPrint(char** buf, const char* fmt, ...) {
   va_list args;
   va_start(args, fmt);
   size_t len = vsnprintf(NULL, 0, fmt, args);
   char* ptr = stbds_arraddnptr(*buf, len);
   vsnprintf(ptr, len, fmt, args);
   va_end(args);
}

static void
BufPushLen(char** buf, const char* str, int len)
{
   memcpy(stbds_arraddnptr(*buf, len), str, len);
}

static void
BufPush(char** buf, const char* str)
{
   BufPushLen(buf, str, strlen(str));
}

static char*
Compress(const char* in, int* out_len, bool* out_comp)
{
   int len = strlen(in);
   if (len <= BRUT_FILE_MIN_COMPRESS_SIZE) {
      *out_len  = len;
      *out_comp = false;
      return CopyString(in);
   }

   int buf_len = (int)(((float)len) * 1.5f);
   if (buf_len < 66) {
      *out_len  = len;
      *out_comp = false;
      return CopyString(in);
   }

   char* buf = malloc(buf_len);
   *out_len  = fastlz_compress_level(2, in, len, buf);
   *out_comp = true;
   return buf;
}

static char*
Decompress(const char* in, int len)
{
   int maxlen = len * 2;
   char* buf = malloc(maxlen);
   memset(buf, 0, maxlen);

   int outlen = fastlz_decompress(in, len, buf, maxlen);
   if (outlen == 0) {
      free(buf);
      return 0;
   }

   return buf;
}

static char*
Encode(const char* in, int len, int* out_len)
{
   int enc_len = BASE64_ENCODE_OUT_SIZE(len);
   char* buf = malloc(enc_len);
   memset(buf, 0, enc_len);

   *out_len = base64_encode((unsigned char *)in, len, buf);
   return buf;
}

static char*
Decode(const char* in, int len, int* out_len)
{
   int dec_len = BASE64_DECODE_OUT_SIZE(len);
   char* buf = malloc(dec_len);
   memset(buf, 0, dec_len);

   *out_len = base64_decode(in, len, (unsigned char *)buf);
   return buf;
}

static bool
SetWorkingDirectory(const char* path)
{
#if defined(PLATFORM_WINDOWS)
   return SetCurrentDirectory(path);
#else
   return chdir(path) == 0;
#endif
}

static char*
GetExePath()
{
#if defined(PLATFORM_WINDOWS)
   char buf[MAX_PATH] = {0};

   HANDLE h = GetModuleHandleA(0);
   DWORD l = GetModuleFileNameA(h, buf, MAX_PATH);

   buf[l] = '\0';
   return CopyString(buf);
#elif defined(PLATFORM_DARWIN)
   char buf[MAXPATHLEN] = {0};

   int len = MAXPATHLEN;
   if (_NSGetExecutablePath(buf, &len) != 0)
      { return 0; }

   return CopyString(buf);
#else
   char buf[PATH_MAX] = {0};

   int len = readlink("/proc/self/exe", buf, PATH_MAX);
   if (len == -1)
      { return 0; }

   buf[len] = '\0';
   return CopyString(buf);
#endif
}

static bool
ListDirectory(const char* path, char*** out_entries)
{
#if defined(PLATFORM_WINDOWS)
   WIN32_FIND_DATA fd = {0};

   HWND h = FindFirstFileA(path, &fd);
   if (h == INVALID_HANDLE_VALUE)
      { return false; }

   do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
         { continue; }

      stbds_arrput(*out_entries, CopyString(fd.cFileName));
   } while (FindNextFileA(h, &fd));
#else
   DIR* dir = opendir(path);
   if (!dir)
      { return false; }

   struct dirent* ent = 0;
   while ((ent = readdir(dir)) != 0) {
      stbds_arrput(*out_entries, CopyString(ent->d_name));
   }
#endif

   return true;
}

#if PLATFORM_WINDOWS
static bool
FileExists(const char* path)
{
   return PathFileExistsA(path);
}

static char*
ReadEntireFile(const char* path)
{
   HANDLE fh = CreateFileA(path, FILE_GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
   if (fh == INVALID_HANDLE_VALUE)
      { return 0; }

   LARGE_INTEGER size = {0};
   if (!GetFileSizeEx(fh, &size)) {
      CloseHandle(fh);
      return 0;
   }

   int length = (int)size.QuadPart;
   char* buf = malloc(length + 1);

   int read = 0;
   if (!ReadFile(fh, buf, length, &read, 0) || read != length) {
      free(buf);
      CloseHandle(fh);
      return 0;
   }

   buf[length] = '\0';
   CloseHandle(fh);

   return buf;
}

static bool
WriteEntireFile(const char* path, const char* data, size_t len)
{
   HANDLE fh = CreateFileA(path, FILE_GENERIC_READ|FILE_GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
   if (fh == INVALID_HANDLE_VALUE)
      { return 0; }

   int wrote = 0;
   if (!WriteFile(fh, data, len, &wrote, 0) || wrote != len) {
      CloseHandle(fh);
      return false;
   }

   return true;
}

#else

static bool
FileExists(const char* path)
{
   FILE* file = fopen(path, "r");
   if (!file) { return false; }
   fclose(file);
   return true;
}

static char*
ReadEntireFile(const char* path)
{
   FILE* file = fopen(path, "r");
   if (!file)
      { goto failure; }

   int start = ftell(file);
   if (start == -1)
      { goto failure; }

   fseek(file, 0, SEEK_END);

   int len = ftell(file);
   if (len == -1)
      { goto failure; }

   fseek(file, start, SEEK_SET);

   char* data = malloc(len + 1);

   int read = fread(data, 1, len, file);
   if (read != len) {
      free(data);
      goto failure;
   }

   data[len] = '\0';
   fclose(file);

   return data;

failure:
   if (file)
      { fclose(file); }
   return 0;
}

static bool
WriteEntireFile(const char* path, const char* data, size_t len)
{
   FILE* file = fopen(path, "wb");
   if (!file)
      { return false; }

   size_t written = fwrite(data, 1, len, file);
   fclose(file);

   return written == len;
}

#endif
