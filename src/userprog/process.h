#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
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

    char* command_arguments[PGSIZE / sizeof(char *)];
    char* file_name;
    int command_arguments_number;

    /* For load synchronization. */
    struct semaphore *sema_for_loading;
};

#endif /* userprog/process.h */
