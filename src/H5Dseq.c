/*
 * Copyright (C) 2000-2001 NCSA
 *		           All rights reserved.
 *
 * Programmer: 	Quincey Koziol <koziol@ncsa.uiuc.edu>
 *	       	Thursday, September 28, 2000
 *
 * Purpose:	Provides I/O facilities for sequences of bytes stored with various 
 *      layout policies.  These routines are similar to the H5Farray.c routines,
 *      these deal in terms of byte offsets and lengths, not coordinates and
 *      hyperslab sizes.
 *
 */

#define H5F_PACKAGE		/*suppress error about including H5Fpkg	  */

#include "H5private.h"
#include "H5Dprivate.h"
#include "H5Eprivate.h"
#include "H5Fpkg.h"
#include "H5FDprivate.h"	/*file driver				  */
#include "H5Iprivate.h"
#include "H5MFprivate.h"
#include "H5MMprivate.h"	/*memory management			  */
#include "H5Oprivate.h"
#include "H5Pprivate.h"
#include "H5Vprivate.h"

/* MPIO & MPIPOSIX driver functions are needed for some special checks */
#include "H5FDmpio.h"
#include "H5FDmpiposix.h"

/* Interface initialization */
#define PABLO_MASK	H5Fseq_mask
#define INTERFACE_INIT	NULL
static int interface_initialize_g = 0;


/*-------------------------------------------------------------------------
 * Function:	H5F_seq_read
 *
 * Purpose:	Reads a sequence of bytes from a file dataset into a buffer in
 *      in memory.  The data is read from file F and the array's size and
 *      storage information is in LAYOUT.  External files are described
 *      according to the external file list, EFL.  The sequence offset is 
 *      DSET_OFFSET in the dataset (offsets are in terms of bytes) and the 
 *      size of the hyperslab is SEQ_LEN. The total size of the file array 
 *      is implied in the LAYOUT argument.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Thursday, September 28, 2000
 *
 * Modifications:
 *              Re-written to use new vector I/O call - QAK, 7/7/01
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_seq_read(H5F_t *f, hid_t dxpl_id, const H5O_layout_t *layout,
    H5P_genplist_t *dc_plist, const H5O_efl_t *efl,
    const H5S_t *file_space, size_t elmt_size,
    size_t seq_len, hsize_t dset_offset, void *buf/*out*/)
{
    herr_t      ret_value=SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI(H5F_seq_read, FAIL);

    /* Check args */
    assert(f);
    assert(layout);
    assert(efl);
    assert(buf);
    assert(TRUE==H5P_isa_class(dxpl_id,H5P_DATASET_XFER));

    if (H5F_seq_readv(f, dxpl_id, layout, dc_plist, efl, file_space, elmt_size, 1, &seq_len, &dset_offset, buf)<0)
        HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "vector read failed");

done:
    FUNC_LEAVE(ret_value);
}   /* H5F_seq_read() */


/*-------------------------------------------------------------------------
 * Function:	H5F_seq_write
 *
 * Purpose:	Writes a sequence of bytes to a file dataset from a buffer in
 *      in memory.  The data is written to file F and the array's size and
 *      storage information is in LAYOUT.  External files are described
 *      according to the external file list, EFL.  The sequence offset is 
 *      DSET_OFFSET in the dataset (offsets are in terms of bytes) and the 
 *      size of the hyperslab is SEQ_LEN. The total size of the file array 
 *      is implied in the LAYOUT argument.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Monday, October 9, 2000
 *
 * Modifications:
 *              Re-written to use new vector I/O routine - QAK, 7/7/01
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_seq_write(H5F_t *f, hid_t dxpl_id, H5O_layout_t *layout,
    H5P_genplist_t *dc_plist, const H5O_efl_t *efl,
    const H5S_t *file_space, size_t elmt_size,
    size_t seq_len, hsize_t dset_offset, const void *buf)
{
    herr_t      ret_value=SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI(H5F_seq_write, FAIL);

    /* Check args */
    assert(f);
    assert(layout);
    assert(efl);
    assert(buf);
    assert(TRUE==H5P_isa_class(dxpl_id,H5P_DATASET_XFER));

    if (H5F_seq_writev(f, dxpl_id, layout, dc_plist, efl, file_space, elmt_size, 1, &seq_len, &dset_offset, buf)<0)
        HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "vector write failed");

done:
    FUNC_LEAVE(ret_value);
}   /* H5F_seq_write() */


