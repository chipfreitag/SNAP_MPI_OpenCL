
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#include "comms.h"
#include "input.h"
#include "problem.h"
#include "allocate.h"
#include "halos.h"
#include "source.h"
#include "sweep.h"
#include "scalar_flux.h"
#include "convergence.h"
#include "population.h"


#include "ocl_global.h"
#include "ocl_buffers.h"

/** \mainpage
* SNAP-MPI is a cut down version of the SNAP mini-app which allows us to
* investigate MPI decomposition schemes with various node-level implementations.
* In particular, this code will allow:
* \li Flat MPI
* \li Hybrid MPI+OpenMP (For CPU and larger core counts)
* \li OpenCL
*
* The MPI scheme used is KBA, expending into hybrid-KBA.
*/

extern double wtime(void);

int main(int argc, char **argv)
{
    int mpi_err = MPI_Init(&argc, &argv);
    check_mpi(mpi_err, "MPI_Init");

    double setup_time = wtime();

    int rank, size;
    mpi_err = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    check_mpi(mpi_err, "Getting MPI rank");

    mpi_err = MPI_Comm_size(MPI_COMM_WORLD, &size);
    check_mpi(mpi_err, "Getting MPI size");

    struct problem problem;

    if (rank == 0)
    {
        // Check for two files on CLI
        if (argc != 2)
        {
            fprintf(stderr, "Usage: ./snap snap.in\n");
            exit(-1);
        }
        read_input(argv[1], &problem);
        if ((problem.npex * problem.npey * problem.npez) != size)
        {
            fprintf(stderr, "Input error: wanted %d ranks but executing with %d\n", problem.npex*problem.npey*problem.npez, size);
            exit(-1);
        }
        check_decomposition(&problem);

    }

    // Set dx, dy, dz, dt values
    problem.dx = problem.lx / (double)problem.nx;
    problem.dy = problem.ly / (double)problem.ny;
    problem.dz = problem.lz / (double)problem.nz;
    problem.dt = problem.tf / (double)problem.nsteps;

    // Broadcast the global variables
    broadcast_problem(&problem, rank);


    // Set up communication neighbours
    struct rankinfo rankinfo;
    setup_comms(&problem, &rankinfo);

    // Initlise the OpenCL
    struct context context;
    init_ocl(&context);
    struct buffers buffers;
    check_device_memory_requirements(&problem, &rankinfo, &context);
    allocate_buffers(&problem, &rankinfo, &context, &buffers);


    // Allocate the problem arrays
    struct memory memory;
    allocate_memory(&problem, &rankinfo, &memory);
    struct halo halos;
    allocate_halos(&problem, &rankinfo, &halos);

    // Set up problem
    init_quadrature_weights(&problem, &context, &buffers);
    calculate_cosine_coefficients(&problem, &context, &buffers, memory.mu, memory.eta, memory.xi);
    calculate_scattering_coefficients(&problem, &context, &buffers, memory.mu, memory.eta, memory.xi);
    init_material_data(&problem, &context, &buffers, memory.mat_cross_section);
    init_fixed_source(&problem, &rankinfo, &context, &buffers);
    init_scattering_matrix(&problem, &context, &buffers, memory.mat_cross_section);
    init_velocities(&problem, &context, &buffers);

    struct plane* planes;
    unsigned int num_planes;
    init_planes(&planes, &num_planes, &rankinfo);
    copy_planes(planes, num_planes, &context, &buffers);

    // Zero out the angular flux buffers
    for (int oct = 0; oct < 8; oct++)
    {
        zero_buffer(&context, buffers.angular_flux_in[oct], problem.nang*problem.ng*rankinfo.nx*rankinfo.ny*rankinfo.nz);
        zero_buffer(&context, buffers.angular_flux_out[oct], problem.nang*problem.ng*rankinfo.nx*rankinfo.ny*rankinfo.nz);
    }

    // Zero out the outer source, because later moments are +=
    zero_buffer(&context, buffers.outer_source, problem.cmom*problem.ng*rankinfo.nx*rankinfo.ny*rankinfo.nz);

    cl_int err = clFinish(context.queue);
    check_ocl(err, "Finish queue at end of setup");

    setup_time = wtime() - setup_time;
    printf("Setup took %lfs\n", setup_time);

    bool innerdone, outerdone;

    double simulation_time = wtime();

    //----------------------------------------------
    // Timestep loop
    //----------------------------------------------
    for (unsigned int t = 0; t < problem.nsteps; t++)
    {
        // Zero out the scalar flux and flux moments
        zero_buffer(&context, buffers.scalar_flux, problem.ng*rankinfo.nx*rankinfo.ny*rankinfo.nz);
        if (problem.cmom-1 > 0)
            zero_buffer(&context, buffers.scalar_flux_moments, (problem.cmom-1)*problem.ng*rankinfo.nx*rankinfo.ny*rankinfo.nz);

        // Swap angluar flux pointers (not for the first timestep)
        if (t > 0)
            swap_angular_flux_buffers(&buffers);

        //----------------------------------------------
        // Outers
        //----------------------------------------------
        for (unsigned int o = 0; o < problem.oitm; o++)
        {
            init_velocity_delta(&problem, &context, &buffers);
            calculate_dd_coefficients(&problem, &context, &buffers);
            calculate_denominator(&problem, &rankinfo, &context, &buffers);

            compute_outer_source(&problem, &rankinfo, &context, &buffers);

            // Get the scalar flux back
            copy_back_scalar_flux(&problem, &rankinfo, &context, &buffers, memory.old_outer_scalar_flux, CL_FALSE);

            //----------------------------------------------
            // Inners
            //----------------------------------------------
            for (unsigned int i = 0; i < problem.iitm; i++)
            {
                compute_inner_source(&problem, &rankinfo, &context, &buffers);

                // Get the scalar flux back
                copy_back_scalar_flux(&problem, &rankinfo, &context, &buffers, memory.old_inner_scalar_flux, CL_FALSE);


                // Sweep each octant in turn
                int octant, istep, jstep, kstep;

                // Octant 1
                octant = 0;
                istep = -1;
                jstep = -1;
                kstep = -1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Octant 2
                octant = 1;
                istep = +1;
                jstep = -1;
                kstep = -1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Octant 3
                octant = 2;
                istep = -1;
                jstep = +1;
                kstep = -1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Octant 4
                octant = 3;
                istep = +1;
                jstep = +1;
                kstep = -1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Octant 5
                octant = 4;
                istep = -1;
                jstep = -1;
                kstep = +1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Octant 6
                octant = 5;
                istep = +1;
                jstep = -1;
                kstep = +1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Octant 7
                octant = 6;
                istep = -1;
                jstep = +1;
                kstep = +1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Octant 8
                octant = 7;
                istep = +1;
                jstep = +1;
                kstep = +1;
                recv_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);
                for (unsigned int p = 0; p < num_planes; p++)
                {
                    sweep_plane(octant, istep, jstep, kstep, p, planes, &problem, &rankinfo, &context, &buffers);
                }
                send_boundaries(octant, istep, jstep, kstep, &problem, &rankinfo, &memory, &context, &buffers);

                // Compute the Scalar Flux
                compute_scalar_flux(&problem, &rankinfo, &context, &buffers);
                if (problem.cmom-1 > 0)
                    compute_scalar_flux_moments(&problem, &rankinfo, &context, &buffers);

                // Get the new scalar flux back and check inner convergence
                copy_back_scalar_flux(&problem, &rankinfo, &context, &buffers, memory.scalar_flux, CL_TRUE);
                innerdone = inner_convergence(&problem, &rankinfo, &memory);
                if (innerdone)
                    break;

            }
            //----------------------------------------------
            // End of Inners
            //----------------------------------------------

            // Check outer convergence
            // We don't need to copy back the new scalar flux again as it won't have changed from the last inner
            double max_outer_diff;
            outerdone = outer_convergence(&problem, &rankinfo, &memory, &max_outer_diff) && innerdone;
            if (rankinfo.rank == 0)
                printf("Outer done? %d - diff %lf\n", outerdone, max_outer_diff);
            if (outerdone)
                break;

        }
        //----------------------------------------------
        // End of Outers
        //----------------------------------------------

        // Calculate particle population and print out the value
        double population;
        calculate_population(&problem, &rankinfo, &memory, &population);
        if (rankinfo.rank == 0)
        {
            // Get exponent of outer convergence criteria
            int places;
            frexp(100.0 * problem.epsi, &places);
            places = abs(floor(places / log2(10)));
            char format[100];
            sprintf(format, "Time %%d population: %%.%dlf\n", places);
            printf(format, t, population);
        }


        // Exit the time loop early if outer not converged
        if (!outerdone)
            break;

    }
    //----------------------------------------------
    // End of Timestep
    //----------------------------------------------

    err = clFinish(context.queue);
    check_ocl(err, "Finishing queue before simulation end");

    simulation_time = wtime() - simulation_time;
    printf("Simulation took %lfs\n", simulation_time);

    free_halos(&problem, &halos);
    free_memory(&memory);

    release_context(&context);
    finish_comms();
}
