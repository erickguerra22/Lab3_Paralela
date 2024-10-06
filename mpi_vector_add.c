/* File:     mpi_vector_add.c
 *
 * Purpose:  Implement parallel vector addition using a block
 *           distribution of the vectors.  This version also
 *           illustrates the use of MPI_Scatter and MPI_Gather.
 *
 * Compile:  mpicc -g -Wall -o mpi_vector_add mpi_vector_add.c
 * Run:      mpiexec -n <comm_sz> ./vector_add
 *
 * Input:    The order of the vectors, n, and the vectors x and y
 * Output:   The sum vector z = x+y
 *
 * Notes:
 * 1.  The order of the vectors, n, should be evenly divisible
 *     by comm_sz
 * 2.  DEBUG compile flag.
 * 3.  This program does fairly extensive error checking.  When
 *     an error is detected, a message is printed and the processes
 *     quit.  Errors detected are incorrect values of the vector
 *     order (negative or not evenly divisible by comm_sz), and
 *     malloc failures.
 *
 * IPP:  Section 3.4.6 (pp. 109 and ff.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>

void Check_for_error(int local_ok, char fname[], char message[],
                     MPI_Comm comm);
void Read_n(int *n_p, int *local_n_p, int my_rank, int comm_sz,
            MPI_Comm comm);
void Allocate_vectors(double **local_x_pp, double **local_y_pp,
                      double **local_z_pp, double **local_a_pp,
                      double **local_b_pp, int local_n, MPI_Comm comm);
void Read_vector(double local_a[], int local_n, int n, char vec_name[],
                 int my_rank, MPI_Comm comm);
void Print_vector(double local_b[], int local_n, int n, char title[],
                  int my_rank, MPI_Comm comm);
void Parallel_vector_sum(double local_x[], double local_y[],
                         double local_z[], int local_n);
void Parallel_vector_dot(double local_x[], double local_y[],
                         double *dotP, int local_n);
void Parallel_vector_scalar(double local_x[], double local_y[], double local_a[],
                            double local_b[], int local_n, int scalar);

/*-------------------------------------------------------------------*/
int main(void)
{
      int n, local_n;
      int comm_sz, my_rank;
      double *local_x, *local_y, *local_z, *local_b, *local_a;
      double dotP;
      int scalar;
      MPI_Comm comm;
      double tstart, tend;
      srand(time(NULL));

      MPI_Init(NULL, NULL);
      comm = MPI_COMM_WORLD;
      MPI_Comm_size(comm, &comm_sz);
      MPI_Comm_rank(comm, &my_rank);

      Read_n(&n, &local_n, my_rank, comm_sz, comm);
      tstart = MPI_Wtime();
      Allocate_vectors(&local_x, &local_y, &local_z, &local_a, &local_b, local_n, comm);

      Read_vector(local_x, local_n, n, "x", my_rank, comm);
      Print_vector(local_x, local_n, n, "Vector X:", my_rank, comm);
      Read_vector(local_y, local_n, n, "y", my_rank, comm);
      Print_vector(local_y, local_n, n, "Vector Y:", my_rank, comm);

      Parallel_vector_sum(local_x, local_y, local_z, local_n);
      Parallel_vector_dot(local_x, local_y, &dotP, local_n);

      scalar = (int)(rand() % (10 - 5 + 1)) + 5;
      Parallel_vector_scalar(local_x, local_y, local_a, local_b, local_n, scalar);

      tend = MPI_Wtime();

      Print_vector(local_z, local_n, n, "Resultado suma:", my_rank, comm);

      char scalar_message[100];
      sprintf(scalar_message, "Resultado X * %d", scalar);
      Print_vector(local_a, local_n, n, scalar_message, my_rank, comm);
      sprintf(scalar_message, "Resultado Y * %d", scalar);
      Print_vector(local_b, local_n, n, scalar_message, my_rank, comm);
      if (my_rank == 0)
      {
            printf("Escalar utilizado: %d\n", scalar);
            printf("Resultado producto punto: %f\n", dotP);
            printf("Tiempo transcurrido: %f\n", tend - tstart);
      }

      free(local_x);
      free(local_y);
      free(local_z);

      MPI_Finalize();

      return 0;
} /* main */

/*-------------------------------------------------------------------
 * Function:  Check_for_error
 * Purpose:   Check whether any process has found an error.  If so,
 *            print message and terminate all processes.  Otherwise,
 *            continue execution.
 * In args:   local_ok:  1 if calling process has found an error, 0
 *               otherwise
 *            fname:     name of function calling Check_for_error
 *            message:   message to print if there's an error
 *            comm:      communicator containing processes calling
 *                       Check_for_error:  should be MPI_COMM_WORLD.
 *
 * Note:
 *    The communicator containing the processes calling Check_for_error
 *    should be MPI_COMM_WORLD.
 */
