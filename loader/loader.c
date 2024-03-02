/*
 * Loader Implementation
 *
 * 2022, Operating Systems
 */

#include "exec_parser.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define STOP 139

static so_exec_t *exec;                  // exec file description
static int file_d;                       // file desc for exec files
static struct sigaction handler_default; // old handler for sigsegv

static void segv_handler(int signum, siginfo_t *info, void *context) {

  if (signum != SIGSEGV || info == NULL) {
    exit(STOP);
  }

  uintptr_t fault = (uintptr_t)info->si_addr;

  // find seg-fault segment:
  struct so_seg *segm_fault = NULL;
  struct so_seg *temp = NULL;
  int nr_segs = exec->segments_no;

  for (int k = 0; k < nr_segs; k++) {
    temp = &exec->segments[k];
    if (fault >= temp->vaddr && fault < temp->vaddr + temp->mem_size) {
      segm_fault = temp;
    }
  }

  if (segm_fault == NULL) {
    handler_default.sa_sigaction(signum, info, context);
    return;
  }

  int size_of_page = getpagesize();
  int index_of_page = (fault - segm_fault->vaddr) / size_of_page;
  int nr_of_pages = segm_fault->mem_size / size_of_page;

  // verify if it was mapped:
  // alloc vector to know to register if a certain page was mapped
  if (segm_fault->data == NULL) {
    segm_fault->data = calloc(nr_of_pages, sizeof(int));
  }

  int mapping_flags = MAP_PRIVATE | MAP_FIXED;
  int difference = 0;

  if (((int *)(segm_fault->data))[index_of_page] ==
      1) { // the page at index_of_page is mapped:
    handler_default.sa_sigaction(signum, info, context);
    return;
  }
  if (((int *)(segm_fault->data))[index_of_page] ==
      0) { // the page at index_of_page is NOT mapped:
    if (segm_fault->file_size <= segm_fault->mem_size) {
      if (segm_fault->file_size <
          index_of_page *
              size_of_page) { // "unspecified" section of the segment
        mapping_flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED;
      }
    }

    if (segm_fault->mem_size > segm_fault->file_size) {
      if ((index_of_page + 1) * size_of_page >
          segm_fault->file_size) { // difference must be set to zero afterwards
        difference = (index_of_page + 1) * size_of_page - segm_fault->file_size;
      }
      mapping_flags = MAP_PRIVATE | MAP_FIXED;
    }
  }

  char *wemmap = NULL;
  uintptr_t parameter1 = segm_fault->vaddr + index_of_page * size_of_page;
  wemmap =
      mmap((void *)parameter1, size_of_page, segm_fault->perm, mapping_flags,
           file_d, segm_fault->offset + index_of_page * size_of_page);

  if (wemmap == MAP_FAILED) {
    exit(STOP);
  }

  if (difference != 0) {
    uintptr_t parameter2 = (segm_fault->vaddr + index_of_page * size_of_page +
                            (size_of_page - difference));

    memset((void *)parameter2, 0, difference);
  }

  ((int *)(segm_fault->data))[index_of_page] = 1;
}

int so_init_loader(void) {
  int rc;
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segv_handler;
  sa.sa_flags = SA_SIGINFO;
  rc = sigaction(SIGSEGV, &sa, &handler_default);
  if (rc < 0) {
    perror("sigaction");
    return -1;
  }
  return 0;
}

int so_execute(char *path, char *argv[]) {
  file_d = open(path, O_RDWR);
  exec = so_parse_exec(path);
  if (!exec)
    return -1;

  so_start_exec(exec, argv);

  close(file_d);

  return -1;
}