/*-------------------------------------------------------------------------
 * Function:	H5F_seq_readv
 *
 * Purpose:	Reads in a vector of byte sequences from a file dataset into a
 *      buffer in in memory.  The data is read from file F and the array's size
 *      and storage information is in LAYOUT.  External files are described
 *      according to the external file list, EFL.  The vector of byte sequences
 *      offsets is in the DSET_OFFSET array into the dataset (offsets are in
 *      terms of bytes) and the size of each sequence is in the SEQ_LEN array.
 *      The total size of the file array is implied in the LAYOUT argument.
 *      Bytes read into BUF are sequentially stored in the buffer, each sequence
 *      from the vector stored directly after the previous.  The number of
 *      sequences is NSEQ.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Wednesday, May 1, 2001
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_seq_readv(H5F_t *f, hid_t dxpl_id, const H5O_layout_t *layout,
        H5P_genplist_t *dc_plist, const H5O_efl_t *efl,
        const H5S_t *file_space, size_t elmt_size,
        size_t nseq, size_t seq_len_arr[], hsize_t dset_offset_arr[],
        void *_buf/*out*/)
{
    unsigned char *real_buf=(unsigned char *)_buf;   /* Local pointer to buffer to fill */
    unsigned char *buf;                         /* Local pointer to buffer to fill */
    hsize_t dset_offset;                /* Offset in dataset */
    hsize_t seq_len;                    /* Number of bytes to read */
    hsize_t	dset_dims[H5O_LAYOUT_NDIMS];	/* dataspace dimensions */
    hssize_t    mem_offset[H5O_LAYOUT_NDIMS];	/* offset of hyperslab in memory buffer */
    hssize_t    coords[H5O_LAYOUT_NDIMS];	/* offset of hyperslab in dataspace */
    hsize_t	hslab_size[H5O_LAYOUT_NDIMS];	/* hyperslab size in dataspace*/
    hsize_t     down_size[H5O_LAYOUT_NDIMS];    /* Cumulative yperslab sizes (in elements) */
    hsize_t     acc;    /* Accumulator for hyperslab sizes (in elements) */
    int ndims;
    hsize_t	max_data;    			/*bytes in dataset	*/
    haddr_t	addr=0;				/*address in file	*/
    unsigned	u;				/*counters		*/
    size_t      v;                              /*counters              */
    int	i,j;				/*counters		*/
#ifdef H5_HAVE_PARALLEL
    H5FD_mpio_xfer_t xfer_mode=H5FD_MPIO_INDEPENDENT;
    H5P_genplist_t *plist=NULL;                 /* Property list */
#endif /* H5_HAVE_PARALLEL */
    herr_t      ret_value = SUCCEED;            /* Return value */
   
    FUNC_ENTER_NOAPI(H5F_seq_readv, FAIL);

    /* Check args */
    assert(f);
    assert(layout);
    assert(efl);
    assert(real_buf);
    /* Make certain we have the correct type of property list */
    assert(TRUE==H5P_isa_class(dxpl_id,H5P_DATASET_XFER));

#ifdef H5_HAVE_PARALLEL
    {
	H5FD_mpio_dxpl_t *dx;
        hid_t driver_id;            /* VFL driver ID */

	/* Get the transfer mode for MPIO transfers */
        if(IS_H5FD_MPIO(f)) {
            /* Get the plist structure */
            if(NULL == (plist = H5I_object(dxpl_id)))
                HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");

            /* Get the driver ID */
            if(H5P_get(plist, H5D_XFER_VFL_ID_NAME, &driver_id)<0)
                HGOTO_ERROR (H5E_PLIST, H5E_CANTGET, FAIL, "Can't retrieve VFL driver ID");

            /* Check if we are using the MPIO driver (for the DXPL) */
            if(H5FD_MPIO==driver_id) {
                /* Get the driver information */
                if(H5P_get(plist, H5D_XFER_VFL_INFO_NAME, &dx)<0)
                    HGOTO_ERROR (H5E_PLIST, H5E_CANTGET, FAIL, "Can't retrieve VFL driver info");

                /* Check if we are not using independent I/O */
                if(H5FD_MPIO_INDEPENDENT!=dx->xfer_mode)
                    xfer_mode = dx->xfer_mode;
            } /* end if */
        } /* end if */
    }

    /* Collective MPIO access is unsupported for non-contiguous datasets */
    if (H5D_CHUNKED==layout->type && H5FD_MPIO_COLLECTIVE==xfer_mode)
        HGOTO_ERROR (H5E_DATASET, H5E_READERROR, FAIL, "collective access on non-contiguous datasets not supported yet");
#endif /* H5_HAVE_PARALLEL */

    switch (layout->type) {
        case H5D_CONTIGUOUS:
            /* Read directly from file if the dataset is in an external file */
            if (efl->nused>0) {
                /* Iterate through the sequence vectors */
                for(v=0; v<nseq; v++) {
#ifdef H5_HAVE_PARALLEL
                    if (H5FD_MPIO_COLLECTIVE==xfer_mode) {
                        /*
                         * Currently supports same number of collective access. Need to
                         * be changed LATER to combine all reads into one collective MPIO
                         * call.
                         */
                        unsigned long max, min, temp;

                        temp = seq_len_arr[v];
                        assert(temp==seq_len_arr[v]);	/* verify no overflow */
                        MPI_Allreduce(&temp, &max, 1, MPI_UNSIGNED_LONG, MPI_MAX,
                              H5FD_mpio_communicator(f->shared->lf));
                        MPI_Allreduce(&temp, &min, 1, MPI_UNSIGNED_LONG, MPI_MIN,
                              H5FD_mpio_communicator(f->shared->lf));
#ifdef AKC
                        printf("seq_len=%lu, min=%lu, max=%lu\n", temp, min, max);
#endif
                        if (max != min)
                            HGOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL,
                              "collective access with unequal number of blocks not supported yet");
                    }
#endif /* H5_HAVE_PARALLEL */
                    /* Note: We can't use data sieve buffers for datasets in external files
                     *  because the 'addr' of all external files is set to 0 (above) and
                     *  all datasets in external files would alias to the same set of
                     *  file offsets, totally mixing up the data sieve buffer information. -QAK
                     */
                    H5_CHECK_OVERFLOW(dset_offset_arr[v],hsize_t,haddr_t);
                    if (H5O_efl_read(f, efl, (haddr_t)dset_offset_arr[v], seq_len_arr[v], real_buf)<0)
                        HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "external data read failed");

                    /* Increment offset in buffer */
                    real_buf += seq_len_arr[v];
                } /* end for */
            } else {
                /* Compute the size of the dataset in bytes */
                for(u=1, max_data=layout->dim[0]; u<layout->ndims; u++)
                    max_data *= layout->dim[u];

                /* Pass along the vector of sequences to read */
                if (H5F_contig_readv(f, max_data, H5FD_MEM_DRAW, layout->addr, nseq, seq_len_arr, dset_offset_arr, dxpl_id, real_buf)<0)
                    HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "block read failed");
            } /* end else */
            break;

        case H5D_CHUNKED:
            /*
             * This method is unable to access external raw data files 
             */
            if (efl->nused>0)
                HGOTO_ERROR(H5E_IO, H5E_UNSUPPORTED, FAIL, "chunking and external files are mutually exclusive");

            /* Compute the file offset coordinates and hyperslab size */
            if((ndims=H5S_get_simple_extent_dims(file_space,dset_dims,NULL))<0)
                HGOTO_ERROR(H5E_IO, H5E_UNSUPPORTED, FAIL, "unable to retrieve dataspace dimensions");
            
            /* Build the array of cumulative hyperslab sizes */
            /* (And set the memory offset to zero) */
            for(acc=1, i=(ndims-1); i>=0; i--) {
                mem_offset[i]=0;
                down_size[i]=acc;
                acc*=dset_dims[i];
            } /* end for */
            mem_offset[ndims]=0;

            /* Brute-force, stupid way to implement the vectors, but too complex to do other ways... */
            for(v=0; v<nseq; v++) {
                dset_offset=dset_offset_arr[v];
                seq_len=seq_len_arr[v];
                buf=real_buf;

                {
                    /* Set location in dataset from the dset_offset */
                    addr=dset_offset;

                    /* Convert the bytes into elements */
                    seq_len/=elmt_size;
                    addr/=elmt_size;

                    /* Compute the hyperslab offset from the address given */
                    for(i=ndims-1; i>=0; i--) {
                        coords[i]=addr%dset_dims[i];
                        addr/=dset_dims[i];
                    } /* end for */
                    coords[ndims]=0;   /* No offset for element info */

                    /*
                     * Peel off initial partial hyperslabs until we've got a hyperslab which starts
                     *      at coord[n]==0 for dimensions 1->(ndims-1)  (i.e. starting at coordinate
                     *      zero for all dimensions except the slowest changing one
                     */
                    for(i=ndims-1; i>0 && seq_len>=down_size[i]; i--) {
                        hsize_t partial_size;       /* Size of the partial hyperslab in bytes */

                        /* Check if we have a partial hyperslab in this lower dimension */
                        if(coords[i]>0) {
                            /* Reset the partial hyperslab size */
                            partial_size=1;

                            /* Build the partial hyperslab information */
                            for(j=0; j<ndims; j++) {
                                if(i==j)
                                    hslab_size[j]=MIN(seq_len/down_size[i],dset_dims[i]-coords[i]);
                                else
                                    if(j>i)
                                        hslab_size[j]=dset_dims[j];
                                    else
                                        hslab_size[j]=1;
                                partial_size*=hslab_size[j];
                            } /* end for */
                            hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                            /* Read in the partial hyperslab */
                            if (H5F_istore_read(f, dxpl_id, layout, dc_plist,
                                    hslab_size, mem_offset, coords, hslab_size,
                                    buf)<0)
                                HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "chunked read failed");

                            /* Increment the buffer offset */
                            buf=(unsigned char *)buf+(partial_size*elmt_size);

                            /* Decrement the length of the sequence to read */
                            seq_len-=partial_size;

                            /* Correct the coords array */
                            coords[i]=0;
                            coords[i-1]++;

                            /* Carry the coord array correction up the array, if the dimension is finished */
                            while(i>0 && coords[i-1]==(hssize_t)dset_dims[i-1]) {
                                i--;
                                coords[i]=0;
                                if(i>0) {
                                    coords[i-1]++;
                                    assert(coords[i-1]<=(hssize_t)dset_dims[i-1]);
                                } /* end if */
                            } /* end while */
                        } /* end if */
                    } /* end for */

                    /* Check if there is more than just a partial hyperslab to read */
                    if(seq_len>=down_size[0]) {
                        hsize_t tmp_seq_len;    /* Temp. size of the sequence in elements */
                        hsize_t full_size;      /* Size of the full hyperslab in bytes */

                        /* Get the sequence length for computing the hyperslab sizes */
                        tmp_seq_len=seq_len;

                        /* Reset the size of the hyperslab read in */
                        full_size=1;

                        /* Compute the hyperslab size from the length given */
                        for(i=ndims-1; i>=0; i--) {
                            /* Check if the hyperslab is wider than the width of the dimension */
                            if(tmp_seq_len>dset_dims[i]) {
                                assert(0==coords[i]);
                                hslab_size[i]=dset_dims[i];
                            } /* end if */
                            else 
                                hslab_size[i]=tmp_seq_len;

                            /* compute the number of elements read in */
                            full_size*=hslab_size[i];

                            /* Fold the length into the length in the next highest dimension */
                            tmp_seq_len/=dset_dims[i];

                            /* Make certain the hyperslab sizes don't go less than 1 for dimensions less than 0*/
                            assert(tmp_seq_len>=1 || i==0);
                        } /* end for */
                        hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                        /* Read the full hyperslab in */
                        if (H5F_istore_read(f, dxpl_id, layout, dc_plist,
                                hslab_size, mem_offset, coords, hslab_size, buf)<0)
                            HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "chunked read failed");

                        /* Increment the buffer offset */
                        buf=(unsigned char *)buf+(full_size*elmt_size);

                        /* Decrement the sequence length left */
                        seq_len-=full_size;

                        /* Increment coordinate of slowest changing dimension */
                        coords[0]+=hslab_size[0];

                    } /* end if */

                    /*
                     * Peel off final partial hyperslabs until we've finished reading all the data
                     */
                    if(seq_len>0) {
                        hsize_t partial_size;       /* Size of the partial hyperslab in bytes */

                        /*
                         * Peel off remaining partial hyperslabs, from the next-slowest dimension
                         *  on down to the next-to-fastest changing dimension
                         */
                        for(i=1; i<(ndims-1); i++) {
                            /* Check if there are enough elements to read in a row in this dimension */
                            if(seq_len>=down_size[i]) {
                                /* Reset the partial hyperslab size */
                                partial_size=1;

                                /* Build the partial hyperslab information */
                                for(j=0; j<ndims; j++) {
                                    if(j<i)
                                        hslab_size[j]=1;
                                    else
                                        if(j==i)
                                            hslab_size[j]=seq_len/down_size[j];
                                        else
                                            hslab_size[j]=dset_dims[j];

                                    partial_size*=hslab_size[j];
                                } /* end for */
                                hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                                /* Read in the partial hyperslab */
                                if (H5F_istore_read(f, dxpl_id, layout, dc_plist,
                                        hslab_size, mem_offset, coords, hslab_size, buf)<0)
                                    HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "chunked read failed");

                                /* Increment the buffer offset */
                                buf=(unsigned char *)buf+(partial_size*elmt_size);

                                /* Decrement the length of the sequence to read */
                                seq_len-=partial_size;

                                /* Correct the coords array */
                                coords[i]=hslab_size[i];
                            } /* end if */
                        } /* end for */

                        /* Handle fastest changing dimension if there are any elements left */
                        if(seq_len>0) {
                            assert(seq_len<dset_dims[ndims-1]);

                            /* Reset the partial hyperslab size */
                            partial_size=1;

                            /* Build the partial hyperslab information */
                            for(j=0; j<ndims; j++) {
                                if(j==(ndims-1))
                                    hslab_size[j]=seq_len;
                                else
                                    hslab_size[j]=1;

                                partial_size*=hslab_size[j];
                            } /* end for */
                            hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                            /* Read in the partial hyperslab */
                            if (H5F_istore_read(f, dxpl_id, layout, dc_plist,
                                    hslab_size, mem_offset, coords, hslab_size, buf)<0)
                                HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "chunked read failed");

                            /* Double-check the amount read in */
                            assert(seq_len==partial_size);
                        } /* end if */
                    } /* end if */
                }
                /* Increment offset in buffer */
                real_buf += seq_len_arr[v];
            } /* end for */

            break;

        case H5D_COMPACT:

            /* Pass along the vector of sequences to read */
            if (H5F_compact_readv(f, layout, nseq, seq_len_arr, dset_offset_arr, dxpl_id, real_buf)<0)
                HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "block read failed");
                                                        
            break;
        default:
            assert("not implemented yet" && 0);
            HGOTO_ERROR(H5E_IO, H5E_UNSUPPORTED, FAIL, "unsupported storage layout");
    }   /* end switch() */

