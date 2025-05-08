# 🖥️ Unix Shell: Tiny Shell with Job Control

![License: BSD](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)

## 🚀 Overview

This project implements a **Unix-like shell** (called `tsh`) written in C, featuring **full job control**. The shell allows users to run jobs in the foreground and background, manage jobs with built-in commands, and handle signals like `SIGINT` (Ctrl+C) and `SIGTSTP` (Ctrl+Z).

---

## ✨ Key Features

- **Job Control:**
  - Start jobs in foreground (FG) or background (BG)
  - Suspend/resume jobs
  - Manage job states: `FG`, `BG`, `ST` (stopped)
- **Built-in Commands:**
  - `quit`: Exit the shell
  - `jobs`: List all jobs
  - `bg`: Resume a stopped job in the background
  - `fg`: Resume a stopped job in the foreground
- **Signal Handling:**
  - `SIGCHLD`: Reap zombie processes
  - `SIGINT`: Forward Ctrl+C to foreground job
  - `SIGTSTP`: Forward Ctrl+Z to suspend the foreground job
- **Robust Implementation:**
  - Safe I/O and error handling (e.g., `Sio_puts`, `unix_error`)
  - Race condition protection using signal blocking
  - Process group management with `setpgid()` and `tcsetpgrp()`

---

## 🗂️ Project Structure

```
unix_shell/
├── tsh.c               # The complete shell implementation
├── README.md           # Project documentation
```

---

## ▶️ How to Build & Run

1️⃣ **Compile:**

```bash
gcc -Wall -Wextra -o tsh tsh.c
```

2️⃣ **Run:**

```bash
./tsh
```

---

## 🛠️ Usage Examples

```bash
tsh> ./myprogram
tsh> ./myprogram &
tsh> jobs
tsh> fg %1
tsh> bg %1
tsh> quit
```

---

## 🔑 Implementation Highlights

- **Foreground & Background Execution:**
  - `parseline()` determines if a command should run in FG or BG.
- **Process Group Control:**
  - Ensures background jobs do not receive terminal signals.
- **Built-in Commands Handling:**
  - `builtin_cmd()` checks and runs built-ins (`jobs`, `bg`, `fg`, `quit`).
- **Signal Handlers:**
  - `sigchld_handler()`, `sigint_handler()`, `sigtstp_handler()` handle signals robustly and asynchronously.
- **Job List Management:**
  - `addjob()`, `deletejob()`, `listjobs()` manage an internal job table.

---

## 🧪 Testing

- **Foreground Jobs:**
  - Run a command and interrupt it using Ctrl+C.
- **Background Jobs:**
  - Run a command with `&`, stop it using Ctrl+Z, and resume using `bg`/`fg`.
- **Job List:**
  - Use `jobs` to view all active jobs.

---

## 👥 Author

- **Yuesong Huang** (yhu116@u.rochester.edu)

---

## 📄 License

This project is licensed under the BSD 3-Clause License – see the [LICENSE](LICENSE) file for details.

---
