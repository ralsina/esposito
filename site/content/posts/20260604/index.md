---
title: ESP-Osito News for June 4, 2026
date: 2026-06-04 12:00:00 -03:00
---

## Shell App: A Command Line for Esposito

The latest addition to the app roster is a shell — a simple command-line
environment running in text mode on the 320x240 display. It supports `ls`,
`cd`, `cp`, and `mv` as builtins, and any two-token command that isn't a
builtin is treated as `<app> <file>`, launching Esposito apps directly
from the command line.

It features inline line editing with cursor movement (Fn+A/D), history
navigation (Fn+W/S), Ctrl+U to clear the line, and Ctrl+L to clear the
screen. The current working directory is displayed in a status bar and
persisted across sessions through the config API, along with up to 20
history entries.

The shell is deliberately barebones — no pipelines, no redirection, no
tab completion, no variable expansion. What it does provide is the right
foundation to add those things later.
