
#pragma once

/** \file
* \brief Source update routines
*/

#include "global.h"
#include "problem.h"

/** \ingroup MEM
* @{
* \brief Index for outer source array */
#define OUTER_SOURCE_INDEX(m,g,i,j,k,cmom,ng,nx,ny) ((m)+((cmom)*(g))+((cmom)*(ng)*(i))+((cmom)*(ng)*(nx)*(j))+((cmom)*(ng)*(nx)*(ny)*(k)))
/**@}*/

/** \brief Computer the outer source
*
* First moment is set to fixed source. Subsequent momemnts
* use group-to-group scattering.
*/
void compute_outer_source(const struct problem * global, const struct rankinfo * local, const double * restrict fixed_source, const double * restrict scattering_matrix, const double * restrict scalar_flux, const double * restrict scalar_flux_moments, double * restrict outer_source);