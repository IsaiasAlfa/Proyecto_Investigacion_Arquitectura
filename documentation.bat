@echo off
rem this file will be overwritten by the project setup script
setlocal
set SIMICS_BASE_PACKAGE=C:\Users\isaia\AppData\Local\Programs\Simics\simics-7.113.0
if "%SIMICS_PYTHON_PACKAGE%"=="" (
    set SIMICS_PYTHON_PACKAGE=
)
set "SIMICS_PYTHON=%~dp0.\python.bat"
"C:\Users\isaia\AppData\Local\Programs\Simics\simics-7.113.0\bin\documentation.bat" --project "C:\Users\isaia\simics-projects\my-intel-simics-project-1" %*
