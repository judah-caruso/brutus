@echo off

rem release flags
set compiler_flags=/MT /O2

rem debug flags
rem set compiler_flags=/DEBUG /Zi

cl /std:clatest /I .\src\ /I .\src\luajit\src %compiler_flags% main.c /link /libpath:.\src\luajit\src luajit.lib lua51.lib kernel32.lib shlwapi.lib -incremental:no -opt:ref -OUT:brutus.exe

del *.pdb
del *.obj
del *.exp
del *.lib
