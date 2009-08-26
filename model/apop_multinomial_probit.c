/** \file apop_multinomial_probit.c */

/* Copyright (c) 2005--2008 by Ben Klemens.  Licensed under the modified GNU GPL v2; see COPYING and COPYING2.  */

#include "model.h"
#include "mapply.h"
#include "likelihoods.h"


/////////  Part I: Methods for the apop_category_settings struct

/** Convert a column of input data into factors, for use with \ref apop_probit, apop_logit, &c.
    
  \param d The input data set that you're probably about to run a regression on
  \param source_column The number of the column to convert to factors. As usual, the vector is -1.
  \param source_type 't' = text; anything else ('d' is a good choice) is numeric data.
 */
apop_category_settings *apop_category_settings_alloc(apop_data *d, int source_column, char source_type){
  apop_category_settings *out = malloc (sizeof(apop_category_settings));
    out->source_column = source_column;
    out->source_type = source_type;
    out->source_data = d;
    if (source_type == 't'){
        out->factors = apop_text_unique_elements(d, source_column);
        out->factors->vector = gsl_vector_alloc(d->textsize[0]);
        for (size_t i=0; i< out->factors->vector->size; i++)
            apop_data_set(out->factors, i, -1, i);
    } else{ //Save if statements by giving everything a text label.
        Apop_col(d, source_column, list);
        out->factors = apop_data_alloc(0,0,0);
        out->factors->vector = apop_vector_unique_elements(list);
        apop_text_alloc(out->factors, out->factors->vector->size, 1);
        for (size_t i=0; i< out->factors->vector->size; i++)
            apop_text_add(out->factors, i, 0, "%g", apop_data_get(out->factors, i, -1));
    }
    return out;
}

apop_category_settings *apop_category_settings_init(apop_category_settings in){
    return apop_category_settings_alloc(in.source_data, in.source_column, in.source_type);
}


void *apop_category_settings_copy(apop_category_settings *in){
  apop_category_settings *out = malloc (sizeof(apop_category_settings));
    *out = *in;
    out->factors = apop_data_copy(in->factors);
    return out;
}

void apop_category_settings_free(apop_category_settings *in){
    apop_data_free(in->factors);
    free(in);
}

/////////  Part II: plain old probit

/** If we find the apop_category group, then you've already converted
 something to factors and, I assume, put it in your data set's vector.

 If we don't find the apop_category group, then convert the first column of the matrix to categories, put it in the vector, and add a ones column.
 */
static void probit_prep(apop_data *d, apop_model *m){
  if (m->prepared) return;
  int       count;
  apop_data *factor_list;
    if (!d->vector){
        if (!Apop_settings_get_group(m, apop_category)){
            Apop_settings_add_group(m, apop_category, d, 0, 'd');
            Apop_col(d, 0, outcomes);
            d->vector = apop_vector_copy(outcomes);
            gsl_vector_set_all(outcomes, 1);
            if (d->names->column && d->names->column[0]){
                apop_name_add(d->names, d->names->column[0], 'v');
                sprintf(d->names->column[0], "1");
            }
        } else{
            int sourcecol = Apop_settings_get(m, apop_category, source_column);
            char sourcetype = Apop_settings_get(m, apop_category, source_type);
            if (sourcetype == 't')
                apop_text_to_factors(d, sourcecol, -1);
            else {
                Apop_col(d, sourcecol, outcomes);
                d->vector = apop_vector_copy(outcomes);
                gsl_vector_set_all(outcomes, 1);
            }
        }
            //else, I assume you already have the vector set up with something good.
    }

    void *mpt = m->prep; //and use the defaults.
    m->prep = NULL;
    apop_model_prep(d, m);
    m->prep = mpt;
    apop_name_stack(m->parameters->names, d->names, 'r', 'c');

    factor_list = Apop_settings_get(m, apop_category, factors);
    count = factor_list->textsize[0];
    m->parameters = apop_data_alloc(0, d->matrix->size2, count-1);
    apop_name_stack(m->parameters->names, d->names, 'r', 'c');
    for (int i=1; i< count; i++) 
        apop_name_add(m->parameters->names, factor_list->text[i][0], 'c');
    gsl_matrix_set_all(m->parameters->matrix, 1);
    snprintf(m->name, 100, "%s with %s as numeraire", m->name, factor_list->text[0][0]);
}

