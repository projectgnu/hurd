/* Pass 5 of GNU fsck -- check allocation maps and summaries
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "fsck.h"

pass5 ()
{
  char cgbuf[MAXBSIZE];
  struct cg *newcg = cgbuf;
  struct ocg *ocg = (struct ocg *)cgbuf;
  int savednrpos;
  struct csum cstotal;
  int i, j;
  int c;
  struct cg *cg = alloca (sblock.fs_cgsize);
  char csumbuf[fragroundup (sizeof (struct csum) * sblock.fs_ncg)];
  struct csum *sbcsums = (struct csum *)csumbuf;

  int basesize;			/* size of cg not counting flexibly sized */
  int sumsize;			/* size of block totals and pos tbl */
  int mapsize;			/* size of inode map + block map */

  int writesb;
  int writecg;
  int writecsum;

  writesb = 0;
  writecsum = 0;

  readblock (fsbtodb (&sblock, sblock.fs_csaddr), csumbuf, 
	     fragroundup (sizeof (struct csum) * sblock.fs_ncg));

  /* Construct a CG structure; initialize everything that's the same
     in each cylinder group. */
  bzero (newcg, sblock.fs_cgsize);
  newcg->cg_niblk = sblock.fs_ipg;
  switch (sblock.fs_postblformat)
    {
    case FS_42POSTBLFMT:
      /* Initialize size information */
      basesize = (char *)(&ocg->cg_btot[0]) - (char *)(&ocg->cg_link);
      sumsize = &ocg->cg_iused[0] - (char *)(&ocg->cg_btot[0]);
      mapsize = (&ocg->cg_free[howmany(sblock.fs_fpg, NBBY)]
		 - (u_char *)&ocg->cg_iused[0]);
      savednrpos = sblock.fs_nrpos;
      sblock.fs_nrpos = 8;
      break;
      
    case FS_DYNAMICPOSTBLFMT;
      /* Set fields unique to new cg structure */
      newcg->cg_btotoff = &newcg->cg_space[0] - (u_char *)(&newcg->cg_link);
      newcg->cg_boff = newcg->cg_btotoff + sblock.fs_cpg * sizeof (long);
      newcg->cg_iusedoff = newcg->cg_boff + (sblock.fs_cpg
					     * block.fs_nrpos 
					     * sizeof (short));
      newcg->cg_freeoff = newcg->cg_iusedoff + howmany (sblock.fs_ipg, NBBY);

      /* Only support sblock.fs_contigsumsize == 0 here */
      /* If we supported clustered filesystems, then we would set 
	 clustersumoff and clusteroff and nextfree off would be past
	 them. */
      newcg->cg_nextfreeoff = 
	(newcg->cg_freeoff
	 + howmany (sblock.fs_cpg * sblock.fs_spc / NSPF (&sblock), NBBY));
      newcg->cg_magic = CG_MAGIC;

      /* Set map sizes */
      basesize = &newcg->cg_space[0] - (u_char *)(&newcg->cg_link);
      sumsize = newcg->cg_iusedoff - newcg->cg_btotoff;
      mapsize = newcg->cg_nextfreeoff - newcg->cg_iusedoff;
      break;
    }
  
  bzero (&cstotal, sizeof (struct csum));

  /* Mark fragments past the end of the filesystem as used. */
  j = blknum (&sblock, sblock->fs_size + fs->fs_frag - 1);
  for (i = sblock.fs_size; i < j; i++)
    setbmap (i);
  
  /* Now walk through the cylinder groups, checking each one. */
  for (c = 0; c < sblock.fs_ncg; c++)
    {
      int dbase, dmax;
      
      /* Read the cylinder group structure */
      readblock (fsbtodb (cgtod (&sblock, c)), cg, sblock.fs_cgsize);
      writecg = 0;
      
      if (!cg_chkmagic (cg))
	pfatal ("CG %d: BAD MAGIC NUMBER\n", c);
      
      /* Compute first and last data block addresses in this group */
      dbase = cgbase (&sblock, c);
      dmax = dbase + sblock.fs_fpg;
      if (dmax > sblock.fs_size)
	dmax = sblock.fs_size;
      
      /* Initialize newcg fully; values from cg for those
	 we can't check. */
      newcg->cg_time = cg->cg_time;
      newcg->cg_cgx = c;
      if (c == sblock.fs_ncg - 1)
	newcg->cg_ncyl = sblock.fs_ncyl % sblock.fs_cpg;
      else
	newcg->cg_ncyl = sblock.fs_cpg;
      newcg->cg_ndblk = dmax - dbase;
      /* Don't set nclusterblks; we don't support that */

      newcg->cg_cs.cs_ndir = 0;
      newcg->cg_cs.cs_nffree = 0;
      newcg->cg_cs.cs_nbfree = 0;
      newcg->cg_cs.cs_nifree = sblock.fs_ipg;

      /* Check these for basic viability; if they are wrong
	 then clear them. */
      newcg->cg_rotor = cg->cg_rotor;
      newcg->cg_frotor = cg->cg_frotor;
      newcg->cg_irotor = cg->cg_irotor;
      if (newcg->cg_rotor > newcg->cg_ndblk)
	{
	  pwarn ("ILLEGAL ROTOR VALUE IN CG %d", c);
	  if (preen || reply ("FIX"))
	    {
	      if (preen)
		printf (" (FIXED)");
	      newcg->cg_rotor = 0;
	      cg->cg_rotor = 0;
	      writecg = 1;
	    }
	}
      if (newcg->cg_frotor > newcg->cg_ndblk)
	{
	  pwarn ("ILLEGAL FROTOR VALUE IN CG %d", c);
	  if (preen || reply ("FIX"))
	    {
	      if (preen)
		printf (" (FIXED)");
	      newcg->cg_frotor = 0;
	      cg->cg_frotor = 0;
	      writecg = 1;
	    }
	}
      if (newcg->cg_irotor > newcg->cg_niblk)
	{
	  pwarn ("ILLEGAL IROTOR VALUE IN CG %d", c);
	  if (preen || reply ("FIX"))
	    {
	      if (preen)
		printf (" (FIXED)");
	      newcg->cg_irotor = 0;
	      cg->cg_irotor = 0;
	      writecg = 1;
	    }
	}
      
      /* Zero the block maps and summary areas */
      bzero (&newcg->cg_frsum[0], sizeof newcg->cg_frsum);
      bzero (&cg_blktot (newcg)[0], sumsize + mapsize);
      if (sblock.fs_postblformat == FS_42POSTBLFMT)
	ocg->cg_magic = CG_MAGIC;

      /* Walk through each inode, accounting for it in
	 the inode map and in newcg->cg_cs. */
      j = fs->fs_ipg * c;
      for (i = 0; i < fs->fs_ipg; j++, i++)
	switch (inodestate[i])
	  {
	  case DIR:
	  case DIR | DIR_REF:
	    newcg->cg_cs.cs_ndir++;
	    /* Fall through... */
	  case REG:
	    newcg->cg_cs.cs_nifree--;
	    setbit (cg_inosused (newcg), i);
	  }
      /* Account for inodes 0 and 1 */
      if (c == 0)
	for (i = 0; i < ROOTINO; i++)
	  {
	    setbit (cg_inosused (newcg), i);
	    newcg->cg_cs.cs_nifree--;
	  }
      
      /* Walk through each data block, accounting for it in 
	 the block map and in newcg->cg_cs. */
      for (i = 0, d = dbase;
	   d < dmax;
	   d += sblock.fs_frag, i += sblock.fs_frag)
	{
	  int frags = 0;
	  
	  /* Set each free frag of this block in the block map;
	     count how many frags were free. */
	  for (j = 0; j < fs->fs_frag; j++)
	    {
	      if (testbmap (d + j))
		continue;
	      setbit (cg_blksfree (newcg), i + j);
	      frags++;
	    }
	  
	  /* If all the frags were free, then count this as 
	     a free block too. */
	  if (frags == fs->fs_frag)
	    {
	      newcg->cg_cs.cs_nbfree++;
	      j = cbtocylno (&sblock, i);
	      cg_blktot(newcg)[j]++;
	      cg_blks(&sblock, newcg, j)[cktorpos(&sblock, i)]++;
	      /* If we support clustering, then we'd account for this
		 in the cluster map too. */
	    }
	  else if (frags)
	    {
	      /* Partial; account for the frags. */
	      newcg->cg_cs.cs_nffree += frags;
	      blk = blkmap (&sblock, cg_blksfree (newcg), i);
	      ffs_fragacct (&sblock, blk, newcg->cg_frsum, 1);
	    }
	}
      
      /* Add this cylinder group's totals into the superblock's
	 totals. */
      cstotal.cs.nffree += newcg->cg_cs.cs_nffree;
      cstotal.cs_nbfree += newcg->cg_cs.cs_nbfree;
      cstotal.cs_nifree += newcg->cg_cs.cs_nifree;
      cstotal.cs_ndir += newcg->cg_cs.cs_ndir;

      /* Check counts in superblock */
      if (bcmp (&newcg->cg_cs, cs, sizeof (struct csum)))
	{
	  pwarn ("FREE BLK COUNTS FOR CG %d WRONG IN SUPERBLOCK", c);
	  if (preen || reply ("FIX"))
	    {
	      if (preen)
		printf (" (FIXED)");
	      bcopy (newcg->cg_cs, cs, sizeof (struct csum));
	      writecsum = 1;
	    }
	}
      
      /* Check inode and block maps */
      if (bcmp (cg_inosused (newcg), cg_inosused (cg), mapsize))
	{
	  pwarn ("BLKS OR INOS MISSING IN CG %d BIT MAPS", c);
	  if (preen || reply ("FIX"))
	    {
	      if (preen)
		printf (" (FIXED)");
	      bcopy (cg_inosused (newcg), cg_inosused (cg), mapsize);
	      writecg = 1;
	    }
	}
      
      if (bcmp (&cg_blktot(newcg)[0], &cg_blktot(cg)[0], sumsize))
	{
	  pwarn ("SUMMARY INFORMATION FOR CG %d BAD", c);
	  if (preen || reply ("FIX"))
	    {
	      if (preen)
		printf (" (FIXED)");
	      bcopy (cg_blktot(newcg)[0], &cg_blktot(cg)[0], sumsize);
	      writecg = 1;
	    }
	}
      
      if (bcmp (newcg, cg, basesize))
	{
	  pwarn ("CYLINDER GROUP %d BAD", c);
	  if (preen || reply ("FIX"))
	    {
	      if (preen)
		printf (" (FIXED)");
	      bcopy (newcg, cg, basesize);
	      writecg = 1;
	    }
	}

      if (writecg)
	writeblock (fsbtodb (cgtod (&sblock, c)), cg, sblock.fs_cgsize);
    }
  
  /* Restore nrpos */
  if (sblock.fs_postblformat == FS_42POSTBLFMT)
    sblock.fs_nrpos = savednrpos;
  
  if (bcmp (cstotal, sblock.fs_cstotal, sizeof (struct csum)))
    {
      pwarn ("TOTAL FREE BLK COUNTS WRONG IN SUPERBLOCK", c);
      if (preen || reply ("FIX"))
	{
	  if (preen)
	    printf (" (FIXED)");
	  bcopy (&cstotal, sblock.fs_cstotal, sizeof (struct csum));
	  sblock.fs_ronly = 0;
	  sblock.fs_fmod = 0;
	  writesb = 1;
	}
    }

  if (writesb)
    writeblock (SBLOCK, &sblock, SBSIZE);
  if (writecsum)
    writeblock (fsbtodb (&sblock, sblock.fs_csaddr), csumbuf, 
		fragroundup (sizeof (struct csum) * sblock.fs_ncg));
}
