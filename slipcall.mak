ALL : ..\srcbin\slio.exe ..\srcbin\slipcall.exe ..\srcbin\sliowait.exe

slipcall.obj: slipcall.c
  cl /c /AL slipcall.c

..\srcbin\slipcall.exe : slipcall.obj
  link slipcall,..\srcbin\slipcall.exe,slipcall.map/map,,slipcall.def/ST:8192
