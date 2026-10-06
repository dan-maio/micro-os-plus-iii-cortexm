/*
 * This file is part of the µOS++ project (https://micro-os-plus.github.io/).
 * Copyright (c) 2026 Dan. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose is hereby granted, under the terms of the MIT license.
 */

/*
 * newlib's getentropy() is implemented in terms of the _getentropy() syscall
 * stub, which lives in libnosys (not in libc). The Cortex-M33 QEMU images
 * link the kernel's own syscalls, not libnosys, so provide the stub here.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

int
_getentropy (void* buffer, size_t length)
{
  if (buffer == NULL)
    {
      errno = EFAULT;
      return -1;
    }

  // Deterministic placeholder: the emulated targets have no entropy source.
  uint8_t* p = (uint8_t*)buffer;
  for (size_t i = 0; i < length; ++i)
    {
      p[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }

  return 0;
}
