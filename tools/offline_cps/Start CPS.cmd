@rem SPDX-License-Identifier: MIT
@rem Copyright (c) 2026 The RadioFoundry Authors
@echo off
title RadioFoundry Offline CPS
"%~dp0runtime\python.exe" -I "%~dp0server.py" %*
if errorlevel 1 (
  echo.
  echo The CPS could not start. See the message above.
  pause
)