done:
    FUNC_LEAVE(ret_value);
}   /* H5F_seq_readv() */


/*-------------------------------------------------------------------------
 * Function:	H5F_seq_writev
 *
 * Purpose:	Writes a vector of byte sequences from a buffer in memory into
 *      a file dataset.  The data is written to file F and the array's size
 *      and storage information is in LAYOUT.  External files are described
 *      according to the external file list, EFL.  The vector of byte sequences
 *      offsets is in the DSET_OFFSET array into the dataset (offsets are in
 *      terms of bytes) and the size of each sequence is in the SEQ_LEN array.
 *      The total size of the file array is implied in the LAYOUT argument.
 *      Bytes written from BUF are sequentially stored in the buffer, each sequence
 *      from the vector stored directly after the previous.  The number of
 *      sequences is NSEQ.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Friday, July 6, 2001
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_seq_writev(H5F_t *f, hid_t dxpl_id, H5O_layout_t *layout,
        H5P_genplist_t *dc_plist, const H5O_efl_t *efl,
        const H5S_t *file_space, size_t elmt_size,
        size_t nseq, size_t seq_len_arr[], hsize_t dset_offset_arr[],
        const void *_buf)
{
    const unsigned char *real_buf=(const unsigned char *)_buf;   /* Local pointer to buffer to fill */
    const unsigned char *buf;                         /* Local pointer to buffer to fill */
    hsize_t dset_offset;                /* Offset in dataset */
    hsize_t seq_len;                    /* Number of bytes to read */
    hsize_t	dset_dims[H5O_LAYOUT_NDIMS];	/* dataspace dimensions */
    hssize_t    mem_offset[H5O_LAYOUT_NDIMS];	/* offset of hyperslab in memory buffer */
    hssize_t    coords[H5O_LAYOUT_NDIMS];	/* offset of hyperslab in dataspace */
    hsize_t	hslab_size[H5O_LAYOUT_NDIMS];	/* hyperslab size in dataspace*/
    hsize_t     down_size[H5O_LAYOUT_NDIMS];    /* Cumulative hyperslab sizes (in elements) */
    hsize_t     acc;    /* Accumulator for hyperslab sizes (in elements) */
    int ndims;
    hsize_t	max_data;    			/*bytes in dataset	*/
    haddr_t	addr;				/*address in file	*/
    unsigned	u;				/*counters		*/
    size_t      v;                              /*counters              */
    int	i,j;				/*counters		*/
