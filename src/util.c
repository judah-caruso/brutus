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
   va_end(args);

   char* ptr = stbds_arraddnptr(*buf, len + 1);
   vsnprintf(ptr, len + 1, fmt, args);
}

static void
BufPushLen(char** buf, const char* str, int len)
{
   char* ptr = stbds_arraddnptr(*buf, len);
   memcpy(ptr, str, len);
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
   if (len <= BRUT_MIN_COMPRESS_SIZE) {
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