static double probit_log_likelihood(apop_data *d, apop_model *p){
  apop_assert(p->parameters,  0, 0,'s', "You asked me to evaluate an un-parametrized model.");
  long double	n, total_prob	= 0;
  apop_data *betadotx = apop_dot(d, p->parameters, 0, 0); 
	for(size_t i=0; i< d->matrix->size1; i++){
		n	        = gsl_cdf_gaussian_P(-apop_data_get(betadotx, i, 0),1);
        n = n ? n : 1e-10; //prevent -inf in the next step.
        n = n<1 ? n : 1-1e-10; 
        total_prob += apop_data_get(d, i, -1) ?  log(1-n): log(n);
	}
    apop_data_free(betadotx);
	return total_prob;
}

static void probit_dlog_likelihood(apop_data *d, gsl_vector *gradient, apop_model *p){
  apop_assert_void(p->parameters, 0,'s', "You asked me to evaluate an un-parametrized model.");
  long double	cdf, betax, deriv_base;
  apop_data *betadotx = apop_dot(d, p->parameters, 0, 0); 
    gsl_vector_set_all(gradient,0);
    for (size_t i=0; i< d->matrix->size1; i++){
        betax            = apop_data_get(betadotx, i, 0);
        cdf              = gsl_cdf_gaussian_P(-betax, 1);
        cdf = cdf ? cdf : 1e-10; //prevent -inf in the next step.
        cdf = cdf<1 ? cdf : 1-1e-10; 
        if (apop_data_get(d, i, -1))
            deriv_base      = gsl_ran_gaussian_pdf(-betax, 1) /(1-cdf);
        else
            deriv_base      = -gsl_ran_gaussian_pdf(-betax, 1) /  cdf;
        for (size_t j=0; j< d->matrix->size2; j++)
            apop_vector_increment(gradient, j, apop_data_get(d, i, j) * deriv_base);
	}
	apop_data_free(betadotx);
}


/** The Probit model.
 The first column of the data matrix this model expects is ones and zeros;
 the remaining columns are values of the independent variables. Thus,
 the model will return (data columns)-1 parameters.

\hideinitializer
\ingroup models
*/
apop_model apop_probit = {"Probit", .log_likelihood = probit_log_likelihood, 
    .score = probit_dlog_likelihood, .prep = probit_prep};


/////////  Part III: Multinomial Logit (plain logit is a special case)

static apop_data *multilogit_expected(apop_data *in, apop_model *m){
  apop_assert(m->parameters, NULL, 0, 's', "You're asking me to provide expected values of an "
                                           "un-parameterized model. Please run apop_estimate first.");
    apop_model_prep(in, m);
    gsl_matrix *params = m->parameters->matrix;
    apop_data *out = apop_data_alloc(in->matrix->size1, in->matrix->size1, params->size2+1);
    for (size_t i=0; i < in->matrix->size1; i ++){
        Apop_row(in, i, observation);
        Apop_row(out, i, outrow);
        double oneterm;
        int    bestindex  = 0;
        double bestscore  = 0;
        gsl_vector_set(outrow, 0, 1);
        for (size_t j=0; j < params->size2+1; j ++){
            if (j == 0)
                oneterm = 0;
            else {
                Apop_matrix_col(params, j-1, p);
                gsl_blas_ddot(observation, p, &oneterm);
                gsl_vector_set(outrow, j, exp(oneterm));
            }
            if (oneterm > bestscore){
                bestindex = j;
                bestscore = oneterm;
            }
        }
        double total = apop_sum(outrow);
        gsl_vector_scale(outrow, 1/total);
        apop_data_set(out, i, -1, bestindex);
    }
    apop_data *factor_list = Apop_settings_get(m, apop_category, factors);
    apop_name_add(out->names, factor_list->text[0][0], 'c');
    apop_name_stack(out->names, m->parameters->names, 'c');
    return out;
}


static double val;
static double unordered(double in){ return in == val; }
//static double ordered(double in){ return in >= val; }

