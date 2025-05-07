/* 
 * tsh - A tiny shell program with job control
 * 
 * Name: Yuesong Huang
 * Email: yhu116@u.rochester.edu
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);

int builtin_cmd(char **argv);

void do_bgfg(char **argv);

void waitfg(pid_t pid);

void sigchld_handler(int sig);

void sigtstp_handler(int sig);

void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);

void sigquit_handler(int sig);

void clearjob(struct job_t *job);

void initjobs(struct job_t *jobs);

int maxjid(struct job_t *jobs);

int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);

int deletejob(struct job_t *jobs, pid_t pid);

pid_t fgpid(struct job_t *jobs);

struct job_t *getjobpid(struct job_t *jobs, pid_t pid);

struct job_t *getjobjid(struct job_t *jobs, int jid);

int pid2jid(pid_t pid);

void listjobs(struct job_t *jobs);

void usage(void);

void unix_error(char *msg);

void app_error(char *msg);

typedef void handler_t(int);

handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT, sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    Signal(SIGTTOU, SIG_IGN); // Ignore the SIGTTOU for tcsetpgrp()

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/****************
 * Error handlers
 ****************/

void Sigfillset(sigset_t *mask) {
    if (sigfillset(mask) < 0)
        unix_error("sigfillset error");
}

void Sigemptyset(sigset_t *mask) {
    if (sigemptyset(mask) < 0)
        unix_error("sigemptyset error");
}

void Sigaddset(sigset_t *mask, int signum) {
    if (sigaddset(mask, signum) < 0)
        unix_error("sigaddset error");
}

void Sigprocmask(int how, const sigset_t *mask, sigset_t *prev) {
    if (sigprocmask(how, mask, prev) < 0)
        unix_error("sigprocmask error");
}


pid_t Fork() {
    pid_t pid;

    if ((pid = fork()) < 0)
        unix_error("fork error");

    return pid;
}

void Setpgid(pid_t pid, pid_t pgid) {
    if (setpgid(pid, pgid) < 0)
        unix_error("setpgid error");
}

void Tcsetpgrp(int bg, pid_t pid, pid_t pgid) {
    if (!bg && (tcsetpgrp(pid, pgid) < 0)) {
        if (errno == ENOTTY) { // If calling tcsetpgrp() fails due to a bg process call
            if (verbose) // Ignore it unless use a verbose option to print the error message
                printf("tcsetpgrp error: Calling tcsetpgrp from the background");
        } else // throw unix_error() for other types of errno
            unix_error("tcsetpgrp error");
    }
}

pid_t Getpid() {
    pid_t pid = getpid();

    if (pid < 0)
        unix_error("getpid error");

    return pid;
}

pid_t Getpgrp() {
    // get the foreground process group ID for current process
    pid_t pgid = getpgrp();

    if (pgid < 0)
        unix_error("getpgrp error");

    return pgid;
}

void Kill(pid_t pid, int sig) {
    if (kill(pid, sig) < 0) { // This may happen while sending signals to a child just be reaped
        if (errno == ESRCH) // My shell is well protected but make sure it keeps running just in case
            printf("(%d): No such process or process group\n", abs((int) pid));
        else
            unix_error("kill error");
    }
}

/********************
 * End error handlers
 ********************/

/***************************************
 * Sio (Safe I/O) package from "csapp.h"
 ***************************************/

static size_t Sio_strlen(const char s[]) { // Safe strlen() in handlers
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}

ssize_t Sio_puts(char s[]) { // Safe STDOUT in handlers
    return write(STDOUT_FILENO, s, Sio_strlen(s));
}

void Sio_error(char s[]) { // Safe unix_error() in handlers
    Sio_puts(s);
    _exit(1);
}

/****************************
 * End sio (Safe I/O) package
 ****************************/

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/

