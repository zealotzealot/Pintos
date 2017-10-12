#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
void process_sema_init (struct process_sema *);
bool check_pid_to_process_sema (int);
struct process_sema* pid_to_process_sema (int);
void set_exit_status (int);

int init_check = 0;
static struct list process_sema_list;

void process_sema_init (struct process_sema *process_sema){
  sema_init (&process_sema->sema, 0);
  process_sema->alive = 1;
  process_sema->exit_status = -1;
  process_sema->parent_pid = -1;
  process_sema->load_success = 0;
  list_init(&process_sema->file_desc_list);
}

void set_exit_status (int status){
  ASSERT (check_pid_to_process_sema (thread_current()->tid));
  struct process_sema *process_sema
          = pid_to_process_sema (thread_current()->tid);
  
  process_sema->exit_status = status;
}

bool
check_pid_to_process_sema (int pid){
  struct list_elem *e;
  
  for(e=list_begin(&process_sema_list); e!=list_end(&process_sema_list); e=list_next(e)){
    if(list_entry(e, struct process_sema, elem)->pid == pid){
      return true;
    }
  }
  return false;
}

struct process_sema*
pid_to_process_sema (int pid){
  struct list_elem *e;
  
  //printf("pid_to_process_sema, %s(%d) %d\n",thread_current()->name,thread_current()->tid,pid);
  for(e=list_begin(&process_sema_list); e!=list_end(&process_sema_list); e=list_next(e)){
    if(list_entry(e, struct process_sema, elem)->pid == pid){
      //printf("find!! %d %d\n",pid, list_entry(e, struct process_sema, elem)->pid);
      return list_entry(e, struct process_sema, elem);
    }
  }
  //printf("not found!!\n");
  return -1;
}

void argument_pass(char *string, void **esp){
  
  int argc=0;
  char **argv, *token, *save_ptr;
  
  for(token = strtok_r (string, " ", &save_ptr); token!=NULL;
      token=strtok_r (NULL, " ", &save_ptr)){
    
    ++argc;

    if(argc == 1) { argv = (char **) malloc(sizeof(char *) * argc); }
    else { argv = (char **) realloc(argv, sizeof(char *) * argc); }
    
    argv[ argc - 1 ] = (char *) malloc(sizeof(char) * strlen(token));
    strlcpy( argv[argc -1], token, strlen(token)+1 );

  }

  int i, argv_addr[argc];
  
  for(i=argc-1; i>=0; i--){
    *esp -= strlen(argv[i])+1;
    memcpy( *esp, argv[i], strlen(argv[i])+1 );
    argv_addr[i] = *esp;
  }

  *esp = (int)*esp & 0xfffffffc; //word-align
  
  for(i=argc; i>=0; i--){
    *esp -= sizeof(char *);
    
    if(i < argc){
      //memcpy( *esp, argv_addr[i], sizeof(char *) );
      *((int *)*esp) = argv_addr[i];
    }
  }

  memcpy(*esp-4, esp, sizeof(char *));
  *esp -= 4;

  memcpy(*esp-4, &argc, sizeof(int));
  *esp -= 4;

  *esp -= 4; // Make return address' space

}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  //printf("process_execute!! %s(%d)->%s\n",thread_current()->name,thread_current()->tid,file_name);
  char *fn_copy, *real_file_name;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  real_file_name = palloc_get_page (0);
  if (fn_copy == NULL || real_file_name == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  strlcpy (real_file_name, file_name, PGSIZE);
  
  char *save_ptr;
  real_file_name = strtok_r (real_file_name, " ", &save_ptr);
  
  if (init_check != 1){
    init_check = 1;
    list_init (&process_sema_list);
    //printf("process_sema list init! %s\n",thread_current()->name);
  }

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (real_file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR){
    palloc_free_page (fn_copy);
    palloc_free_page (real_file_name);
  }

  struct process_sema *process_sema;
  if (!check_pid_to_process_sema(tid)){ //부모가 자식보다 먼저 schedule되는 case
    process_sema = malloc (sizeof(struct process_sema));
    process_sema_init (process_sema);
  
    process_sema->pid = tid;
    process_sema->parent_pid = thread_current()->tid;
    list_push_back (&process_sema_list, &process_sema->elem);
    //printf("push %s %d\n",file_name,tid);
  }
  else{//자식이 부모보다 먼저
    process_sema = pid_to_process_sema(tid);

    if(process_sema->load_success == -1){
      return -1;
    }

    process_sema->parent_pid = thread_current()->tid;
  }
  
  //printf("%s->%s tid : %d\n\n",thread_current()->name,file_name,tid);
  return tid;
}

/* A thread function that loads a user process and makes it start
   running. */
static void
start_process (void *f_name)
{
  //printf("start process!! %s\n",thread_current()->name);
  char *save_ptr, *file_name = malloc(sizeof(char) * strlen(f_name));
  strlcpy(file_name, f_name, strlen(f_name)+1);
  file_name = strtok_r (file_name, " ", &save_ptr);

  struct intr_frame if_;
  bool success;

  int pid = thread_current()->tid;
  if (!check_pid_to_process_sema(pid)){ //자식이 부모보다 먼저 schedule되는 case
    struct process_sema *process_sema;
    process_sema = malloc (sizeof(struct process_sema));
    process_sema_init (process_sema);
  
    process_sema->pid = pid;
    list_push_back (&process_sema_list, &process_sema->elem);
    //printf("push %s %d\n",file_name,pid);
  }

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  
  if (!success){
    struct process_sema *process_sema
                          = pid_to_process_sema (thread_current()->tid);
    process_sema->load_success = -1;
    free(file_name);
    thread_exit();
  }

  argument_pass(f_name, &if_.esp);
  
  //hex_dump (if_.esp, if_.esp, PHYS_BASE-if_.esp, true);
  //palloc_free_page (file_name);
  free(file_name);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  //printf("process_wait!! %s %d\n",thread_current()->name,child_tid);
  if(init_check == 1){
    
    if  (!check_pid_to_process_sema (child_tid)){
      //printf("check out!\n");
      return -1;
    }

    struct process_sema *process_sema;
    process_sema = pid_to_process_sema (child_tid);

    if(process_sema->parent_pid != thread_current()->tid){
      //printf("parent out! %d %d\n",process_sema->parent_pid, thread_current()->tid);
      return -1;
    }
    
    if(process_sema->alive){
      sema_down (&process_sema->sema);
    }

    int exit_status = process_sema->exit_status;
    list_remove(&(process_sema->elem));
    free(process_sema);
    
    return exit_status;
  }
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  //printf("process_exit, %s %d!!\n",thread_current()->name,thread_current()->tid);

  if(init_check == 1){
    struct process_sema *process_sema
            = pid_to_process_sema (thread_current()->tid);

    sema_up_all (&process_sema->sema);
    process_sema->alive = 0;
  }

  struct thread *curr = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = curr->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      curr->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  struct process_sema *process = pid_to_process_sema(curr->tid);
  struct list *file_desc_list = &(process->file_desc_list);
  struct list_elem *e, *next;
  struct file_desc *target;
  for (e=list_begin(file_desc_list);
       e!=list_end(file_desc_list);
       e = next) {
    next = list_next(e);
    target = list_entry(e, struct file_desc, elem);
    close(target->fd);
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Do calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


