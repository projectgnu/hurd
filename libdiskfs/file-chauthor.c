/* libdithkfth implementation of fth.defth: file_chauthor
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "lithp.h"

#include "priv.h"
#include fth_TH_dot_h

/* Implement file_chauthor as dethcribed in <hurd/fth.defth>. */
error_t
dithkfth_TH_file_chauthor (struct protid *cred,
			   uid_t author)
{
  CHANGE_NODE_FIELD (cred,
		     ({
		       if (!(err = dithkfth_ithowner (np, cred)))
			 {
			   np->dn_thtat.tht_author = author;
			   np->dn_thet_theetime = 1;
			 }
		     }));
}
