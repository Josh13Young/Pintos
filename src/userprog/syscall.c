#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include <stdlib.h>

// Task 2
static void syscall_handler (struct intr_frame *);
static void sc_halt(void);
static void sc_exit(int);
static pid_t sc_exec(const char *);
static int sc_wait (pid_t);
static bool sc_create (const char *, unsigned );
static bool sc_remove (const char *);
static int sc_open (const char *);
static int sc_filesize (int);
static int sc_read (int, void *, unsigned);
static int sc_write (int, const void *, unsigned);
static void sc_seek (int, unsigned);
static unsigned sc_tell (int);
static void sc_close (int);
static struct file* get_file(int);

// Task 2
/* Struct for storing and converting file to fd */
struct file_with_fd {
  struct file* file_ptr;
  int fd;
  struct list_elem elem;
};


void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /*
  if (!is_user_vaddr(f->esp)) {
    //page_fault(&f);
    return;
  }
  if (!pagedir_get_page(thread_current()->pagedir, f->esp)) {
    //page_fault(&f);
    return;
  }
  */
  int syscall_num = (int)f->esp;

  int status;
  char *file;
  const char *cmd_line;
  pid_t pid;
  unsigned initial_size;
  int fd;
  void *buffer;
  unsigned size;
  unsigned position;

  switch (syscall_num) {
  case SYS_HALT:
    sc_halt();
    break;

  case SYS_EXIT:
    status = (int)(f->esp) + 1;
    sc_exit(status);
    break;

  case SYS_EXEC:
    cmd_line = (char *)((int)(f->esp) + 1); // first argv
    f->eax = (uint32_t) sc_exec(cmd_line);
    break;

  case SYS_WAIT:
    pid = (int)(f->esp) + 1;
    f->eax = (uint32_t) sc_wait(pid);
    break;

  case SYS_CREATE:
    file = (char *)((int)(f->esp) + 1);
    initial_size = (int)(f->esp) + 2;
    f->eax = (uint32_t) sc_create(file, initial_size);
    break;
    
  case SYS_REMOVE:
    file = (char *)((int)(f->esp) + 1);
    f->eax = (uint32_t) sc_remove(file);
    break;

  case SYS_OPEN:
    file = (char *)((int)(f->esp) + 1);
    f->eax = (uint32_t) sc_open(file);
    break;

  case SYS_FILESIZE:
    fd = (int)(f->esp) + 1;
    f->eax = (uint32_t) sc_filesize(fd);
    break;
    
  case SYS_READ:
    fd = (int)(f->esp) + 1;
    buffer = (int)(f->esp) + 2;
    size = (int)(f->esp) + 3;
    f->eax = (uint32_t) sc_read(fd, buffer, size);
    break; 

  case SYS_WRITE:
    fd = (int)(f->esp) + 1;
    buffer = (int)(f->esp) + 2;
    size = (int)(f->esp) + 3;
    f->eax = (uint32_t) sc_write(fd, buffer, size);
    break;

  case SYS_SEEK:
    fd = (int)(f->esp) + 1;
    position = (int)(f->esp) + 2;
    sc_seek(fd, position);
    break;

  case SYS_TELL:
    fd = (int)(f->esp) + 1;
    f->eax = (uint32_t) sc_tell(fd);
    break;
    
  case SYS_CLOSE:
    fd = (int)(f->esp) + 1;
    sc_close(fd);
    break; 
  }

  thread_exit ();
}

// Task 2
static void sc_halt(void) {
  shutdown_power_off();
}

static void sc_exit(int status) {
  thread_current()->exit_status = status;
  thread_exit();
}

//to do
static pid_t sc_exec(const char *cmd_line) {
  return process_execute(cmd_line);
}

static int sc_wait (pid_t pid) {
  return process_wait(pid);
}

static bool sc_create (const char *file, unsigned initial_size) {
  lock_acquire(&file_lock);
  bool created = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return created;
}

static bool sc_remove (const char *file) {
  lock_acquire(&file_lock);
  bool removed = filesys_remove(file);
  lock_release(&file_lock);
  return removed;
}

static int sc_open (const char *file) {
  lock_acquire(&file_lock);
  struct file_with_fd* new_file_with_fd;
  struct file* new_file = filesys_open(file);
  if (new_file == NULL) {
    return -1;
  }

  /* Generate new fd for the file and store the conversion 
     in file_list of current thread */
  new_file_with_fd->file_ptr = new_file;
  new_file_with_fd->fd = list_size(&thread_current()->file_list); 
  list_push_back(&thread_current()->file_list, &new_file_with_fd->elem);
  lock_release(&file_lock);
  return new_file_with_fd->fd;
}

//to do
static int sc_filesize (int fd) {
  struct file *file = get_file(fd);
  lock_acquire(&file_lock);
  int size = file_length(file);
  lock_release(&file_lock);
  return size;
}

//to do
static int sc_read (int fd, void *buffer, unsigned size) {
  if (fd == 0){
    uint8_t *temp_buff = (uint8_t *) buffer;
    for (int i = 0; i < size; i++) {
      temp_buff[i] = input_getc();
    }
    return size;
  }
  
  if (fd > 0) {
    lock_acquire(&file_lock);
    off_t size_read = file_read(fd, buffer, size);
    lock_release(&file_lock);
    return size_read;
  }

  return -1;
}

static int sc_write (int fd, const void *buffer, unsigned size) {
  lock_acquire(&file_lock);
  /* Write to console */
  if (fd == 1) {
    /* Only write 500 characters if it contains more than 500 */
    if (size > 500) {
      putbuf((char *) buffer, 500);
      lock_release(&file_lock);
      return 500;
    }
    else {
      putbuf((char *) buffer, size);
      lock_release(&file_lock);
      return size;
    }
  }
  /* Write to file */
  else {
    struct file* file_to_write = get_file(fd);
    int write = file_write(file_to_write, buffer, size);
    lock_release(&file_lock);
    return write;
  }
}

//to do
static void sc_seek (int fd, unsigned position) {
  if (fd < 1) {
    return;
  }

  struct file *file = get_file(fd);
  if(!file) {
    return;
  }
  
  lock_acquire(&file_lock);
  file_seek(file, position);
  lock_release(&file_lock);
  
}

//to do
static unsigned sc_tell (int fd) {

  struct file *file = get_file(fd);
  
  lock_acquire(&file_lock);
  off_t pos = file_tell(file);
  lock_release(&file_lock);
  return pos;
}

//to do
static void sc_close (int fd) {
  lock_acquire(&file_lock);

  /* if the list is empty, return straight away*/
  if (list_empty(&thread_current()->file_list)) {
    lock_release(&file_lock);
    return;
  }

  /* loop through the threads file list, if the fd matches, close the file and remove it from the list the return */
  struct list_elem *temp_elem;
  for (temp_elem = list_front(&thread_current()->file_list);
    temp_elem != list_tail(&thread_current()->file_list);
    temp_elem = list_next(&temp_elem)) {
      struct file_with_fd *f = list_entry (temp, struct file_with_fd, elem);
      if (f->fd == fd){
        file_close(f->file_ptr);
        list_remove(f->elem);
        lock_release(&file_lock);
        return;
      }
    }

    /* if the file wasn't found, release the lock then return */
    lock_release(&file_lock);
    return;
}

static struct file* get_file(int fd) {
  struct list_elem* curr_elem = list_front(&thread_current()->file_list);
  for (int i = 2; i < fd; i++) {
    curr_elem = curr_elem->next;
  }
  return list_entry(curr_elem, struct file_with_fd, elem)->file_ptr;
}