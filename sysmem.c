/**
 ******************************************************************************
 * @file      sysmem.c
 * @brief     STM32CubeIDE System Memory calls file
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include <errno.h>
#include <stdint.h>

extern int errno;
__attribute__((weak)) void *_sbrk(ptrdiff_t incr)
{
  extern char _end; /* Symbol defined in the linker script */
  static char *heap_end;
  char *prev_heap_end;

  if (heap_end == 0)
    heap_end = &_end;

  prev_heap_end = heap_end;
  if (heap_end + incr > (char *)&_end + 0x2000) /* 0x2000 = 8KB stack size */
  {
    errno = ENOMEM;
    return (void *)-1;
  }
  heap_end += incr;
  return (void *)prev_heap_end;
}
