@echo off
setlocal
cd /d "%~dp0"
if exist ".venv\Scripts\python.exe" (set "PY=.venv\Scripts\python.exe") else (where python >nul 2>nul && set "PY=python")
if not defined PY (echo Python not found. Check .venv\Scripts\python.exe & pause & exit /b 1)
if exist "C:\Program Files\Wireshark\dumpcap.exe" (set "DUMPCAP=C:\Program Files\Wireshark\dumpcap.exe")
if exist "C:\Program Files\Wireshark\tshark.exe" (set "TSHARK=C:\Program Files\Wireshark\tshark.exe")
set /p SECONDS=Test seconds [60]:
if "%SECONDS%"=="" set "SECONDS=60"
if defined DUMPCAP "%DUMPCAP%" -D
set /p IFACE=Wireshark capture interface ID [1]:
if "%IFACE%"=="" set "IFACE=1"
if defined DUMPCAP (if defined TSHARK (%PY% acceptance_tool.py --seconds %SECONDS% --interface %IFACE% --dumpcap-path "%DUMPCAP%" --tshark-path "%TSHARK%") else (%PY% acceptance_tool.py --seconds %SECONDS% --interface %IFACE% --dumpcap-path "%DUMPCAP%")) else (%PY% acceptance_tool.py --seconds %SECONDS% --interface %IFACE%)
pause
