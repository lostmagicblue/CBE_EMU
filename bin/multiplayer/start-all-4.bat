@echo off
setlocal EnableExtensions

for %%P in (1 2 3 4) do start "CBE Player %%P" /D "%~dp0" cmd /c call "%~dp0start-player-%%P.bat"

echo Started four isolated clients. Start bin\start-server.bat first if the mock service is not already running.
