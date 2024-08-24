#if defined(PLATFORM_WINDOWS)
   #define TEST_PATH ".\\tests"
   #define TEST_GLOB "*.*"
#else
   #define TEST_PATH "./tests"
   #define TEST_GLOB "."
#endif

int
RunLoadTests()
{
   if (!SetWorkingDirectory(TEST_PATH))
      { return 1; }

   char** entries = {0};
   if (!ListDirectory(TEST_GLOB, &entries)) {
      Log("unable to get test files");
      return 1;
   }

   char** datfiles = {0};
   for (int i = 0; i < stbds_arrlen(entries); i += 1) {
      char* entry = entries[i];
      if (EndsWith(entry, ".dat")) {
         stbds_arrput(datfiles, entry);
      }
   }

   Log("running %d test(s)...", stbds_arrlen(datfiles));

   int pass = 0;
   for (int i = 0; i < stbds_arrlen(datfiles); i += 1) {
      char* entry = datfiles[i];

      int out_len = 0;
      char* chunk = LoadBrutFile(entry, &out_len);
      if (!chunk || out_len == 0) {
         Log("%s fail", entry);
      }
      else {
         Log("%s ok", entry);
         pass += 1;
      }
   }

   Log("%d/%d ok", pass, stbds_arrlen(datfiles));
   return !(pass == stbds_arrlen(datfiles));
}
