/*
 * Copyright (C) 2001, 2002
 *     National Center for Supercomputing Applications
 *     All rights reserved.
 *
 */

/*
 * Parallel HDF5 Performance Testing Code
 * --------------------------------------
 *
 * Portable code to test performance on the different platforms we support.
 * This is what the report should look like:
 *
 *  nprocs = Max#Procs
 *      IO API = POSIXIO
 *          # Files = 1, # of dsets = 1000, Elements per dset = 37000
 *              Write Results = x MB/s
 *              Read Results = x MB/s
 *          # Files = 1, # of dsets = 3000, Elements per dset = 37000
 *              Write Results = x MB/s
 *              Read Results = x MB/s
 *
 *          . . .
 *
 *      IO API = MPIO
 *          # Files = 1, # of dsets = 1000, Elements per dset = 37000
 *              Write Results = x MB/s
 *              Read Results = x MB/s
 *          # Files = 1, # of dsets = 3000, Elements per dset = 37000
 *              Write Results = x MB/s
 *              Read Results = x MB/s
 *
 *          . . .
 *
 *      IO API = PHDF5
 *          # Files = 1, # of dsets = 1000, Elements per dset = 37000
 *              Write Results = x MB/s
 *              Read Results = x MB/s
 *          # Files = 1, # of dsets = 3000, Elements per dset = 37000
 *              Write Results = x MB/s
 *              Read Results = x MB/s
 *
 *          . . .
 *
 *  nprocs = Max#Procs / 2
 *
 *      . . .
 *
 */

/* system header files */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "hdf5.h"

#ifdef H5_HAVE_PARALLEL

/* library header files */
#include <mpi.h>

/* our header files */
#include "h5tools_utils.h"
#include "pio_perf.h"

/* useful macros */
#define TAB_SPACE           4

#define ONE_KB              1024
#define ONE_MB              (ONE_KB * ONE_KB)
#define ONE_GB              (ONE_MB * ONE_KB)

#define PIO_POSIX           0x10
#define PIO_MPI             0x20
#define PIO_HDF5            0x40

/* report 0.0 in case t is zero too */
#define MB_PER_SEC(bytes,t) (((t)==0.0) ? 0.0 : ((((double)bytes) / ONE_MB) / (t)))

/* global variables */
FILE       *output;             /* output file                          */
int         comm_world_rank_g;  /* my rank in MPI_COMM_RANK             */
int         comm_world_nprocs_g;/* num. of processes of MPI_COMM_WORLD  */
MPI_Info    pio_info_g = MPI_INFO_NULL; /*MPI INFO object to run the PIO*/
MPI_Comm    pio_comm_g;         /* Communicator to run the PIO          */
int         pio_mpi_rank_g;     /* MPI rank of pio_comm_g               */
int         pio_mpi_nprocs_g;   /* Number of processes of pio_comm_g    */
int         pio_debug_level = 0;/* The debug level:
                                 *   0 - Off
                                 *   1 - Minimal
                                 *   2 - Some more
                                 *   3 - Maximal
                                 *   4 - Maximal & then some
                                 */

/* local variables */
static const char  *progname = "pio_perf";

/*
 * Command-line options: The user can specify short or long-named
 * parameters. The long-named ones can be partially spelled. When
 * adding more, make sure that they don't clash with each other.
 */
