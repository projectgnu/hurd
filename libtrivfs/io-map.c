/*
   Copyright (C) 1993, 1994, 1999 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include "io_S.h"
#include <assert.h>

kern_return_t
trivfs_S_io_map (struct trivfs_protid *cred,
		 mach_port_t reply,
		 mach_msg_type_name_t replytype,
		 mach_port_t *rdobj,
		 mach_msg_type_name_t *rdtype,
		 mach_port_t *wrobj,
		 mach_msg_type_name_t *wrtype)
{
  return trivfs_S_io_map_segment (cred, 0, reply, replytype,
				  rdobj, rdtype, wrobj, wrtype);
}
