/*-------------------------------------------------------------------------
 * Copyright (C) 1997	National Center for Supercomputing Applications.
 *                      All rights reserved.
 *
 *-------------------------------------------------------------------------
 *
 * Created:		H5Ostab.c
 * 			Aug  6 1997
 * 			Robb Matzke <matzke@llnl.gov>
 *
 * Purpose:		Symbol table messages.
 *
 * Modifications:	
 *
 *-------------------------------------------------------------------------
 */
#include <H5private.h>
#include <H5Eprivate.h>
#include <H5Gprivate.h>
#include <H5MMprivate.h>
#include <H5Oprivate.h>

#define PABLO_MASK	H5O_stab_mask

/* PRIVATE PROTOTYPES */
static void *H5O_stab_decode (H5F_t *f, size_t raw_size, const uint8 *p);
static herr_t H5O_stab_encode (H5F_t *f, size_t size, uint8 *p,
			       const void *_mesg);
static void *H5O_stab_copy (const void *_mesg, void *_dest);
static size_t H5O_stab_size (H5F_t *f, const void *_mesg);
static herr_t H5O_stab_debug (H5F_t *f, const void *_mesg,
			      FILE *stream, intn indent, intn fwidth);

/* This message derives from H5O */
const H5O_class_t H5O_STAB[1] = {{
   H5O_STAB_ID,				/*message id number		*/
   "stab",				/*message name for debugging	*/
   sizeof (H5O_stab_t),			/*native message size		*/
   H5O_stab_decode,			/*decode message		*/
   H5O_stab_encode,			/*encode message		*/
   H5O_stab_copy,			/*copy the native value		*/
   H5O_stab_size,			/*size of symbol table entry	*/
   NULL,				/*default reset method		*/
   H5O_stab_debug,			/*debug the message		*/
}};

/* Interface initialization */
static hbool_t interface_initialize_g = FALSE;
#define INTERFACE_INIT	NULL


/*-------------------------------------------------------------------------
 * Function:	H5O_stab_decode
 *
 * Purpose:	Decode a symbol table message and return a pointer to
 *		a new one created with malloc().
 *
 * Return:	Success:	Ptr to new message in native order.
 *
 *		Failure:	NULL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Aug  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static void *
H5O_stab_decode (H5F_t *f, size_t raw_size, const uint8 *p)
{
   H5O_stab_t	*stab;
   
   FUNC_ENTER (H5O_stab_decode, NULL);

   /* check args */
   assert (f);
   assert (raw_size == 2*H5F_SIZEOF_ADDR(f));
   assert (p);

   /* decode */
   stab = H5MM_xcalloc (1, sizeof(H5O_stab_t));
   H5F_addr_decode (f, &p, &(stab->btree_addr));
   H5F_addr_decode (f, &p, &(stab->heap_addr));

   FUNC_LEAVE (stab);
}


/*-------------------------------------------------------------------------
 * Function:	H5O_stab_encode
 *
 * Purpose:	Encodes a symbol table message.
 *
 * Return:	Success:	SUCCEED
 *
 *		Failure:	FAIL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Aug  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_stab_encode (H5F_t *f, size_t raw_size, uint8 *p, const void *_mesg)
{
   const H5O_stab_t	*stab = (const H5O_stab_t *)_mesg;

   FUNC_ENTER (H5O_stab_encode, FAIL);

   /* check args */
   assert (f);
   assert (raw_size == 2 * H5F_SIZEOF_ADDR(f));
   assert (p);
   assert (stab);

   /* encode */
   H5F_addr_encode (f, &p, &(stab->btree_addr));
   H5F_addr_encode (f, &p, &(stab->heap_addr));

   FUNC_LEAVE (SUCCEED);
}


/*-------------------------------------------------------------------------
 * Function:	H5O_stab_fast
 *
 * Purpose:	Initializes a new message struct with info from the cache of
 *		a symbol table entry.
 *
 * Return:	Success:	Ptr to message struct, allocated if none
 *				supplied.
 *
 *		Failure:	NULL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Aug  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
void *
H5O_stab_fast (const H5G_cache_t *cache, const H5O_class_t *type, void *_mesg)
{
   H5O_stab_t	*stab = NULL;
   
   FUNC_ENTER (H5O_stab_fast, NULL);

   /* check args */
   assert (cache);
   assert (type);

   if (H5O_STAB==type) {
      if (_mesg) stab = (H5O_stab_t *)_mesg;
      else stab = H5MM_xcalloc (1, sizeof(H5O_stab_t));
      stab->btree_addr = cache->stab.btree_addr;
      stab->heap_addr = cache->stab.heap_addr;
   }

   FUNC_LEAVE (stab);
}


/*-------------------------------------------------------------------------
 * Function:	H5O_stab_copy
 *
 * Purpose:	Copies a message from _MESG to _DEST, allocating _DEST if
 *		necessary.
 *
 * Return:	Success:	Ptr to _DEST
 *
 *		Failure:	NULL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Aug  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static void *
H5O_stab_copy (const void *_mesg, void *_dest)
{
   const H5O_stab_t	*stab = (const H5O_stab_t *)_mesg;
   H5O_stab_t		*dest = (H5O_stab_t *)_dest;
   
   FUNC_ENTER (H5O_stab_copy, NULL);

   /* check args */
   assert (stab);
   if (!dest) dest = H5MM_xcalloc (1, sizeof(H5O_stab_t));

   /* copy */
   *dest = *stab;

   FUNC_LEAVE ((void*)dest);
}


/*-------------------------------------------------------------------------
 * Function:	H5O_stab_size
 *
 * Purpose:	Returns the size of the raw message in bytes not counting
 *		the message type or size fields, but only the data fields.
 *		This function doesn't take into account alignment.
 *
 * Return:	Success:	Message data size in bytes without alignment.
 *
 *		Failure:	FAIL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Aug  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static size_t
H5O_stab_size (H5F_t *f, const void *_mesg)
{
   FUNC_ENTER (H5O_stab_size, FAIL);
   FUNC_LEAVE (2 * H5F_SIZEOF_ADDR(f));
}


/*-------------------------------------------------------------------------
 * Function:	H5O_stab_debug
 *
 * Purpose:	Prints debugging info for a symbol table message.
 *
 * Return:	Success:	SUCCEED
 *
 *		Failure:	FAIL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Aug  6 1997
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5O_stab_debug (H5F_t *f, const void *_mesg, FILE *stream, intn indent,
		intn fwidth)
{
   const H5O_stab_t	*stab = (const H5O_stab_t *)_mesg;
   
   FUNC_ENTER (H5O_stab_debug, FAIL);

   /* check args */
   assert (f);
   assert (stab);
   assert (stream);
   assert (indent>=0);
   assert (fwidth>=0);

   fprintf (stream, "%*s%-*s ", indent, "", fwidth,
	    "B-tree address:");
   H5F_addr_print (stream, &(stab->btree_addr));
   fprintf (stream, "\n");
   
   fprintf (stream, "%*s%-*s ", indent, "", fwidth,
	    "Name heap address:");
   H5F_addr_print (stream, &(stab->heap_addr));
   fprintf (stream, "\n");
   
   FUNC_LEAVE (SUCCEED);
}
	    