#if 1
static const char *s_opts = "ha:A:cD:f:P:p:X:x:nd:F:i:o:stT:";
#else
static const char *s_opts = "ha:A:bcD:f:P:p:X:x:nd:F:i:o:stT:";
#endif  /* 1 */
static struct long_options l_opts[] = {
    { "help", no_arg, 'h' },
    { "hel", no_arg, 'h' },
    { "he", no_arg, 'h' },
    { "align", require_arg, 'a' },
    { "alig", require_arg, 'a' },
    { "ali", require_arg, 'a' },
    { "al", require_arg, 'a' },
    { "api", require_arg, 'A' },
    { "ap", require_arg, 'A' },
#if 0
    /* a sighting of the elusive binary option */
    { "binary", no_arg, 'b' },
    { "binar", no_arg, 'b' },
    { "bina", no_arg, 'b' },
    { "bin", no_arg, 'b' },
    { "bi", no_arg, 'b' },
#endif  /* 0 */
    { "chunk", no_arg, 'c' },
    { "chun", no_arg, 'c' },
    { "chu", no_arg, 'c' },
    { "ch", no_arg, 'c' },
    { "debug", require_arg, 'D' },
    { "debu", require_arg, 'D' },
    { "deb", require_arg, 'D' },
    { "de", require_arg, 'D' },
    { "file-size", require_arg, 'f' },
    { "file-siz", require_arg, 'f' },
    { "file-si", require_arg, 'f' },
    { "file-s", require_arg, 'f' },
    { "file", require_arg, 'f' },
    { "fil", require_arg, 'f' },
    { "fi", require_arg, 'f' },
    { "max-num-processes", require_arg, 'P' },
    { "max-num-processe", require_arg, 'P' },
    { "max-num-process", require_arg, 'P' },
    { "max-num-proces", require_arg, 'P' },
    { "max-num-proce", require_arg, 'P' },
    { "max-num-proc", require_arg, 'P' },
    { "max-num-pro", require_arg, 'P' },
    { "max-num-pr", require_arg, 'P' },
    { "max-num-p", require_arg, 'P' },
    { "min-num-processes", require_arg, 'p' },
    { "min-num-processe", require_arg, 'p' },
    { "min-num-process", require_arg, 'p' },
    { "min-num-proces", require_arg, 'p' },
    { "min-num-proce", require_arg, 'p' },
    { "min-num-proc", require_arg, 'p' },
    { "min-num-pro", require_arg, 'p' },
    { "min-num-pr", require_arg, 'p' },
    { "min-num-p", require_arg, 'p' },
    { "max-xfer-size", require_arg, 'X' },
    { "max-xfer-siz", require_arg, 'X' },
    { "max-xfer-si", require_arg, 'X' },
    { "max-xfer-s", require_arg, 'X' },
    { "max-xfer", require_arg, 'X' },
    { "max-xfe", require_arg, 'X' },
    { "max-xf", require_arg, 'X' },
    { "max-x", require_arg, 'X' },
    { "min-xfer-size", require_arg, 'x' },
    { "min-xfer-siz", require_arg, 'x' },
    { "min-xfer-si", require_arg, 'x' },
    { "min-xfer-s", require_arg, 'x' },
    { "min-xfer", require_arg, 'x' },
    { "min-xfe", require_arg, 'x' },
    { "min-xf", require_arg, 'x' },
    { "min-x", require_arg, 'x' },
    { "no-fill", no_arg, 'n' },
    { "no-fil", no_arg, 'n' },
    { "no-fi", no_arg, 'n' },
    { "no-f", no_arg, 'n' },
    { "no-", no_arg, 'n' },
    { "no", no_arg, 'n' },
    { "num-dsets", require_arg, 'd' },
    { "num-dset", require_arg, 'd' },
    { "num-dse", require_arg, 'd' },
    { "num-ds", require_arg, 'd' },
    { "num-d", require_arg, 'd' },
    { "num-files", require_arg, 'F' },
    { "num-file", require_arg, 'F' },
    { "num-fil", require_arg, 'F' },
    { "num-fi", require_arg, 'F' },
    { "num-f", require_arg, 'F' },
    { "num-iterations", require_arg, 'i' },
    { "num-iteration", require_arg, 'i' },
    { "num-iteratio", require_arg, 'i' },
    { "num-iterati", require_arg, 'i' },
    { "num-iterat", require_arg, 'i' },
    { "num-itera", require_arg, 'i' },
    { "num-iter", require_arg, 'i' },
    { "num-ite", require_arg, 'i' },
    { "num-it", require_arg, 'i' },
    { "num-i", require_arg, 'i' },
    { "output", require_arg, 'o' },
    { "outpu", require_arg, 'o' },
    { "outp", require_arg, 'o' },
    { "out", require_arg, 'o' },
    { "ou", require_arg, 'o' },
    { "threshold", require_arg, 'T' },
    { "threshol", require_arg, 'T' },
    { "thresho", require_arg, 'T' },
    { "thresh", require_arg, 'T' },
    { "thres", require_arg, 'T' },
    { "thre", require_arg, 'T' },
    { "thr", require_arg, 'T' },
    { "th", require_arg, 'T' },
    { NULL, 0, '\0' }
};

struct options {
    long io_types;              /* bitmask of which I/O types to test   */
    const char *output_file;    /* file to print report to              */
    off_t file_size;            /* size of file                         */
    long num_dsets;             /* number of datasets                   */
    long num_files;             /* number of files                      */
    long num_iters;             /* number of iterations                 */
    int max_num_procs;          /* maximum number of processes to use   */
    int min_num_procs;          /* minimum number of processes to use   */
    size_t max_xfer_size;       /* maximum transfer buffer size         */
    size_t min_xfer_size;       /* minimum transfer buffer size         */
    int print_times;       	/* print times as well as throughputs   */
    int print_raw;         	/* print raw data throughput info       */
    off_t h5_alignment;         /* alignment in HDF5 file               */
    off_t h5_threshold;         /* threshold for alignment in HDF5 file */
    int h5_use_chunks;     	/* Make HDF5 dataset chunked            */
    int h5_no_fill;        	/* Disable HDF5 writing fill values     */
};

typedef struct _minmax {
    double min;
    double max;
    double sum;
    int num;
} minmax;

/* local functions */
static off_t parse_size_directive(const char *size);
static struct options *parse_command_line(int argc, char *argv[]);
static void run_test_loop(struct options *options);
static int run_test(iotype iot, parameters parms, struct options *opts);
static void output_all_info(minmax *mm, int count, int indent_level);
static void get_minmax(minmax *mm, double val);
static minmax accumulate_minmax_stuff(minmax *mm, int count);
static int create_comm_world(int num_procs, int *doing_pio);
static int destroy_comm_world(void);
static void output_results(const struct options *options, const char *name,
                           minmax *table, int table_size, off_t data_size);
static void output_report(const char *fmt, ...);
static void print_indent(register int indent);
static void usage(const char *prog);
static int parse_environment(void);
static void report_parameters(struct options *opts);

/*
 * Function:    main
 * Purpose:     Start things up. Initialize MPI and then call the test looping
 *              function.
 * Return:      EXIT_SUCCESS or EXIT_FAILURE
 * Programmer:  Bill Wendling, 30. October 2001
 * Modifications:
 */
