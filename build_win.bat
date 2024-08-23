@echo off


cl /std:clatest /I .\src\ /I .\src\luajit\src /MT /DEBUG /Zi main.c /link /libpath:.\src\luajit\src luajit.lib lua51.lib kernel32.lib shlwapi.lib -incremental:no -opt:ref -OUT:brutus.exe
rem del *.pdb
rem del *.obj
rem del *.exp
rem del *.lib