/*
This is just a for loop that runs a probit on each row.
*/
static double multiprobit_log_likelihood(apop_data *d, apop_model *p){
  apop_assert(p->parameters,  0, 0,'s', "You asked me to evaluate an un-parametrized model.");
  static apop_model *spare_probit = NULL;
    if (!spare_probit){
        spare_probit = apop_model_copy(apop_probit);
        spare_probit->parameters = apop_data_alloc(0,0,0);
        spare_probit->prepared++;
    }
  static apop_data *working_data = NULL;
  if (!working_data)
      working_data = apop_data_alloc(0,0,0);

    working_data->matrix = d->matrix;

    gsl_vector *original_outcome = d->vector;
    double ll    = 0;
    double *vals = Apop_settings_get(p, apop_category, factors)->vector->data;
    for(size_t i=0; i < p->parameters->matrix->size2; i++){
        APOP_COL(p->parameters, i, param);
        val = vals[i];
        working_data->vector = apop_vector_map(original_outcome, unordered);
        //gsl_matrix_set_col(spare_probit->parameters->matrix, 0, param);
        spare_probit->parameters->matrix = apop_vector_to_matrix(param);
        ll  += apop_log_likelihood(working_data, spare_probit);
        gsl_vector_free(working_data->vector); //yup. It's inefficient.
        gsl_matrix_free(spare_probit->parameters->matrix);
    }
	return ll;
}


static size_t find_index(double in, double *m, size_t max){
  size_t i = 0;
    while (in !=m[i] && i<max) i++;
    return i;
}

/**

  The likelihood of choosing item $j$ is:
  \f$e^{x\beta_j}/ (\sum_i{e^{x\beta_i}})\f$

  so the log likelihood is 
  \f$x\beta_j  - ln(\sum_i{e^{x\beta_i}})\f$

  A nice trick used in the implementation: let \f$y_i = x\beta_i\f$.
  Then
\f[ln(\sum_i{e^{x\beta_i}}) = max(y_i) + ln(\sum_i{e^{y_i - max(y_i)}}).\f]

The elements of the sum are all now exp(something negative), so 
overflow won't happen, and if there's underflow, then that term
must not have been very important. [This trick is attributed to Tom
Minka, who implemented it in his Lightspeed Matlab toolkit.]
*/
static double multilogit_log_likelihood(apop_data *d, apop_model *p){
  apop_assert(p->parameters, 0, 0, 's', "You asked me to evaluate an un-parametrized model.");
  size_t i, j, index, choicect = p->parameters->matrix->size2;
  double* factor_list = Apop_settings_get(p, apop_category, factors)->vector->data;

    //Find X\beta_i for each row of X and each column of \beta.
    apop_data  *xbeta = apop_dot(d, p->parameters, 0, 0);

    //get the $x\beta_j$ numerator for the appropriate choice:
    long double ll    = 0;
    for(i=0; i < d->vector->size; i++){
        index   = find_index(gsl_vector_get(d->vector, i), factor_list, choicect);
        ll += (index==0) ? 1 : apop_data_get(xbeta, i, index-1);
    }

    //Get the denominator, using the subtract-the-max trick above.
    //Don't forget the implicit beta_0, fixed at zero (so we need to add exp(0-max)).
    for(j=0; j < xbeta->matrix->size1; j++){
        APOP_ROW(xbeta, j, thisrow);
        double max = gsl_vector_max(thisrow);
        gsl_vector_add_constant(thisrow, -max);
        apop_vector_exp(thisrow);
        ll -= max + log(apop_vector_sum(thisrow) +exp(-max)) ;
    }
    apop_data_free(xbeta);
	return ll;
}

apop_model *logit_estimate(apop_data *d, apop_model *m){
  apop_model *out = apop_maximum_likelihood(d, *m);

    //That's the whole estimation. But now we need to add a row of zeros
    //for the numeraire. This is just tedious matrix-shunting.
    apop_data *p = out->parameters;
    apop_data *zeros = apop_data_calloc(0, p->matrix->size1, 1);
    apop_data *newparams = apop_data_stack(zeros, p, 'c');
    apop_name_stack(newparams->names, p->names, 'r');

    apop_data_free(p);
    apop_data_free(zeros);
    out->parameters = newparams;
    return out;
}

/** The Logit model.
 The first column of the data matrix this model expects a number
 indicating the preferred category; the remaining columns are values of
 the independent variables. Thus, the model will return N-1 columns of
 parameters, where N is the number of categories chosen.

\hideinitializer
\ingroup models
*/
apop_model apop_logit = {"Logit", 
     .log_likelihood = multilogit_log_likelihood, 
     .expected_value=multilogit_expected, .prep = probit_prep};

/** The Multinomial Probit model.
 The first column of the data matrix this model expects a number
 indicating the preferred category; the remaining columns are values of
 the independent variables. Thus, the model will return N-1 columns of
 parameters, where N is the number of categories chosen.

\hideinitializer
\ingroup models
*/
apop_model apop_multinomial_probit = {"Multinomial probit",
     .log_likelihood = multiprobit_log_likelihood, .prep = probit_prep};