int
main(int argc, char **argv)
{
    int ret;
    int exit_value = EXIT_SUCCESS;
    struct options *opts = NULL;

    output = stdout;

    /* initialize MPI and get the maximum num of processors we started with */
    MPI_Init(&argc, &argv);
    ret = MPI_Comm_size(MPI_COMM_WORLD, &comm_world_nprocs_g);

    if (ret != MPI_SUCCESS) {
        fprintf(stderr, "%s: MPI_Comm_size call failed\n", progname);

        if (ret == MPI_ERR_COMM)
            fprintf(stderr, "invalid MPI communicator\n");
        else
            fprintf(stderr, "invalid argument\n");

        exit_value = EXIT_FAILURE;
        goto finish;
    }

    ret = MPI_Comm_rank(MPI_COMM_WORLD, &comm_world_rank_g);

    if (ret != MPI_SUCCESS) {
        fprintf(stderr, "%s: MPI_Comm_rank call failed\n", progname);

        if (ret == MPI_ERR_COMM)
            fprintf(stderr, "invalid MPI communicator\n");
        else
            fprintf(stderr, "invalid argument\n");

        exit_value = EXIT_FAILURE;
        goto finish;
    }

    pio_comm_g = MPI_COMM_WORLD;

    h5_set_info_object();
    opts = parse_command_line(argc, argv);

    if (!opts) {
        exit_value = EXIT_FAILURE;
        goto finish;
    }

    if (opts->output_file) {
        if ((output = fopen(opts->output_file, "w")) == NULL) {
            fprintf(stderr, "%s: cannot open output file\n", progname);
            perror(opts->output_file);
            goto finish;
        }
    }

    report_parameters(opts);
    run_test_loop(opts);

finish:
    MPI_Finalize();
    free(opts);
    return exit_value;
}

/*
 * Function:    run_test_loop
 * Purpose:     Run the I/O tests. Write the results to OUTPUT.
 *
 *            - The slowest changing part of the test is the number of
 *              processors to use. For each loop iteration, we divide that
 *              number by 2 and rerun the test.
 *
 *            - The second slowest is what type of IO API to perform. We have
 *              three choices: POSIXIO, MPI-IO, and PHDF5.
 *
 *            - Then we change the size of the buffer. This information is
 *              inferred from the number of datasets to create and the number
 *              of integers to put into each dataset. The backend code figures
 *              this out.
 *
 * Return:      Nothing
 * Programmer:  Bill Wendling, 30. October 2001
 * Modifications:
 */
static void
run_test_loop(struct options *opts)
{
    parameters parms;
    int num_procs;
    int doing_pio;      /* if this process is doing PIO */
    int io_runs = PIO_HDF5 | PIO_MPI | PIO_POSIX; /* default to run all tests */

    if (opts->io_types & ~0x7) {
        /* we want to run only a select subset of these tests */
        io_runs = 0;

        if (opts->io_types & PIO_HDF5)
            io_runs |= PIO_HDF5;

        if (opts->io_types & PIO_MPI)
            io_runs |= PIO_MPI;

        if (opts->io_types & PIO_POSIX)
            io_runs |= PIO_POSIX;
    }

    parms.num_files = opts->num_files;
    parms.num_dsets = opts->num_dsets;
    parms.num_iters = opts->num_iters;
    parms.h5_align = opts->h5_alignment;
    parms.h5_thresh = opts->h5_threshold;
    parms.h5_use_chunks = opts->h5_use_chunks;
    parms.h5_no_fill = opts->h5_no_fill;

    /* start with max_num_procs and decrement it by half for each loop. */
    /* if performance needs restart, fewer processes may be needed. */
    for (num_procs = opts->max_num_procs;
            num_procs >= opts->min_num_procs; num_procs >>= 1) {
        register size_t buf_size;

        parms.num_procs = num_procs;

        if (create_comm_world(parms.num_procs, &doing_pio) != SUCCESS) {
            /* do something harsh */
        }

	/* only processes doing PIO will run the tests */
	if (doing_pio){
            output_report("Number of processors = %ld\n", parms.num_procs);

            /* multiply the xfer buffer size by 2 for each loop iteration */
            for (buf_size = opts->min_xfer_size;
                    buf_size <= opts->max_xfer_size; buf_size <<= 1) {
                parms.buf_size = buf_size;
                parms.num_elmts = opts->file_size /
                                    (off_t)(parms.num_dsets * sizeof(int));

                print_indent(1);
                output_report("Transfer Buffer Size: %ld bytes, File size: %.2f MBs\n",
                              buf_size,
                              ((double)parms.num_dsets * (double)parms.num_elmts *
                                    (double)sizeof(int)) / ONE_MB);
                print_indent(1);
                output_report("  # of files: %ld, # of dsets: %ld, # of elmts per dset: %ld\n",
                              parms.num_files, parms.num_dsets, parms.num_elmts);

                if (io_runs & PIO_POSIX)
                    run_test(POSIXIO, parms, opts);

                if (io_runs & PIO_MPI)
                    run_test(MPIO, parms, opts);

                if (io_runs & PIO_HDF5)
                    run_test(PHDF5, parms, opts);

                /* Run the tests once if buf_size==0, but then break out */
                if(buf_size==0)
                    break;
            }

            if (destroy_comm_world() != SUCCESS) {
                /* do something harsh */
            }
	}
    }
}

/*
 * Function:    run_test
 * Purpose:     Inner loop call to actually run the I/O test.
 * Return:      Nothing
 * Programmer:  Bill Wendling, 18. December 2001
 * Modifications:
 */
