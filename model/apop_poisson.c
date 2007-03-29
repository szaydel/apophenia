/** \file apop_poisson.c

  The poisson distribution.

Copyright (c) 2006 by Ben Klemens. Licensed under the GNU GPL version 2.
*/

//The default list. Probably don't need them all.
#include "model.h"
#include "stats.h"
#include "types.h"
#include "mapply.h"
#include "bootstrap.h"
#include "conversions.h"
#include "likelihoods.h"
#include "linear_algebra.h"
#include <gsl/gsl_rng.h>
#include <assert.h>
static double poisson_log_likelihood(const apop_data *beta, apop_data *d, void *);

static apop_estimate * poisson_estimate(apop_data * data,  void *parameters){
  apop_estimate 	*est= apop_estimate_alloc(data,apop_poisson, parameters);
  double		mean    = apop_matrix_mean(data->matrix);
	gsl_vector_set(est->parameters->vector, 0, mean);
	if (est->ep.uses.log_likelihood)
		est->log_likelihood	= poisson_log_likelihood(est->parameters, data, parameters);
	if (est->ep.uses.covariance)
		    est->covariance = apop_jackknife_cov(data, apop_poisson, &(est->ep));
	return est;
}

static apop_data *c  = NULL;

static double beta_zero_greater_than_x_constraint(const apop_data *beta, void * d, apop_data *returned_beta, void *v){
    //constraint is 0 < beta_2
  static apop_data *constraint = NULL;
    if (!constraint)constraint= apop_data_calloc(1,1,1);
    apop_data_set(constraint, 0, 0, 1);
    return apop_linear_constraint(beta->vector, constraint, 1e-3, returned_beta->vector);
}

static double ln_l;

static double apply_me(gsl_vector *v){
  int       k;
  double    x,
            llikelihood = 0;
    for (k=0; k< v->size; k++){
        x	= gsl_vector_get(v, k);
        if (x!=0)
            llikelihood    += ln_l *x - gsl_sf_lngamma(x+1);
    }
    return llikelihood;
}

static double poisson_log_likelihood(const apop_data *beta, apop_data *d, void* p){
  double        lambda      = gsl_vector_get(beta->vector, 0);
    ln_l 	= log(lambda);
  gsl_vector *  v           = apop_matrix_map(d->matrix, apply_me);
  double        llikelihood = apop_vector_sum(v);
    gsl_vector_free(v);
    return llikelihood - d->matrix->size1*d->matrix->size2*lambda;
}

static double poisson_p(const apop_data *beta, apop_data *d, void* v){
    return exp(poisson_log_likelihood(beta, d, v));
}

/** The derivative of the poisson distribution, for use in likelihood
 * minimization. You'll probably never need to call this directly.*/
static void poisson_dlog_likelihood(const apop_data *beta, apop_data *d, gsl_vector *gradient, void *p){
  double       	lambda  = gsl_vector_get(beta->vector, 0);
  gsl_matrix      *data	= d->matrix;
  float           d_a;
    d_a  = apop_matrix_sum(data)/lambda;
    d_a -= data->size1*data->size2;
    gsl_vector_set(gradient,0, d_a);
}

/* Just a wrapper for gsl_ran_poisson.

   cut & pasted from the GSL documentation:
\f$       
p(k) = {\mu^k \over k!} \exp(-\mu), \f$

where \f$k\geq 0\f$.
*/
static void poisson_rng(double *out, apop_data * a, apop_ep *p, gsl_rng* r){
    *out = gsl_ran_poisson(r, *a->vector->data);
}


/** The poisson distribution

Location of data in the grid is not relevant; send it a 1 x N, N x 1, or N x M and it will all be the same.

apop_poisson.estimate() is an MLE, so feed it appropriate \ref apop_ep.
  
\f$p(k) = {\mu^k \over k!} \exp(-\mu), \f$

\ingroup models
*/
apop_model apop_poisson = {"poisson", 1, 0, 0,
     poisson_estimate, poisson_p, poisson_log_likelihood, poisson_dlog_likelihood,
     beta_zero_greater_than_x_constraint, poisson_rng};