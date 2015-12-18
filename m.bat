@echo off
cl /nologo /Zi /W4 /WX /Wall /wd4200 /wd4201 /wd4204 /wd4710 sys_win.c main.c /link user32.lib gdi32.lib /out:ghack.exe && ghack