void Check_for_error(
    int local_ok /* in */,
    char fname[] /* in */,
    char message[] /* in */,
    MPI_Comm comm /* in */)
{
      int ok;

      MPI_Allreduce(&local_ok, &ok, 1, MPI_INT, MPI_MIN, comm);
      if (ok == 0)
      {
            int my_rank;
            MPI_Comm_rank(comm, &my_rank);
            if (my_rank == 0)
            {
                  fprintf(stderr, "Proc %d > In %s, %s\n", my_rank, fname,
                          message);
                  fflush(stderr);
            }
            MPI_Finalize();
            exit(-1);
      }
} /* Check_for_error */

/*-------------------------------------------------------------------
 * Function:  Read_n
 * Purpose:   Get the order of the vectors from stdin on proc 0 and
 *            broadcast to other processes.
 * In args:   my_rank:    process rank in communicator
 *            comm_sz:    number of processes in communicator
 *            comm:       communicator containing all the processes
 *                        calling Read_n
 * Out args:  n_p:        global value of n
 *            local_n_p:  local value of n = n/comm_sz
 *
 * Errors:    n should be positive and evenly divisible by comm_sz
 */
void Read_n(
    int *n_p /* out */,
    int *local_n_p /* out */,
    int my_rank /* in  */,
    int comm_sz /* in  */,
    MPI_Comm comm /* in  */)
{
      int local_ok = 1;
      char *fname = "Read_n";

      if (my_rank == 0)
      {
            // printf("Ingrese el tamaño de los vectores:\n");
            // scanf("%d", n_p);
            *n_p = 100000000;
            printf("Tamaño de vectores: %d\n\n", *n_p);
      }
      MPI_Bcast(n_p, 1, MPI_INT, 0, comm);
      if (*n_p <= 0 || *n_p % comm_sz != 0)
            local_ok = 0;
      Check_for_error(local_ok, fname,
                      "n should be > 0 and evenly divisible by comm_sz", comm);
      *local_n_p = *n_p / comm_sz;
} /* Read_n */

/*-------------------------------------------------------------------
 * Function:  Allocate_vectors
 * Purpose:   Allocate storage for x, y, z, a and b
 * In args:   local_n:  the size of the local vectors
 *            comm:     the communicator containing the calling processes
 * Out args:  local_x_pp, local_y_pp, local_z_pp, local_a_pp, local_b_pp:  pointers to memory
 *               blocks to be allocated for local vectors
 *
 * Errors:    One or more of the calls to malloc fails
 */
void Allocate_vectors(
    double **local_x_pp,
    double **local_y_pp,
    double **local_z_pp,
    double **local_a_pp,
    double **local_b_pp,
    int local_n,
    MPI_Comm comm)
{
      int local_ok = 1;
      char *fname = "Allocate_vectors";

      *local_x_pp = malloc(local_n * sizeof(double));
      *local_y_pp = malloc(local_n * sizeof(double));
      *local_z_pp = malloc(local_n * sizeof(double));
      *local_a_pp = malloc(local_n * sizeof(double)); // Asignación para local_a
      *local_b_pp = malloc(local_n * sizeof(double)); // Asignación para local_b

      if (*local_x_pp == NULL || *local_y_pp == NULL ||
          *local_z_pp == NULL || *local_a_pp == NULL || *local_b_pp == NULL)
            local_ok = 0;
      Check_for_error(local_ok, fname, "Can't allocate local vector(s)", comm);
}
/* Allocate_vectors */

/*-------------------------------------------------------------------
 * Function:   Read_vector
 * Purpose:    Read a vector from stdin on process 0 and distribute
 *             among the processes using a block distribution.
 * In args:    local_n:  size of local vectors
 *             n:        size of global vector
 *             vec_name: name of vector being read (e.g., "x")
 *             my_rank:  calling process' rank in comm
 *             comm:     communicator containing calling processes
 * Out arg:    local_a:  local vector read
 *
 * Errors:     if the malloc on process 0 for temporary storage
 *             fails the program terminates
 *
 * Note:
 *    This function assumes a block distribution and the order
 *   of the vector evenly divisible by comm_sz.
 */
void Read_vector(
    double local_a[] /* out */,
    int local_n /* in  */,
    int n /* in  */,
    char vec_name[] /* in  */,
    int my_rank /* in  */,
    MPI_Comm comm /* in  */)
{

      double *a = NULL;
      int i;
      int local_ok = 1;
      char *fname = "Read_vector";

      if (my_rank == 0)
      {
            a = malloc(n * sizeof(double));
            if (a == NULL)
                  local_ok = 0;
            Check_for_error(local_ok, fname, "Can't allocate temporary vector",
                            comm);
            // printf("Enter the vector %s\n", vec_name);
            // fill vec with indez
            for (i = 0; i < n; i++)
            {
                  a[i] = (rand() % (10 - 1 + 1)) + 1;
            }
            MPI_Scatter(a, local_n, MPI_DOUBLE, local_a, local_n, MPI_DOUBLE, 0,
                        comm);
            free(a);
      }
      else
      {
            Check_for_error(local_ok, fname, "Can't allocate temporary vector",
                            comm);
            MPI_Scatter(a, local_n, MPI_DOUBLE, local_a, local_n, MPI_DOUBLE, 0,
                        comm);
      }
} /* Read_vector */

