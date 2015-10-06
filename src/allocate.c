
#include <stdlib.h>

#include "problem.h"
#include "allocate.h"

void allocate_memory(struct problem * problem, struct rankinfo local, struct mem * memory)
{
    // Allocate two copies of the angular flux
    // grid * angles * noct (8) * ng
    memory->angular_flux_in = malloc(sizeof(double)*local.nx*local.ny*local.nz*problem->nang*8*problem->ng);
    memory->angular_flux_out = malloc(sizeof(double)*local.nx*local.ny*local.nz*problem->nang*8*problem->ng);

    // Allocate edge arrays
    memory->flux_i = malloc(sizeof(double)*problem->nang*problem->ng*local.ny*local.nz);
    memory->flux_j = malloc(sizeof(double)*problem->nang*problem->ng*local.nx*local.nz);
    memory->flux_k = malloc(sizeof(double)*problem->nang*problem->ng*local.nx*local.ny);

    // Scalar flux
    // grid * ng
    memory->scalar_flux_in = malloc(sizeof(double)*local.nx*local.ny*local.nz*problem->ng);
    memory->scalar_flux_out = malloc(sizeof(double)*local.nx*local.ny*local.nz*problem->ng);

    //Scalar flux moments
    memory->scalar_flux_moments = malloc(sizeof(double)*(problem->cmom-1)*problem->ng*local.nx*local.ny*local.nz);

    // Quadrature weights
    memory->quad_weights = malloc(sizeof(double)*problem->nang);

    // Cosine coefficients
    memory->mu = malloc(sizeof(double)*problem->nang);
    memory->eta = malloc(sizeof(double)*problem->nang);
    memory->xi = malloc(sizeof(double)*problem->nang);

    // Scattering coefficient
    memory->scat_coeff = malloc(sizeof(double)*problem->nang*problem->cmom*8);

    // Material cross section
    memory->mat_cross_section = malloc(sizeof(double)*problem->ng);

    // Sources
    memory->fixed_source = malloc(sizeof(double)*problem->ng*local.nx*local.ny*local.nz);
    memory->outer_source = malloc(sizeof(double)*problem->cmom*problem->ng*local.nx*local.ny*local.nz);
    memory->inner_source = malloc(sizeof(double)*problem->cmom*problem->ng*local.nx*local.ny*local.nz);

    // Scattering matrix
    memory->scattering_matrix = malloc(sizeof(double)*problem->nmom*problem->ng*problem->ng);

    // Diamon difference co-efficients
    memory->dd_i = malloc(sizeof(double));
    memory->dd_j = malloc(sizeof(double)*problem->nang);
    memory->dd_k = malloc(sizeof(double)*problem->nang);

    // Mock velocities array
    memory->velocities = malloc(sizeof(double)*problem->ng);

    // Time absorption coefficient
    memory->velocity_delta = malloc(sizeof(double)*problem->ng);

    // Denominator array
    memory->denominator = malloc(sizeof(double)*problem->nang*problem->ng*local.nx*local.ny*local.nz);

}

void free_memory(struct mem * memory)
{
    free(memory->angular_flux_in);
    free(memory->angular_flux_out);
    free(memory->flux_i);
    free(memory->flux_j);
    free(memory->flux_k);
    free(memory->scalar_flux_in);
    free(memory->scalar_flux_out);
    free(memory->scalar_flux_moments);
    free(memory->quad_weights);
    free(memory->mu);
    free(memory->eta);
    free(memory->xi);
    free(memory->scat_coeff);
    free(memory->mat_cross_section);
    free(memory->fixed_source);
    free(memory->outer_source);
    free(memory->inner_source);
    free(memory->scattering_matrix);
    free(memory->dd_i);
    free(memory->dd_j);
    free(memory->dd_k);
    free(memory->velocities);
    free(memory->velocity_delta);
    free(memory->denominator);
}