#ifdef H5_HAVE_PARALLEL
    H5FD_mpio_xfer_t xfer_mode=H5FD_MPIO_INDEPENDENT;
    H5P_genplist_t *plist=NULL;                 /* Property list */
#endif /* H5_HAVE_PARALLEL */
    herr_t      ret_value = SUCCEED;            /* Return value */
   
    FUNC_ENTER_NOAPI(H5F_seq_writev, FAIL);

    /* Check args */
    assert(f);
    assert(layout);
    assert(efl);
    assert(real_buf);
    /* Make certain we have the correct type of property list */
    assert(TRUE==H5P_isa_class(dxpl_id,H5P_DATASET_XFER));

#ifdef H5_HAVE_PARALLEL
    {
	H5FD_mpio_dxpl_t *dx;
        hid_t driver_id;            /* VFL driver ID */

	/* Get the transfer mode for MPIO transfers */
        if(IS_H5FD_MPIO(f)) {
            /* Get the plist structure */
            if(NULL == (plist = H5I_object(dxpl_id)))
                HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");

            /* Get the driver ID */
            if(H5P_get(plist, H5D_XFER_VFL_ID_NAME, &driver_id)<0)
                HGOTO_ERROR (H5E_PLIST, H5E_CANTGET, FAIL, "Can't retrieve VFL driver ID");

            /* Check if we are using the MPIO driver (for the DXPL) */
            if(H5FD_MPIO==driver_id) {
                /* Get the driver information */
                if(H5P_get(plist, H5D_XFER_VFL_INFO_NAME, &dx)<0)
                    HGOTO_ERROR (H5E_PLIST, H5E_CANTGET, FAIL, "Can't retrieve VFL driver info");

                /* Check if we are not using independent I/O */
                if(H5FD_MPIO_INDEPENDENT!=dx->xfer_mode)
                    xfer_mode = dx->xfer_mode;
            } /* end if */
        } /* end if */
    }

    /* Collective MPIO access is unsupported for non-contiguous datasets */
    if (H5D_CHUNKED==layout->type && H5FD_MPIO_COLLECTIVE==xfer_mode)
        HGOTO_ERROR (H5E_DATASET, H5E_WRITEERROR, FAIL, "collective access on chunked datasets not supported yet");
