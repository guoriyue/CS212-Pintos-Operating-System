#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct aux_args_struct
{
    // /* Use a struct for a list and a list_elem. */
    // struct list *cmd_line_args;
    // struct list_elem cmd_line_elem;

    char* command_arguments[(PGSIZE / sizeof(char *)-8)/2];
    char* file_name;
    int command_arguments_number;

    struct semaphore sema_for_loading;
    /* For palloc_free_page. */
    char* fn_copy;
    /* For load result. */
    bool success;
};

/* Can't access child's thread struct in exit() because we are in kernel mode, need a new structure to record the exit status. */
struct exit_status_struct
{
    int process_id;
    int exit_status;
    struct semaphore sema_wait_for_child;
    struct list_elem exit_status_elem;
    int terminated;
};
#endif /* userprog/process.h */