static int
run_test(iotype iot, parameters parms, struct options *opts)
{
    results         res;
    register int    i, ret_value = SUCCESS;
    int             comm_size;
    off_t           raw_size;
    minmax         *write_mpi_mm_table=NULL;
    minmax         *write_mm_table=NULL;
    minmax         *write_gross_mm_table=NULL;
    minmax         *write_raw_mm_table=NULL;
    minmax         *read_mpi_mm_table=NULL;
    minmax         *read_mm_table=NULL;
    minmax         *read_gross_mm_table=NULL;
    minmax         *read_raw_mm_table=NULL;
    minmax          write_mpi_mm = {0.0, 0.0, 0.0, 0};
    minmax          write_mm = {0.0, 0.0, 0.0, 0};
    minmax          write_gross_mm = {0.0, 0.0, 0.0, 0};
    minmax          write_raw_mm = {0.0, 0.0, 0.0, 0};
    minmax          read_mpi_mm = {0.0, 0.0, 0.0, 0};
    minmax          read_mm = {0.0, 0.0, 0.0, 0};
    minmax          read_gross_mm = {0.0, 0.0, 0.0, 0};
    minmax          read_raw_mm = {0.0, 0.0, 0.0, 0};

    raw_size = (off_t)parms.num_dsets * (off_t)parms.num_elmts * (off_t)sizeof(int);
    parms.io_type = iot;
    print_indent(2);
    output_report("IO API = ");

    switch (iot) {
    case POSIXIO:
        output_report("POSIX\n");
        break;
    case MPIO:
        output_report("MPIO\n");
        break;
    case PHDF5:
        output_report("PHDF5\n");
        break;
    }

    MPI_Comm_size(pio_comm_g, &comm_size);

    /* allocate space for tables minmax and that it is sufficient */
    /* to initialize all elements to zeros by calloc.             */
    write_mpi_mm_table = calloc(parms.num_iters , sizeof(minmax));
    write_mm_table = calloc(parms.num_iters , sizeof(minmax));
    write_gross_mm_table = calloc(parms.num_iters , sizeof(minmax));
    write_raw_mm_table = calloc(parms.num_iters , sizeof(minmax));
    read_mpi_mm_table = calloc(parms.num_iters , sizeof(minmax));
    read_mm_table = calloc(parms.num_iters , sizeof(minmax));
    read_gross_mm_table = calloc(parms.num_iters , sizeof(minmax));
    read_raw_mm_table = calloc(parms.num_iters , sizeof(minmax));

    /* Do IO iteration times, collecting statistics each time */
    for (i = 0; i < parms.num_iters; ++i) {
        double t;

        MPI_Barrier(pio_comm_g);
        res = do_pio(parms);

        /* gather all of the "mpi write" times */
        t = get_time(res.timers, HDF5_MPI_WRITE);
	get_minmax(&write_mpi_mm, t);

        write_mpi_mm_table[i] = write_mpi_mm;

        /* gather all of the "write" times */
        t = get_time(res.timers, HDF5_FINE_WRITE_FIXED_DIMS);
	get_minmax(&write_mm, t);

        write_mm_table[i] = write_mm;

        /* gather all of the "write" times from open to close */
        t = get_time(res.timers, HDF5_GROSS_WRITE_FIXED_DIMS);
	get_minmax(&write_gross_mm, t);

        write_gross_mm_table[i] = write_gross_mm;

        /* gather all of the raw "write" times */
        t = get_time(res.timers, HDF5_RAW_WRITE_FIXED_DIMS);
	get_minmax(&write_raw_mm, t);

        write_raw_mm_table[i] = write_raw_mm;

        /* gather all of the "mpi read" times */
        t = get_time(res.timers, HDF5_MPI_READ);
	get_minmax(&read_mpi_mm, t);

        read_mpi_mm_table[i] = read_mpi_mm;

        /* gather all of the "read" times */
        t = get_time(res.timers, HDF5_FINE_READ_FIXED_DIMS);
	get_minmax(&read_mm, t);

        read_mm_table[i] = read_mm;

        /* gather all of the "read" times from open to close */
        t = get_time(res.timers, HDF5_GROSS_READ_FIXED_DIMS);
	get_minmax(&read_gross_mm, t);

        read_gross_mm_table[i] = read_gross_mm;

        /* gather all of the raw "read" times */
        t = get_time(res.timers, HDF5_RAW_READ_FIXED_DIMS);
	get_minmax(&read_raw_mm, t);

        read_raw_mm_table[i] = read_raw_mm;
        pio_time_destroy(res.timers);
    }

    /* 
     * Show various statistics
     */
    /* Write statistics	*/
    /* Print the raw data throughput if desired */
    if (opts->print_raw) {
        /* accumulate and output the max, min, and average "raw write" times */
        if (pio_debug_level >= 3) {
            /* output all of the times for all iterations */
            print_indent(3);
            output_report("Raw Data Write details:\n");
            output_all_info(write_raw_mm_table, parms.num_iters, 4);
        }

        output_results(opts,"Raw Data Write",write_raw_mm_table,parms.num_iters,raw_size);
    } /* end if */

    /* show mpi write statics */
    if (pio_debug_level >= 3) {
        /* output all of the times for all iterations */
        print_indent(3);
        output_report("MPI Write details:\n");
        output_all_info(write_mpi_mm_table, parms.num_iters, 4);
    }

    /* We don't currently output the MPI write results */

    /* accumulate and output the max, min, and average "write" times */
    if (pio_debug_level >= 3) {
        /* output all of the times for all iterations */
        print_indent(3);
        output_report("Write details:\n");
        output_all_info(write_mm_table, parms.num_iters, 4);
    }

    output_results(opts,"Write",write_mm_table,parms.num_iters,raw_size);

    /* accumulate and output the max, min, and average "gross write" times */
    if (pio_debug_level >= 3) {
        /* output all of the times for all iterations */
        print_indent(3);
        output_report("Write Open-Close details:\n");
        output_all_info(write_gross_mm_table, parms.num_iters, 4);
    }

    output_results(opts,"Write Open-Close",write_gross_mm_table,parms.num_iters,raw_size);

    /* Read statistics	*/
    /* Print the raw data throughput if desired */
    if(opts->print_raw) {
        /* accumulate and output the max, min, and average "raw read" times */
        if (pio_debug_level >= 3) {
            /* output all of the times for all iterations */
            print_indent(3);
            output_report("Raw Data Read details:\n");
            output_all_info(read_raw_mm_table, parms.num_iters, 4);
        }

        output_results(opts,"Raw Data Read",read_raw_mm_table,parms.num_iters,raw_size);
    } /* end if */

    /* show mpi read statics */
    if (pio_debug_level >= 3) {
        /* output all of the times for all iterations */
        print_indent(3);
        output_report("MPI Read details:\n");
        output_all_info(read_mpi_mm_table, parms.num_iters, 4);
    }

    /* We don't currently output the MPI read results */

    /* accumulate and output the max, min, and average "read" times */
    if (pio_debug_level >= 3) {
        /* output all of the times for all iterations */
        print_indent(3);
        output_report("Read details:\n");
        output_all_info(read_mm_table, parms.num_iters, 4);
    }

    output_results(opts,"Read",read_mm_table,parms.num_iters,raw_size);

    /* accumulate and output the max, min, and average "gross read" times */
    if (pio_debug_level >= 3) {
        /* output all of the times for all iterations */
        print_indent(3);
        output_report("Read Open-Close details:\n");
        output_all_info(read_gross_mm_table, parms.num_iters, 4);
    }

    output_results(opts,"Read Open-Close",read_gross_mm_table,parms.num_iters,raw_size);

    /* clean up our mess */
    free(write_mpi_mm_table);
    free(read_mpi_mm_table);
    free(write_mm_table);
    free(read_mm_table);
    free(write_gross_mm_table);
    free(read_gross_mm_table);
    free(write_raw_mm_table);
    free(read_raw_mm_table);
    return ret_value;
}