#endif /* H5_HAVE_PARALLEL */

    switch (layout->type) {
        case H5D_CONTIGUOUS:
            /* Write directly to file if the dataset is in an external file */
            if (efl->nused>0) {
                /* Iterate through the sequence vectors */
                for(v=0; v<nseq; v++) {
#ifdef H5_HAVE_PARALLEL
                    if (H5FD_MPIO_COLLECTIVE==xfer_mode) {
                        /*
                         * Currently supports same number of collective access. Need to
                         * be changed LATER to combine all reads into one collective MPIO
                         * call.
                         */
                        unsigned long max, min, temp;

                        temp = seq_len_arr[v];
                        assert(temp==seq_len_arr[v]);	/* verify no overflow */
                        MPI_Allreduce(&temp, &max, 1, MPI_UNSIGNED_LONG, MPI_MAX,
                              H5FD_mpio_communicator(f->shared->lf));
                        MPI_Allreduce(&temp, &min, 1, MPI_UNSIGNED_LONG, MPI_MIN,
                              H5FD_mpio_communicator(f->shared->lf));
#ifdef AKC
                        printf("seq_len=%lu, min=%lu, max=%lu\n", temp, min, max);
#endif
                        if (max != min)
                            HGOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "collective access with unequal number of blocks not supported yet");
                    }
#endif /* H5_HAVE_PARALLEL */
                    /* Note: We can't use data sieve buffers for datasets in external files
                     *  because the 'addr' of all external files is set to 0 (above) and
                     *  all datasets in external files would alias to the same set of
                     *  file offsets, totally mixing up the data sieve buffer information. -QAK
                     */
                    H5_CHECK_OVERFLOW(dset_offset_arr[v],hsize_t,haddr_t);
                    if (H5O_efl_write(f, efl, (haddr_t)dset_offset_arr[v], seq_len_arr[v], real_buf)<0)
                        HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "external data write failed");

                    /* Increment offset in buffer */
                    real_buf += seq_len_arr[v];
                } /* end for */
            } else {
                /* Compute the size of the dataset in bytes */
                for(u=1, max_data=layout->dim[0]; u<layout->ndims; u++)
                    max_data *= layout->dim[u];

                /* Pass along the vector of sequences to write */
                if (H5F_contig_writev(f, max_data, H5FD_MEM_DRAW, layout->addr, nseq, seq_len_arr, dset_offset_arr, dxpl_id, real_buf)<0)
                    HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "block write failed");
            } /* end else */
            break;

        case H5D_CHUNKED:
            /*
             * This method is unable to access external raw data files 
             */
            if (efl->nused>0)
                HGOTO_ERROR(H5E_IO, H5E_UNSUPPORTED, FAIL, "chunking and external files are mutually exclusive");

            /* Compute the file offset coordinates and hyperslab size */
            if((ndims=H5S_get_simple_extent_dims(file_space,dset_dims,NULL))<0)
                HGOTO_ERROR(H5E_IO, H5E_UNSUPPORTED, FAIL, "unable to retrieve dataspace dimensions");

            /* Build the array of cumulative hyperslab sizes */
            /* (And set the memory offset to zero) */
            for(acc=1, i=(ndims-1); i>=0; i--) {
                mem_offset[i]=0;
                down_size[i]=acc;
                acc*=dset_dims[i];
            } /* end for */
            mem_offset[ndims]=0;

            /* Brute-force, stupid way to implement the vectors, but too complex to do other ways... */
            for(v=0; v<nseq; v++) {
                dset_offset=dset_offset_arr[v];
                seq_len=seq_len_arr[v];
                buf=real_buf;

                {
                    /* Set location in dataset from the dset_offset */
                    addr=dset_offset;

                    /* Convert the bytes into elements */
                    seq_len/=elmt_size;
                    addr/=elmt_size;

                    /* Compute the hyperslab offset from the address given */
                    for(i=ndims-1; i>=0; i--) {
                        coords[i]=addr%dset_dims[i];
                        addr/=dset_dims[i];
                    } /* end for */
                    coords[ndims]=0;   /* No offset for element info */

                    /*
                     * Peel off initial partial hyperslabs until we've got a hyperslab which starts
                     *      at coord[n]==0 for dimensions 1->(ndims-1)  (i.e. starting at coordinate
                     *      zero for all dimensions except the slowest changing one
                     */
                    for(i=ndims-1; i>0 && seq_len>=down_size[i]; i--) {
                        hsize_t partial_size;       /* Size of the partial hyperslab in bytes */

                        /* Check if we have a partial hyperslab in this lower dimension */
                        if(coords[i]>0) {
                            /* Reset the partial hyperslab size */
                            partial_size=1;

                            /* Build the partial hyperslab information */
                            for(j=0; j<ndims; j++) {
                                if(i==j)
                                    hslab_size[j]=MIN(seq_len/down_size[i],dset_dims[i]-coords[i]);
                                else
                                    if(j>i)
                                        hslab_size[j]=dset_dims[j];
                                    else
                                        hslab_size[j]=1;
                                partial_size*=hslab_size[j];
                            } /* end for */
                            hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                            /* Write out the partial hyperslab */
                            if (H5F_istore_write(f, dxpl_id, layout, dc_plist,
                                    hslab_size, mem_offset,coords, hslab_size, buf)<0)
                                HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "chunked write failed");

                            /* Increment the buffer offset */
                            buf=(const unsigned char *)buf+(partial_size*elmt_size);

                            /* Decrement the length of the sequence to read */
                            seq_len-=partial_size;

                            /* Correct the coords array */
                            coords[i]=0;
                            coords[i-1]++;

                            /* Carry the coord array correction up the array, if the dimension is finished */
                            while(i>0 && coords[i-1]==(hssize_t)dset_dims[i-1]) {
                                i--;
                                coords[i]=0;
                                if(i>0) {
                                    coords[i-1]++;
                                    assert(coords[i-1]<=(hssize_t)dset_dims[i-1]);
                                } /* end if */
                            } /* end while */
                        } /* end if */
                    } /* end for */

                    /* Check if there is more than just a partial hyperslab to read */
                    if(seq_len>=down_size[0]) {
                        hsize_t tmp_seq_len;    /* Temp. size of the sequence in elements */
                        hsize_t full_size;      /* Size of the full hyperslab in bytes */

                        /* Get the sequence length for computing the hyperslab sizes */
                        tmp_seq_len=seq_len;

                        /* Reset the size of the hyperslab read in */
                        full_size=1;

                        /* Compute the hyperslab size from the length given */
                        for(i=ndims-1; i>=0; i--) {
                            /* Check if the hyperslab is wider than the width of the dimension */
                            if(tmp_seq_len>dset_dims[i]) {
                                assert(0==coords[i]);
                                hslab_size[i]=dset_dims[i];
                            } /* end if */
                            else 
                                hslab_size[i]=tmp_seq_len;

                            /* compute the number of elements read in */
                            full_size*=hslab_size[i];

                            /* Fold the length into the length in the next highest dimension */
                            tmp_seq_len/=dset_dims[i];

                            /* Make certain the hyperslab sizes don't go less than 1 for dimensions less than 0*/
                            assert(tmp_seq_len>=1 || i==0);
                        } /* end for */
                        hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                        /* Write the full hyperslab in */
                        if (H5F_istore_write(f, dxpl_id, layout, dc_plist,
                                hslab_size, mem_offset, coords, hslab_size, buf)<0)
                            HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "chunked write failed");

                        /* Increment the buffer offset */
                        buf=(const unsigned char *)buf+(full_size*elmt_size);

                        /* Decrement the sequence length left */
                        seq_len-=full_size;

                        /* Increment coordinate of slowest changing dimension */
                        coords[0]+=hslab_size[0];

                    } /* end if */

                    /*
                     * Peel off final partial hyperslabs until we've finished reading all the data
                     */
                    if(seq_len>0) {
                        hsize_t partial_size;       /* Size of the partial hyperslab in bytes */

                        /*
                         * Peel off remaining partial hyperslabs, from the next-slowest dimension
                         *  on down to the next-to-fastest changing dimension
                         */
                        for(i=1; i<(ndims-1); i++) {
                            /* Check if there are enough elements to read in a row in this dimension */
                            if(seq_len>=down_size[i]) {
                                /* Reset the partial hyperslab size */
                                partial_size=1;

                                /* Build the partial hyperslab information */
                                for(j=0; j<ndims; j++) {
                                    if(j<i)
                                        hslab_size[j]=1;
                                    else
                                        if(j==i)
                                            hslab_size[j]=seq_len/down_size[j];
                                        else
                                            hslab_size[j]=dset_dims[j];

                                    partial_size*=hslab_size[j];
                                } /* end for */
                                hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                                /* Write out the partial hyperslab */
                                if (H5F_istore_write(f, dxpl_id, layout, dc_plist,
                                        hslab_size, mem_offset, coords, hslab_size, buf)<0)
                                    HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "chunked write failed");

                                /* Increment the buffer offset */
                                buf=(const unsigned char *)buf+(partial_size*elmt_size);

                                /* Decrement the length of the sequence to read */
                                seq_len-=partial_size;

                                /* Correct the coords array */
                                coords[i]=hslab_size[i];
                            } /* end if */
                        } /* end for */

                        /* Handle fastest changing dimension if there are any elements left */
                        if(seq_len>0) {
                            assert(seq_len<dset_dims[ndims-1]);

                            /* Reset the partial hyperslab size */
                            partial_size=1;

                            /* Build the partial hyperslab information */
                            for(j=0; j<ndims; j++) {
                                if(j==(ndims-1))
                                    hslab_size[j]=seq_len;
                                else
                                    hslab_size[j]=1;

                                partial_size*=hslab_size[j];
                            } /* end for */
                            hslab_size[ndims]=elmt_size;   /* basic hyperslab size is the element */

                            /* Write out the final partial hyperslab */
                            if (H5F_istore_write(f, dxpl_id, layout, dc_plist,
                                    hslab_size, mem_offset, coords, hslab_size, buf)<0)
                                HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "chunked write failed");

                            /* Double-check the amount read in */
                            assert(seq_len==partial_size);
                        } /* end if */
                    } /* end if */
                }
                /* Increment offset in buffer */
                real_buf += seq_len_arr[v];
            } /* end for */

            break;

        case H5D_COMPACT:       

            /* Pass along the vector of sequences to write */
            if (H5F_compact_writev(f, layout, nseq, seq_len_arr, dset_offset_arr, dxpl_id, real_buf)<0)
                 HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "block write failed");
                                                        
            break;

        default:
            assert("not implemented yet" && 0);
            HGOTO_ERROR(H5E_IO, H5E_UNSUPPORTED, FAIL, "unsupported storage layout");
    }   /* end switch() */

done:
    FUNC_LEAVE(ret_value);
}   /* H5F_seq_writev() */