void eval(char *cmdline) {
    char *argv[MAXARGS]; // Argument list for execve()
    char buf[MAXLINE]; // Command line
    int bg, jid; // Running background or foreground
    pid_t pid; // Process' ID

    strcpy(buf, cmdline);
    bg = parseline(buf, argv);

    if (argv[0] == NULL)
        return; // Skill empty lines

    sigset_t mask_one, prev_one;
    Sigemptyset(&mask_one);
    Sigaddset(&mask_one, SIGCHLD);

    if (!builtin_cmd(argv)) {
        Sigprocmask(SIG_BLOCK, &mask_one, &prev_one); // Block the SIGCHLD for avoid races
        if ((pid = Fork()) == 0) {
            // Create a child process and run user's job
            Setpgid(0, 0);
            Tcsetpgrp(bg, 0, Getpid());

            Sigprocmask(SIG_SETMASK, &prev_one, NULL); // Unblock the SIGCHLD for execve()

            if (execve(argv[0], argv, environ) < 0) {
                fprintf(stderr, "%s: Command not found\n", argv[0]);
                _exit(1);
                // If Command not found, child will exit so we don't need to block again
                // Use _exit() instead of exit() since child calls _exit() or exit() only when execve() failed
                // atexit(3) and on_exit(3) will no longer exist but child will still call them in the exit() case
                // exit() will also flush the parent's stdio and might cause some errors during fork()
                // learn from the given helper routines
            }
        }

        // Parent wait its child to finish if user's job is fg
        addjob(jobs, pid, !bg ? FG : BG, cmdline);
        jid = pid2jid(pid); // Avoid races of child just terminated before printf() which makes pid2jid(pid) = 0
        Sigprocmask(SIG_SETMASK, &prev_one, NULL); // Add job 1st and unblock the SIGCHLD

        if (!bg) {
            waitfg(pid);
            Tcsetpgrp(0, 0, Getpgrp()); // re-set my shell to fg group
        } else {
            printf("[%d] (%d) %s", jid, (int) pid, cmdline);
        }
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    } else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        } else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0)  /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) {
    if (!strcmp(argv[0], "quit")) // quit command exit normal
        exit(0);

    if (!strcmp(argv[0], "jobs")) {
        sigset_t mask_all, prev_all;
        Sigfillset(&mask_all);

        Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        listjobs(jobs); // Protect when using global variable
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        return 1;
    }

    if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
        do_bgfg(argv);
        return 1;
    }

    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
    if (argv[1] == NULL) { // Checks the second argument
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    struct job_t *job;
    char cmdline[MAXLINE], *ID = argv[1];
    int bg, jid, length = (int) strlen(ID);
    pid_t pid;

    sigset_t mask_one, prev_one;
    Sigemptyset(&mask_one);
    Sigaddset(&mask_one, SIGCHLD);

    if (ID[0] == '%') {
        for (int i = 1; i < length; i++) {  // Skip '%'
            if (!isdigit(ID[i])) { // check every index is digit or not
                printf("%s: argument must be a PID or %%jobid\n", argv[0]);
                return;
            }
        }

        Sigprocmask(SIG_BLOCK, &mask_one, &prev_one); // Block SIGCHLD similar to eval() for avoid races
        jid = atoi(&ID[1]);
        job = getjobjid(jobs, jid);

        if (job == NULL) {
            printf("%s: No such job\n", argv[1]);
            Sigprocmask(SIG_SETMASK, &prev_one, NULL);
            // printf() before unblock for avoiding child handler print some messages just before it
            return;
        }

        pid = job->pid;
    } else {
        for (int i = 0; i < length; i++) {
            if (!isdigit(ID[i])) { // check every index is digit or not
                printf("%s: argument must be a PID or %%jobid\n", argv[0]);
                return;
            }
        }

        Sigprocmask(SIG_BLOCK, &mask_one, &prev_one); // Block SIGCHLD similar to eval() for avoid races
        pid = atoi(ID);
        job = getjobpid(jobs, pid);

        if (job == NULL) {
            printf("(%s): No such process\n", argv[1]);
            Sigprocmask(SIG_SETMASK, &prev_one, NULL);
            return;
        }

        jid = job->jid;
    }

    strcpy(cmdline, job->cmdline); // Holds local copy of command line
    // Avoid races which job has been deleted just before printf() which makes the output "[0] (0) \0"
    job->state = ((bg = strcmp(argv[0], "fg")) == 0) ? FG : BG;
    Kill(-(pid), SIGCONT); // Use local copy of pid
    Tcsetpgrp(bg, 0, pid); // Set the fg group to this child depends on bg
    Sigprocmask(SIG_SETMASK, &prev_one, NULL);

    if (!bg) {
        waitfg(pid);
        Tcsetpgrp(0, 0, Getpgrp()); // re-set my shell to fg group
    } else {
        printf("[%d] (%d) %s", jid, (int) pid, cmdline);
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
    sigset_t mask_one, prev_one;
    Sigemptyset(&mask_one);
    Sigaddset(&mask_one, SIGCHLD);

    Sigprocmask(SIG_BLOCK, &mask_one, &prev_one); // Block SIGCHLD

    while (pid == fgpid(jobs))
        sigsuspend(&prev_one); // Temperately unblock the SIGCHLD and wait for the fg jog finish

    Sigprocmask(SIG_SETMASK, &prev_one, NULL);

    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {
    int status, olderrno = errno; // re-cover errno by signal handler later
    sigset_t mask_all, prev_all;
    pid_t pid;

    Sigfillset(&mask_all);
    // waitpid(-1, ..., ...) means the wait set is all of its children
    // WNOHANG | WUNTRACED If none of the children in the wait set has stopped or terminated
    // Return immediately with a return value of 0,
    // Ro return value equal to the PID of one of the stopped or terminated children.
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        Sigprocmask(SIG_BLOCK, &mask_all, &prev_all); // Block all signals for synchronize processes
        struct job_t *job = getjobpid(jobs, pid);

        if (WIFSTOPPED(status)) {
            printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), (int) pid, WSTOPSIG(status));
            job->state = ST; // Change its state to ST
        } else if (WIFSIGNALED(status)) {
            printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), (int) pid, WTERMSIG(status));
            deletejob(jobs, pid); // tsh recognized this special event and print a message of the signal
        } else if (WIFEXITED(status)) {
            deletejob(jobs, pid);
        }

        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }

    if (pid < 0 && (errno != ECHILD)) // Unexpected event
        Sio_error("waitpid error");
    // Ensure only use async-signal-safe functions in a handler without a Sigprocmask() protect
    errno = olderrno; // Re-set the errno
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);

    Sigprocmask(SIG_BLOCK, &mask_all, &prev_all); // Block all signals for global jobs
    pid_t pid = fgpid(jobs);
    Sigprocmask(SIG_SETMASK, &prev_all, NULL);

    if (pid)
        Kill(-pid, sig);
    errno = olderrno; // Re-set the errno
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);

    Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    pid_t pid = fgpid(jobs); // Protect when using global variable
    Sigprocmask(SIG_SETMASK, &prev_all, NULL);

    if (pid)
        Kill(-pid, sig);
    errno = olderrno; // Re-set the errno
    return;
}

/*********************
 * End signal handlers
 *********************/

/**********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose) {
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) {
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG:
                    printf("Running ");
                    break;
                case FG:
                    printf("Foreground ");
                    break;
                case ST:
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ",
                           i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}

/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    Sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