/*
 * Function:    output_all_info
 * Purpose:     
 * Return:      Nothing
 * Programmer:  Bill Wendling, 29. January 2002
 * Modifications:
 */
static void
output_all_info(minmax *mm, int count, int indent_level)
{
    register int i;

    for (i = 0; i < count; ++i) {
        print_indent(indent_level);
        output_report("Iteration %d:\n", i + 1);
        print_indent(indent_level + 1);
        output_report("Minimum Time: %.2fs\n", mm[i].min);
        print_indent(indent_level + 1);
        output_report("Maximum Time: %.2fs\n", mm[i].max);
    }
}

/*
 * Function:    get_minmax
 * Purpose:     Gather all the min, max and total of val.
 * Return:      Nothing
 * Programmer:  Bill Wendling, 21. December 2001
 * Modifications:
 *    Use MPI_Allreduce to do it. -akc, 2002/01/11
 */
static void
get_minmax(minmax *mm, double val)
{
    int myrank;

    MPI_Comm_rank(pio_comm_g, &myrank);
    MPI_Comm_size(pio_comm_g, &mm->num);

    MPI_Allreduce(&val, &mm->max, 1, MPI_DOUBLE, MPI_MAX, pio_comm_g);
    MPI_Allreduce(&val, &mm->min, 1, MPI_DOUBLE, MPI_MIN, pio_comm_g);
    MPI_Allreduce(&val, &mm->sum, 1, MPI_DOUBLE, MPI_SUM, pio_comm_g);
}

/*
 * Function:    accumulate_minmax_stuff
 * Purpose:     Accumulate the minimum, maximum, and average of the times
 *              across all processes.
 * Return:      TOTAL_MM - the total of all of these.
 * Programmer:  Bill Wendling, 21. December 2001
 * Modifications:
 *              Changed to use seconds instead of MB/s - QAK, 5/9/02
 */
static minmax
accumulate_minmax_stuff(minmax *mm, int count)
{
    register int i;
    minmax total_mm;
    
    total_mm.sum = 0.0;
    total_mm.max = -DBL_MAX;
    total_mm.min = DBL_MAX;
    total_mm.num = count;

    for (i = 0; i < count; ++i) {
        double m = mm[i].max;

        total_mm.sum += m;

        if (m < total_mm.min)
            total_mm.min = m;

        if (m > total_mm.max)
            total_mm.max = m;
    }

    return total_mm;
}

/*
 * Function:    create_comm_world
 * Purpose:     Create an MPI Comm world and store it in pio_comm_g, which
 *              is a global variable.
 * Return:      SUCCESS on success.
 *              FAIL otherwise.
 * Programmer:  Bill Wendling, 19. December 2001
 * Modifications:
 */
