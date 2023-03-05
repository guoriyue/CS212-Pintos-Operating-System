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
    char* command_arguments[(((PGSIZE / sizeof(char *) - 8) / 2)
                             * sizeof(char *) - 8) / sizeof(char *)];
    char* file_name;
    int command_arguments_number;

    struct semaphore sema_for_loading;
    /* For palloc_free_page. */
    char* fn_copy;
    /* For load result. */
    bool success;
};

/* Can't access child's thread struct in exit() because we are 
    in kernel mode, need a new structure to record the exit status. */
struct exit_status_struct
{
    int process_id;
    int exit_status;
    struct semaphore sema_wait_for_child;
    struct list_elem exit_status_elem;
    bool child_terminated;
    bool parent_terminated;
    bool has_been_waited_on;
};


void notify_children_parent_is_terminated (void);
#endif /* userprog/process.h */