/*-------------------------------------------------------------------
 * Function:  Print_vector
 * Purpose:   Print a vector that has a block distribution to stdout
 * In args:   local_b:  local storage for vector to be printed
 *            local_n:  order of local vectors
 *            n:        order of global vector (local_n*comm_sz)
 *            title:    title to precede print out
 *            comm:     communicator containing processes calling
 *                      Print_vector
 *
 * Error:     if process 0 can't allocate temporary storage for
 *            the full vector, the program terminates.
 *
 * Note:
 *    Assumes order of vector is evenly divisible by the number of
 *    processes
 */
void Print_vector(
    double local_b[] /* in */,
    int local_n /* in */,
    int n /* in */,
    char title[] /* in */,
    int my_rank /* in */,
    MPI_Comm comm /* in */)
{

      double *b = NULL;
      int i;
      int local_ok = 1;
      char *fname = "Print_vector";

      if (my_rank == 0)
      {
            b = malloc(n * sizeof(double));
            if (b == NULL)
                  local_ok = 0;
            Check_for_error(local_ok, fname, "Can't allocate temporary vector",
                            comm);
            MPI_Gather(local_b, local_n, MPI_DOUBLE, b, local_n, MPI_DOUBLE,
                       0, comm);
            printf("%s\n", title);
            for (i = 0; i < 10; i++)
                  printf("%f ", b[i]);
            printf("\n...\n");
            for (i = n - 10; i < n; i++)
                  printf("%f ", b[i]);
            printf("\n\n");
            free(b);
      }
      else
      {
            Check_for_error(local_ok, fname, "Can't allocate temporary vector",
                            comm);
            MPI_Gather(local_b, local_n, MPI_DOUBLE, b, local_n, MPI_DOUBLE, 0,
                       comm);
      }
} /* Print_vector */

/*-------------------------------------------------------------------
 * Function:  Parallel_vector_sum
 * Purpose:   Add a vector that's been distributed among the processes
 * In args:   local_x:  local storage of one of the vectors being added
 *            local_y:  local storage for the second vector being added
 *            local_n:  the number of components in local_x, local_y,
 *                      and local_z
 * Out arg:   local_z:  local storage for the sum of the two vectors
 */
void Parallel_vector_sum(
    double local_x[] /* in  */,
    double local_y[] /* in  */,
    double local_z[] /* out */,
    int local_n /* in  */)
{
      int local_i;

      for (local_i = 0; local_i < local_n; local_i++)
            local_z[local_i] = local_x[local_i] + local_y[local_i];
} /* Parallel_vector_sum */

/*-------------------------------------------------------------------
 * Function:  Parallel_vector_dot
 * Purpose:   Calculates the dot product between two vectors that have been
 *            distributed among the processes.
 * In args:   local_x:  local storage of one of the vectors being used
 *            local_y:  local storage for the second vector being used
 *            local_n:  the number of components in local_x and local_y
 * Out arg:   dotP:  local storage for the dot product of the two vectors
 */
void Parallel_vector_dot(
    double local_x[] /* in  */,
    double local_y[] /* in  */,
    double *dotP /* out */,
    int local_n /* in  */)
{
      int local_i;
      *dotP = 0.0;

      for (local_i = 0; local_i < local_n; local_i++)
            *dotP += local_x[local_i] * local_y[local_i];
} /* Parallel_vector_dot */

/*-------------------------------------------------------------------
 * Function:  Parallel_vector_scalar
 * Purpose:   Multiplies each vector by a given scalar number.
 * In args:   local_x:  local storage for one of the vectors being used
 *            local_y:  local storage for the second vector being used
 *            local_n:  the number of components in local_x, local_y,
 *                      local_a, and local_b
 *            scalar:   scalar number to multiply each vector
 * Out args:  local_a:  local storage for the result of vector x
 *            local_b:  local storage for the result of vector y
 */
void Parallel_vector_scalar(
    double local_x[] /* in  */,
    double local_y[] /* in  */,
    double local_a[] /* out */,
    double local_b[] /* out */,
    int local_n /* in  */,
    int scalar)
{
      int local_i;

      for (local_i = 0; local_i < local_n; local_i++)
      {
            local_a[local_i] = scalar * local_x[local_i];
            local_b[local_i] = scalar * local_y[local_i];
      }
} /* Parallel_vector_scalar */