static int
create_comm_world(int num_procs, int *doing_pio)
{
    /* MPI variables */
    int     mrc, ret_value;     /* return values                */
    int     color;              /* for communicator creation    */
    int     myrank, nprocs;

    pio_comm_g = MPI_COMM_NULL;

    /*
     * Create a sub communicator for this PIO run. Easier to use the first N
     * processes.
     */
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    if (num_procs > nprocs) {
        fprintf(stderr,
                "number of process(%d) must be <= number of processes in MPI_COMM_WORLD(%d)\n",
                num_procs, nprocs);
        goto error_done;
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    color = (myrank < num_procs);
    mrc = MPI_Comm_split(MPI_COMM_WORLD, color, myrank, &pio_comm_g);

    if (mrc != MPI_SUCCESS) {
        fprintf(stderr, "MPI_Comm_split failed\n");
        goto error_done;
    }

    if (!color) {
        /* not involved in this run */
        mrc = destroy_comm_world();
        goto done;
    }

    /* determine the MPI rank in the PIO communicator */
    MPI_Comm_size(pio_comm_g, &pio_mpi_nprocs_g);
    MPI_Comm_rank(pio_comm_g, &pio_mpi_rank_g);

done:
    *doing_pio = color;
    return ret_value;

error_done:
    destroy_comm_world();
    return FAIL;
}

/*
 * Function:    destroy_comm_world
 * Purpose:     Destroy the created MPI Comm world which is stored in the
 *              pio_comm_g global variable.
 * Return:      SUCCESS on success.
 *              FAIL otherwise.
 * Programmer:  Bill Wendling, 19. December 2001
 * Modifications:
 */
static int
destroy_comm_world(void)
{
    int     mrc = SUCCESS;      /* return code      */

    /* release MPI resources */
    if (pio_comm_g != MPI_COMM_NULL)
        mrc = (MPI_Comm_free(&pio_comm_g) == MPI_SUCCESS ? SUCCESS : FAIL);

    return mrc;
}

/*
 * Function:    output_results
 * Purpose:     Print information about the time & bandwidth for a given
 *                  minmax & # of iterations.
 * Return:      Nothing
 * Programmer:  Quincey Koziol, 9. May 2002
 * Modifications:
 */
static void
output_results(const struct options *opts, const char *name, minmax *table,
    int table_size,off_t data_size)
{
    minmax          total_mm;

    total_mm = accumulate_minmax_stuff(table, table_size);

    print_indent(3);
    output_report("%s (%d iteration(s)):\n", name,(int)table_size);

    /* Note: The maximum throughput uses the minimum amount of time & vice versa */

    print_indent(4);
    output_report("Maximum Throughput: %6.2f MB/s", MB_PER_SEC(data_size,total_mm.min));
    if(opts->print_times)
        output_report(" (%7.3f s)\n", total_mm.min);
    else
        output_report("\n");

    print_indent(4);
    output_report("Average Throughput: %6.2f MB/s",
                  MB_PER_SEC(data_size,total_mm.sum / total_mm.num));
    if(opts->print_times)
        output_report(" (%7.3f s)\n", (total_mm.sum / total_mm.num));
    else
        output_report("\n");

    print_indent(4);
    output_report("Minimum Throughput: %6.2f MB/s", MB_PER_SEC(data_size,total_mm.max));
    if(opts->print_times)
        output_report(" (%7.3f s)\n", total_mm.max);
    else
        output_report("\n");
}

/*
 * Function:    output_report
 * Purpose:     Print a line of the report. Only do so if I'm the 0 process.
 * Return:      Nothing
 * Programmer:  Bill Wendling, 19. December 2001
 * Modifications:
 */
static void
output_report(const char *fmt, ...)
{
    int myrank;

    MPI_Comm_rank(pio_comm_g, &myrank);

    if (myrank == 0) {
        va_list ap;

        va_start(ap, fmt);
        vfprintf(output, fmt, ap);
        va_end(ap);
    }
}

/*
 * Function:    print_indent
 * Purpose:     Print spaces to indent a new line of text for pretty printing
 *              things.
 * Return:      Nothing
 * Programmer:  Bill Wendling, 29. October 2001
 * Modifications:
 */
static void
print_indent(register int indent)
{
    int myrank;

    MPI_Comm_rank(pio_comm_g, &myrank);

    if (myrank == 0) {
	indent *= TAB_SPACE;

	for (; indent > 0; --indent)
	    fputc(' ', output);
    }
}

static void
recover_size_and_print(long_long val, const char *end)
{
    if (val >= ONE_KB && (val % ONE_KB) == 0) {
        if (val >= ONE_MB && (val % ONE_MB) == 0) {
            if (val >= ONE_GB && (val % ONE_GB) == 0)
                HDfprintf(output, "%HdGB%s", val / ONE_GB, end);
            else
                HDfprintf(output, "%HdMB%s", val / ONE_MB, end);
        } else {
            HDfprintf(output, "%HdKB%s", val / ONE_KB, end);
        }
    } else {
        HDfprintf(output, "%Hd%s", val, end);
    }
}

static void
report_parameters(struct options *opts)
{
    int rank;

    MPI_Comm_rank(pio_comm_g, &rank);

    HDfprintf(output, "rank %d: File size=", rank);
    recover_size_and_print((long_long)opts->file_size, "\n");

    HDfprintf(output, "rank %d: Number of files=%Hd\n", rank,
              (long_long)opts->num_files);
    HDfprintf(output, "rank %d: Number of datasets=%Hd\n", rank,
              (long_long)opts->num_dsets);
    HDfprintf(output, "rank %d: Number of iterations=%Hd\n", rank,
              (long_long)opts->num_iters);
    HDfprintf(output, "rank %d: Number of processes=%Hd:%Hd\n", rank,
              (long_long)opts->min_num_procs,
              (long_long)opts->max_num_procs);

    HDfprintf(output, "rank %d: Transfer buffer size=", rank);
    recover_size_and_print((long_long)opts->min_xfer_size, ":");
    recover_size_and_print((long_long)opts->max_xfer_size, "\n");

    {
        char *prefix = getenv("H5_PARAPREFIX");

        HDfprintf(output, "rank %d: H5_PARAPREFIX Environment Var=%s\n", rank,
                  (prefix ? prefix : "not set"));
    }

    HDfprintf(output, "rank %d: ", rank);
    h5_dump_info_object(pio_info_g);
    HDfprintf(output, "\n");
}

/*
 * Function:    parse_command_line
 * Purpose:     Parse the command line options and return a STRUCT OPTIONS
 *              structure which will need to be freed by the calling function.
 * Return:      Pointer to an OPTIONS structure
 * Programmer:  Bill Wendling, 31. October 2001
 * Modifications:
 */
static struct options *
parse_command_line(int argc, char *argv[])
{
    register int opt;
    struct options *cl_opts;

    cl_opts = (struct options *)malloc(sizeof(struct options));

    cl_opts->output_file = NULL;
    cl_opts->file_size = 64 * ONE_MB;
    cl_opts->io_types = 0x7;    /* bottom bits indicate default type to run */
    cl_opts->num_dsets = 1;
    cl_opts->num_files = 1;
    cl_opts->num_iters = 1;
    cl_opts->max_num_procs = comm_world_nprocs_g;
    cl_opts->min_num_procs = 1;
    cl_opts->max_xfer_size = 1 * ONE_MB;
    cl_opts->min_xfer_size = 128 * ONE_KB;
    cl_opts->print_times = 0;   /* Printing times is off by default */
    cl_opts->print_raw = 0;     /* Printing raw data throughput is off by default */
    cl_opts->h5_alignment = 1;  /* No alignment for HDF5 objects by default */
    cl_opts->h5_threshold = 1;  /* No threshold for aligning HDF5 objects by default */
    cl_opts->h5_use_chunks = 0; /* Don't chunk the HDF5 dataset by default */
    cl_opts->h5_no_fill = 0;    /* Write fill values by default */

    while ((opt = get_option(argc, (const char **)argv, s_opts, l_opts)) != EOF) {
        switch ((char)opt) {
        case 'a':
            cl_opts->h5_alignment = parse_size_directive(opt_arg);
            break;
        case 'A':
            cl_opts->io_types &= ~0x7;

            {
                const char *end = opt_arg;

                while (end && *end != '\0') {
                    char buf[10];
                    int i;

                    memset(buf, '\0', sizeof(buf));

                    for (i = 0; *end != '\0' && *end != ','; ++end)
                        if (isalnum(*end) && i < 10)
                            buf[i++] = *end;

                    if (!strcasecmp(buf, "phdf5")) {
                        cl_opts->io_types |= PIO_HDF5;
                    } else if (!strcasecmp(buf, "mpiio")) {
                        cl_opts->io_types |= PIO_MPI;
                    } else if (!strcasecmp(buf, "posix")) {
                        cl_opts->io_types |= PIO_POSIX;
                    } else {
                        fprintf(stderr, "pio_perf: invalid --api option %s\n",
                                buf);
                        exit(EXIT_FAILURE);
                    }

                    if (*end == '\0')
                        break;

                    end++;
                }
            }

            break;
#if 0
        case 'b':
            /* the future "binary" option */
            break;
#endif  /* 0 */
        case 'c':       /* Turn on chunked HDF5 dataset creation */
            cl_opts->h5_use_chunks = 1;
            break;
        case 'd':
            cl_opts->num_dsets = atoi(opt_arg);
            break;
        case 'D':
            {
                const char *end = opt_arg;

                while (end && *end != '\0') {
                    char buf[10];
                    int i;

                    memset(buf, '\0', sizeof(buf));

                    for (i = 0; *end != '\0' && *end != ','; ++end)
                        if (isalnum(*end) && i < 10)
                            buf[i++] = *end;

                    if (strlen(buf) > 1 || isdigit(buf[0])) {
                        register int i;

                        for (i = 0; i < 10 && buf[i] != '\0'; ++i)
                            if (!isdigit(buf[i])) {
                                fprintf(stderr, "pio_perf: invalid --debug option %s\n",
                                        buf);
                                exit(EXIT_FAILURE);
                            }

                        pio_debug_level = atoi(buf);

                        if (pio_debug_level > 4)
                            pio_debug_level = 4;
                        else if (pio_debug_level < 0)
                            pio_debug_level = 0;
                    } else {
                        switch (*buf) {
                        case 'r':
                            /* Turn on raw data throughput info */
                            cl_opts->print_raw = 1;
                            break;
                        case 't':
                            /* Turn on time printing */
                            cl_opts->print_times = 1;
                            break;
                        default:
                            fprintf(stderr, "pio_perf: invalid --debug option %s\n", buf);
                            exit(EXIT_FAILURE);
                        }
                    }

                    if (*end == '\0')
                        break;

                    end++;
                }
            }

            break;
        case 'f':
            cl_opts->file_size = parse_size_directive(opt_arg);
            break;
        case 'F':
            cl_opts->num_files = atoi(opt_arg);
            break;
        case 'i':
            cl_opts->num_iters = atoi(opt_arg);
            break;
        case 'n':       /* Turn off writing fill values */
#ifdef H5_HAVE_NOFILL
            cl_opts->h5_no_fill = 1;
#else
	    fprintf(stderr, "pio_perf: --no-fill not supported\n");
            usage(progname);
	    exit(EXIT_FAILURE);
#endif
            break;
        case 'o':
            cl_opts->output_file = opt_arg;
            break;
        case 'p':
            cl_opts->min_num_procs = atoi(opt_arg);
            break;
        case 'P':
            cl_opts->max_num_procs = atoi(opt_arg);
            break;
        case 'T':
            cl_opts->h5_threshold = parse_size_directive(opt_arg);
            break;
        case 'x':
            cl_opts->min_xfer_size = parse_size_directive(opt_arg);
            break;
        case 'X':
            cl_opts->max_xfer_size = parse_size_directive(opt_arg);
            break;
        case 'h':
        case '?':
        default:
            usage(progname);
            free(cl_opts);
            return NULL;
        }
    }

    return cl_opts;
}

/*
 * Function:    parse_size_directive
 * Purpose:     Parse the size directive passed on the commandline. The size
 *              directive is an integer followed by a size indicator:
 *
 *                  K, k - Kilobyte
 *                  M, m - Megabyte
 *                  G, g - Gigabyte
 *
 * Return:      The size as a off_t because this is related to file size.
 *              If an unknown size indicator is used, then the program will
 *              exit with EXIT_FAILURE as the return value.
 * Programmer:  Bill Wendling, 18. December 2001
 * Modifications:
 */
static off_t
parse_size_directive(const char *size)
{
    off_t s;
    char *endptr;

    s = strtol(size, &endptr, 10);

    if (endptr && *endptr) {
        while (*endptr != '\0' && (*endptr == ' ' || *endptr == '\t'))
            ++endptr;

        switch (*endptr) {
            case 'K':
            case 'k':
                s *= ONE_KB;
                break;
            case 'M':
            case 'm':
                s *= ONE_MB;
                break;
            case 'G':
            case 'g':
                s *= ONE_GB;
                break;
            default:
                fprintf(stderr, "Illegal size specifier '%c'\n", *endptr);
                exit(EXIT_FAILURE);
        }
    }

    return s;
}

/*
 * Function:    usage
 * Purpose:     Print a usage message and then exit.
 * Return:      Nothing
 * Programmer:  Bill Wendling, 31. October 2001
 * Modifications:
 */
static void
usage(const char *prog)
{
    int myrank;

    MPI_Comm_rank(pio_comm_g, &myrank);

    if (myrank == 0) {
	print_version(prog);
        printf("usage: %s [OPTIONS]\n", prog);
        printf("  OPTIONS\n");
        printf("     -h, --help                  Print a usage message and exit\n");
        printf("     -a S, --align=S             Alignment of objects in HDF5 file [default: 1]\n");
        printf("     -A AL, --api=AL             Which APIs to test [default: all of them]\n");
#if 0
        printf("     -b, --binary                The elusive binary option\n");
#endif  /* 0 */
        printf("     -c, --chunk                 Create HDF5 datasets chunked [default: off]\n");
        printf("     -d N, --num-dsets=N         Number of datasets per file [default:1]\n");
        printf("     -D DL, --debug=DL           Indicate the debugging level\n");
        printf("                                 [default: no debugging]\n");
        printf("     -f S, --file-size=S         Size of a single file [default: 64M]\n");
        printf("     -F N, --num-files=N         Number of files [default: 1]\n");
        printf("     -i, --num-iterations        Number of iterations to perform [default: 1]\n");
        printf("     -n, --no-fill               Don't write fill values to HDF5 dataset\n");
        printf("                                 (Supported in HDF5 library v1.5 only)\n");
        printf("                                 [default: off (i.e. write fill values)]\n");
        printf("     -o F, --output=F            Output raw data into file F [default: none]\n");
        printf("     -P N, --max-num-processes=N Maximum number of processes to use\n");
        printf("                                 [default: all MPI_COMM_WORLD processes ]\n");
        printf("     -p N, --min-num-processes=N Minimum number of processes to use [default: 1]\n");
        printf("     -T S, --threshold=S         Threshold for alignment of objects in HDF5 file\n");
        printf("                                 [default: 1]\n");
        printf("     -X S, --max-xfer-size=S     Maximum transfer buffer size [default: 1M]\n");
        printf("     -x S, --min-xfer-size=S     Minimum transfer buffer size [default: 128K]\n");
        printf("\n");
        printf("  F  - is a filename.\n");
        printf("  N  - is an integer >=0.\n");
        printf("  S  - is a size specifier, an integer >=0 followed by a size indicator:\n");
        printf("          K - Kilobyte (%d)\n", ONE_KB);
        printf("          M - Megabyte (%d)\n", ONE_MB);
        printf("          G - Gigabyte (%d)\n", ONE_GB);
        printf("\n");
        printf("      Example: 37M = 37 Megabytes = %d bytes\n", 37*ONE_MB);
        printf("\n");
        printf("  AL - is an API list. Valid values are:\n");
        printf("          phdf5 - Parallel HDF5\n");
        printf("          mpiio - MPI-I/O\n");
        printf("          posix - POSIX\n");
        printf("\n");
        printf("      Example: --api=mpiio,phdf5\n");
        printf("\n");
        printf("  DL - is a list of debugging flags. Valid values are:\n");
        printf("          1 - Minimal\n");
        printf("          2 - Not quite everything\n");
        printf("          3 - Everything\n");
        printf("          4 - Everything and the kitchen sink\n");
        printf("          r - Raw data I/O throughput information\n");
        printf("          t - Times as well as throughputs\n");
        printf("\n");
        printf("      Example: --debug=2,r,t\n");
        printf("\n");
        fflush(stdout);
    }
}

#else /* H5_HAVE_PARALLEL */

/*
 * Function:    main
 * Purpose:     Dummy main() function for if HDF5 was configured without
 *              parallel stuff.
 * Return:      EXIT_SUCCESS
 * Programmer:  Bill Wendling, 14. November 2001
 * Modifications:
 */
int
main(void)
{
    printf("No parallel IO performance because parallel is not configured\n");
    return EXIT_SUCCESS;
}

#endif /* !H5_HAVE_PARALLEL */
