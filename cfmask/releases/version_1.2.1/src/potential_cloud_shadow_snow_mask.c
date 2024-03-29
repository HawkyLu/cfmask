#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "2d_array.h"
#include "input.h"
#include "const.h"

#define MINSIGMA 1e-5
#define NO_VALUE 255

/******************************************************************************
MODULE:  potential_cloud_shadow_snow_mask

PURPOSE: Identify the cloud pixels, snow pixels, water pixels, clear land 
         pixels, and potential shadow pixels

RETURN: SUCCESS
        FAILURE

HISTORY:
Date        Programmer       Reason
--------    ---------------  -------------------------------------
3/15/2013   Song Guo         Original Development

NOTES: 
******************************************************************************/
int potential_cloud_shadow_snow_mask
(
    Input_t *input,             /*I: input structure */
    float cloud_prob_threshold, /*I: cloud probability threshold */
    float *ptm,                 /*O: percent of clear-sky pixels */
    float *t_templ,             /*O: percentile of low background temp */
    float *t_temph,             /*O: percentile of high background temp */
    unsigned char **cloud_mask, /*I/O: cloud pixel mask */
    unsigned char **shadow_mask,/*I/O: cloud shadow pixel mask */
    unsigned char **snow_mask,  /*I/O: snow pixel mask */
    unsigned char **water_mask, /*I/O: water pixel mask */
    bool verbose                /*I: value to indicate if intermediate messages 
                                     be printed */
)
{
    char errstr[MAX_STR_LEN];  /* error string */
    int nrows = input->size.l; /* number of rows */
    int ncols = input->size.s; /* number of columns */
    int ib = 0;                /* band index */
    int row =0;                /* row index */
    int col = 0;               /* column index */
    int mask_counter = 0;      /* mask counter */
    int clear_pixel_counter = 0;       /* clear sky pixel counter */
    int clear_land_pixel_counter = 0;  /* clear land pixel counter */
    float ndvi, ndsi;                  /* NDVI and NDSI values*/
    int16 *f_temp = NULL;   /* clear land temperature */
    int16 *f_wtemp = NULL;  /* clear water temperature */
    float visi_mean;        /* mean of visible bands */
    float whiteness = 0.0;  /* whiteness value*/
    float hot;     /* hot value for hot test */
    float lndptm;  /* clear land pixel percentage */
    float l_pt;    /* low percentile threshold */
    float h_pt;    /* high percentile threshold */
    float t_wtemp; /* high percentile water temperature */
    float **wfinal_prob = NULL;  /* final water pixel probabilty value */
    float **final_prob = NULL;   /* final land pixel probability value */
    float wtemp_prob;  /* water temperature probability value */
    int t_bright;      /* brightness test value for water */
    float brightness_prob;  /* brightness probability value */
    int t_buffer;  /* temperature test buffer */
    float temp_l;  /* difference of low/high tempearture percentiles */
    float temp_prob;  /* temperature probability */
    float vari_prob;  /* probability from NDVI, NDSI, and whiteness */
    float max_value;  /* maximum value */
    float *prob = NULL;  /* probability value */
    float *wprob = NULL; /* probability value */
    float clr_mask = 0.0;/* clear sky pixel threshold */
    float wclr_mask = 0.0;/* water pixel threshold */
    int16 *nir = NULL;   /* near infrared band (band 4) data */
    int16 *swir = NULL;  /* short wavelength infrared (band 5) data */
    float backg_b4;      /* background band 4 value */
    float backg_b5;      /* background band 5 value */
    int16 shadow_prob;   /* shadow probability */
    int status;          /* return value */
    int satu_bv;         /* sum of saturated bands 1, 2, 3 value */
    unsigned char mask;  /* mask used for 1 pixel */

    /* Dynamic memory allocation */
    unsigned char **clear_mask = NULL;
    unsigned char **clear_land_mask = NULL;
    unsigned char **clear_water_mask = NULL;

    clear_mask = (unsigned char **)allocate_2d_array(input->size.l, 
                 input->size.s, sizeof(unsigned char)); 
    clear_land_mask = (unsigned char **)allocate_2d_array(
                 input->size.l, input->size.s, sizeof(unsigned char)); 
    clear_water_mask = (unsigned char **)allocate_2d_array(
                 input->size.l, input->size.s, sizeof(unsigned char)); 
    if (clear_mask == NULL || clear_land_mask == NULL
        || clear_water_mask == NULL)
        RETURN_ERROR ("Allocating mask memory", "pcloud", FAILURE);
    
    if (verbose)
        printf("The first pass\n");
    /* Loop through each line in the image */
    for (row = 0; row < nrows; row++)
    {
        /* Print status on every 1000 lines */
        if (!(row%1000)) 
        {
            if (verbose)
            {
                printf ("Processing line %d\r",row);
                fflush (stdout);
            }
        }

        /* For each of the image bands */
        for (ib = 0; ib < input->nband; ib++)
        {
            /* Read each input reflective band -- data is read into
               input->buf[ib] */
            if (!GetInputLine(input, ib, row))
            {
                sprintf (errstr, "Reading input image data for line %d, "
                    "band %d", row, ib);
                RETURN_ERROR (errstr, "pcloud", FAILURE);
            }
        }

	/* For the thermal band */
	/* Read the input thermal band -- data is read into input->therm_buf */
	if (!GetInputThermLine(input, row))
        {
	    sprintf (errstr, "Reading input thermal data for line %d", row);
	    RETURN_ERROR (errstr, "pcloud", FAILURE);
	}

        for (col = 0; col < ncols; col++)
        {
            int ib;
            for (ib = 0; ib <  NBAND_REFL_MAX; ib++)
            {
                if (input->buf[ib][col] == input->meta.satu_value_ref[ib])
                input->buf[ib][col] = input->meta.satu_value_max[ib]; 
            }
            if (input->therm_buf[col] == input->meta.therm_satu_value_ref)
                input->therm_buf[col] = input->meta.therm_satu_value_max; 

            /* process non-fill pixels only */
            if (input->therm_buf[col] <= -9999 || input->buf[0][col] == -9999 
                || input->buf[1][col] == -9999 || input->buf[2][col] == -9999 
                || input->buf[3][col] == -9999 || input->buf[4][col] == -9999
                || input->buf[5][col] == -9999)
            {
                mask = 0;
            }
            else
            {
                mask = 1;
	        mask_counter++;
            }
  
            if ((input->buf[2][col] +  input->buf[3][col]) != 0 
                && mask == 1)
            {
                ndvi = (float)(input->buf[3][col] - input->buf[2][col]) / 
                    (float)(input->buf[3][col] + input->buf[2][col]);
            }
            else
                ndvi = 0.01;

            if ((input->buf[1][col] +  input->buf[4][col]) != 0 
                && mask == 1)
            {
                ndsi = (float)(input->buf[1][col] - input->buf[4][col]) / 
                (float)(input->buf[1][col] + input->buf[4][col]);
            }
            else
                ndsi = 0.01;

            /* Basic cloud test, equation 1 */
            if (((ndsi - 0.8) < MINSIGMA) && ((ndvi - 0.8) < MINSIGMA) && 
                (input->buf[5][col] > 300) && (input->therm_buf[col] < 2700))
            {
                 cloud_mask[row][col] = 1;
            }
            else
                 cloud_mask[row][col] = 0;

            /* It takes every snow pixels including snow pixel under thin 
               clouds or icy clouds, equation 20 */
            if (((ndsi - 0.15) > MINSIGMA) && (input->therm_buf[col] < 1000) &&
                (input->buf[3][col] > 1100) && (input->buf[1][col] > 1000))
            {
                snow_mask[row][col] = 1;
            }
            else 
                snow_mask[row][col] = 0;

            /* Zhe's water test (works over thin cloud), equation 5 */
            if (((((ndvi - 0.01) < MINSIGMA) && (input->buf[3][col] < 1100)) || 
                (((ndvi - 0.1) < MINSIGMA) && (ndvi > MINSIGMA) && 
                (input->buf[3][col] < 500))) && (mask == 1))
            {
                water_mask[row][col] = 1;
            }
            else 
                water_mask[row][col] = 0;
            if (mask == 0)
                water_mask[row][col] = NO_VALUE;

            /* visible bands flatness (sum(abs)/mean < 0.6 => brigt and dark 
               cloud), equation 2 */
            if (cloud_mask[row][col] == 1 && mask == 1)
            {
                visi_mean = (float)(input->buf[0][col] + input->buf[1][col] +
                    input->buf[2][col]) / 3.0;
                if (visi_mean != 0)
                {
                    whiteness = ((fabs((float)input->buf[0][col] - visi_mean) + 
                        fabs((float)input->buf[1][col] - visi_mean) +
                        fabs((float)input->buf[2][col] - visi_mean))) / visi_mean;
                }
                else
                    whiteness = 100.0; /* Just put a large value to remove them 
                                          from cloud pixel identification */
            }         

            /* Update cloud_mask,  if one visible band is saturated, 
               whiteness = 0, due to data type conversion, pixel value
               difference of 1 is possible */
            if ((input->buf[0][col] >= (input->meta.satu_value_max[0]-1)) ||
	        (input->buf[1][col] >= (input->meta.satu_value_max[1]-1)) ||
	        (input->buf[2][col] >= (input->meta.satu_value_max[2]-1)))
            {
                whiteness = 0.0;
                satu_bv = 1;
            }
            else
            {
                satu_bv = 0;
            }

            if (cloud_mask[row][col] == 1 && (whiteness - 0.7) < MINSIGMA)
                cloud_mask[row][col] = 1; 
            else
                cloud_mask[row][col] = 0;

            /* Haze test, equation 3 */
            hot = (float)input->buf[0][col] - 0.5 * (float)input->buf[2][col] 
                 - 800.0;
            if (cloud_mask[row][col] == 1 && (hot > MINSIGMA || satu_bv == 1))
                cloud_mask[row][col] = 1;
            else
                cloud_mask[row][col] = 0;

            /* Ratio 4/5 > 0.75 test, equation 4 */
            if (cloud_mask[row][col] == 1 && input->buf[4][col] != 0)
            {
                if ((float)input->buf[3][col]/(float)(input->buf[4][col]) - 0.75 
                    > MINSIGMA)
                {
                    cloud_mask[row][col] = 1;
                }
                else
                    cloud_mask[row][col] = 0;
            }
            else
                cloud_mask[row][col] = 0;
     
            /* Test whether use thermal band or not */
            if (cloud_mask[row][col] == 0 && mask == 1)
            {
                clear_mask[row][col] = 1;
	        clear_pixel_counter++;
            }
            else
                clear_mask[row][col] = 0;

            if (water_mask[row][col] == 0 && clear_mask[row][col] == 1)
            {
                clear_land_mask[row][col] = 1;
                clear_land_pixel_counter++;
	        clear_water_mask[row][col] = 0;
              
            }
            else if (water_mask[row][col] == 1 && clear_mask[row][col] == 1)
            {
                clear_water_mask[row][col] = 1;
                clear_land_mask[row][col] = 0;
            }
            else
            {
                clear_land_mask[row][col] = 0;
                clear_water_mask[row][col] = 0;
            }
        }
    }

    *ptm = 100. * ((float)clear_pixel_counter / (float)mask_counter);
    lndptm = 100. * ((float)clear_land_pixel_counter / (float)mask_counter);
    if (verbose)
        printf("*ptm, lndptm=%f,%f\n", *ptm, lndptm);

    if ((*ptm-0.1) <= MINSIGMA) /* No thermal test is needed, all clouds */
    {
        *t_templ = -1.0;
        *t_temph = -1.0; 
        for (row = 0; row < nrows; row++)
        {
	    for (col = 0; col <ncols; col++)
	    {	    
                /* All cloud */
	        if (cloud_mask[row][col] != 1)
	            shadow_mask[row][col] = 1;
                else
		    shadow_mask[row][col] = 0;
            }
        }
    }
    else
    {
        f_temp = malloc(input->size.l * input->size.s * sizeof(int16));
	f_wtemp = malloc(input->size.l * input->size.s * sizeof(int16));
	if (f_temp == NULL   || f_wtemp == NULL)
	{
	     sprintf (errstr, "Allocating temp memory");
	     RETURN_ERROR (errstr, "pcloud", FAILURE);
	}
    
        if (verbose)
            printf("The second pass\n");
        int16 f_temp_max = 0;
        int16 f_temp_min = 0;
        int16 f_wtemp_max = 0;
        int16 f_wtemp_min = 0;
        int index = 0;
        int index2 = 0;
        /* Loop through each line in the image */
        for (row = 0; row < nrows; row++)
        {
            /* Print status on every 1000 lines */
            if (!(row%1000)) 
            {
                if (verbose)
                {
	            printf ("Processing line %d\r", row);
                    fflush (stdout);
                }
	    }
	 
            /* For each of the image bands */
            for (ib = 0; ib < input->nband; ib++)
            {
                /* Read each input reflective band -- data is read into
                   input->buf[ib] */
                if (!GetInputLine(input, ib, row))
                {
                    sprintf (errstr, "Reading input image data for line %d, "
                        "band %d", row, ib);
                    RETURN_ERROR (errstr, "pcloud", FAILURE);
                }
            }

            /* For the thermal band, read the input thermal band  */
	    if (!GetInputThermLine(input, row))
	    {
	        sprintf (errstr,"Reading input thermal data for line %d", row);
	        RETURN_ERROR (errstr, "pcloud", FAILURE);
	    }

            for (col =0; col < ncols; col++)
	    {
                if (input->buf[5][col] == input->meta.satu_value_ref[5])
                    input->buf[5][col] = input->meta.satu_value_max[5]; 
                if (input->therm_buf[col] == input->meta.therm_satu_value_ref)
                    input->therm_buf[col] = input->meta.therm_satu_value_max; 

                if ((lndptm-0.1) >= MINSIGMA)
	        {
                    /* get clear land temperature */
	            if (clear_land_mask[row][col] == 1)
	            {
		        f_temp[index] = input->therm_buf[col];
                        if (f_temp_max < f_temp[index])
                            f_temp_max = f_temp[index];
                        if (f_temp_min > f_temp[index])
                            f_temp_min = f_temp[index];
            	            index++;
	            }
                }
        	else
	        {
	            /*get clear water temperature */
	            if (clear_mask[row][col] == 1)
	            {
                        f_temp[index] = input->therm_buf[col];
                        if (f_temp_max < f_temp[index])
                            f_temp_max = f_temp[index];
                        if (f_temp_min > f_temp[index])
                            f_temp_min = f_temp[index];
	                index++;
	            }
	        }
                /* Equation 7 */
        	if (clear_water_mask[row][col] == 1)
	        {
	            f_wtemp[index2] = input->therm_buf[col];
                    if (f_wtemp[index2] > f_wtemp_max)
                        f_wtemp_max = f_wtemp[index2];
                    if (f_wtemp[index2] < f_wtemp_min)
                        f_wtemp_min = f_wtemp[index2];
                    index2++;
	        }
            }
        }
    
        /* Tempearture for snow test */
        l_pt = 0.175;
        h_pt = 1.0 - l_pt;
        /* 0.175 percentile background temperature (low) */
        status = prctile(f_temp, index, f_temp_min, f_temp_max, 100.0*l_pt, 
                         t_templ);
        if (status != SUCCESS)
        {
            sprintf (errstr, "Error calling prctile routine");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        /* 0.825 percentile background temperature (high) */
        status = prctile(f_temp, index, f_temp_min, f_temp_max, 100.0*h_pt, 
                         t_temph);
        if (status != SUCCESS)
        {
	    sprintf (errstr, "Error calling prctile routine");
	    RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        status = prctile(f_wtemp, index2, f_wtemp_min, f_wtemp_max, 
                         100.0*h_pt, &t_wtemp);
        if (status != SUCCESS)
        {
            sprintf (errstr, "Error calling prctile routine");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* Temperature test */
        t_buffer = 4*100;    
        *t_templ -= (float)t_buffer;
        *t_temph += (float)t_buffer;
        temp_l=*t_temph-*t_templ;

        /* Relase f_temp memory */
        free(f_wtemp);
        free(f_temp);
        f_wtemp = NULL;
        f_temp = NULL;

        wfinal_prob = (float **)allocate_2d_array(input->size.l, 
                 input->size.s, sizeof(float)); 
        final_prob = (float **)allocate_2d_array(input->size.l, 
                 input->size.s, sizeof(float)); 
        if (wfinal_prob == NULL   ||  final_prob == NULL)
        {
            sprintf (errstr, "Allocating prob memory");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        if (verbose)
            printf("The third pass\n");
        /* Loop through each line in the image */
        for (row = 0; row < nrows; row++)
        {
            /* Print status on every 1000 lines */
            if (!(row%1000)) 
            {
                if (verbose)
                {
                    printf ("Processing line %d\r",row);
                    fflush (stdout);
                }
            }

            /* For each of the image bands */
            for (ib = 0; ib < input->nband; ib++)
            {
                /* Read each input reflective band -- data is read into
                   input->buf[ib] */
                if (!GetInputLine(input, ib, row))
                {
                    sprintf (errstr, "Reading input image data for line %d, "
                        "band %d", row, ib);
                    RETURN_ERROR (errstr, "pcloud", FAILURE);
                }
            }

	    /* For the thermal band, data is read into input->therm_buf */
	    if (!GetInputThermLine(input, row))
            {
       	        sprintf (errstr, "Reading input thermal data for line %d", row);
	        RETURN_ERROR (errstr, "pcloud", FAILURE);
	    }

            /* Loop through each line in the image */
	    for (col = 0; col <ncols; col++)
	    {
                for (ib = 0; ib <  NBAND_REFL_MAX - 1; ib++)
                {
                    if (input->buf[ib][col] == input->meta.satu_value_ref[ib])
                       input->buf[ib][col] = input->meta.satu_value_max[ib]; 
                }
                if (input->therm_buf[col] == input->meta.therm_satu_value_ref)
                    input->therm_buf[col] = input->meta.therm_satu_value_max; 

                if (water_mask[row][col] == 1)
                {
	            /* Get cloud prob over water */
	            /* Temperature test over water */
                    wtemp_prob = (t_wtemp - (float)input->therm_buf[col]) / 
                                  400.0;
                    if (wtemp_prob < MINSIGMA)
                        wtemp_prob = 0.0;
	    
                    /* Brightness test (over water) */
	            t_bright = 1100;
	            brightness_prob = (float)input->buf[4][col] / 
                                      (float)t_bright;
	            if ((brightness_prob - 1.0) > MINSIGMA)
	                 brightness_prob = 1.0;
	            if (brightness_prob < MINSIGMA)
	                 brightness_prob = 0.0;
	    
                    /*Final prob mask (water), cloud over water probability */
	            wfinal_prob[row][col] =100.0 * wtemp_prob * brightness_prob;
                    final_prob[row][col] = 0.0;
                }
                else
                {	    
                    temp_prob=(*t_temph-(float)input->therm_buf[col]) / temp_l;
	            /* Temperature can have prob > 1 */
	            if (temp_prob < MINSIGMA)
	                temp_prob = 0.0;
	    
                    /* label the non-fill pixels */
                    if (input->therm_buf[col] <= -9999
                        || input->buf[0][col] == -9999
                        || input->buf[1][col] == -9999
                        || input->buf[2][col] == -9999
                        || input->buf[3][col] == -9999
                        || input->buf[4][col] == -9999
                        || input->buf[5][col] == -9999)
                    {
                        mask = 0;
                    }
                    else
                        mask = 1;

                    if ((input->buf[2][col] +  input->buf[3][col]) != 0 
                         && mask == 1)
                    {
                        ndvi = (float)(input->buf[3][col] -  
                             input->buf[2][col]) / 
                             (float)(input->buf[3][col] +  input->buf[2][col]);
                    }
                    else
                        ndvi = 0.01;

                    if ((input->buf[1][col] +  input->buf[4][col]) != 0 
                        && mask == 1)
                    {
                        ndsi = (float)(input->buf[1][col] -  
                            input->buf[4][col]) / 
                            (float)(input->buf[1][col] +  input->buf[4][col]);
                    }
                    else
                        ndsi = 0.01;

                    /* NDVI and NDSI should not be negative */
	            if (ndsi < MINSIGMA)
	                ndsi = 0.0;
	            if (ndvi < MINSIGMA)
	                ndvi = 0.0;
	    
                    visi_mean = (input->buf[0][col] + input->buf[1][col] +
                        input->buf[2][col]) / 3.0;
                    if (visi_mean != 0)
                    {
                        whiteness = (float)((fabs((float)input->buf[0][col] - 
                           visi_mean) + fabs((float)input->buf[1][col] - 
                           visi_mean) + fabs((float)input->buf[2][col] - 
                           visi_mean))) / visi_mean;
                    }
                    else
                        whiteness = 0.0;

                    /* If one visible band is saturated, whiteness = 0 */
                    if ((input->buf[0][col] >= (input->meta.satu_value_max[0]
                       - 1)) || 
                       (input->buf[1][col] >= (input->meta.satu_value_max[1]-1))
                       || (input->buf[2][col] >= (input->meta.satu_value_max[2]
                       -1)))
                    {
                        whiteness = 0.0;
                    }

                    /* Vari_prob=1-max(max(abs(NDSI),abs(NDVI)),whiteness); */
	            if ((ndsi - ndvi) > MINSIGMA) 
	                max_value = ndsi;
	            else
	                max_value = ndvi;
	            if ((whiteness - max_value) > MINSIGMA) 
	                max_value = whiteness;
	            vari_prob = 1.0 - max_value;

                    /*Final prob mask (land) */
	            final_prob[row][col] = 100.0 * (temp_prob * vari_prob);
                    wfinal_prob[row][col] = 0.0;
                }
            }
        }

        /* Allocate memory for prob */
        prob = malloc(input->size.l * input->size.s * sizeof(float));
        if(prob == NULL)
        {
            sprintf (errstr, "Allocating prob memory");
	    RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        float prob_max = 0.0;
        float prob_min = 0.0;
        int index3 = 0;
        for (row = 0; row < nrows; row++)
        {
	    for (col = 0; col <ncols; col++)
	    {	    
	        if (clear_land_mask[row][col] == 1)
	        {
		    prob[index3] = final_prob[row][col];
                    if ((prob[index3] - prob_max) > MINSIGMA)
                        prob_max = prob[index3];
                    if ((prob_min - prob[index3]) > MINSIGMA)
                        prob_min = prob[index3];
		    index3++;
	        }
            }
        }

        /*Dynamic threshold for land */
        if (index3 != 0)
        {
            status = prctile2(prob, index3, prob_min, prob_max, 100.0*h_pt, 
                              &clr_mask);
            if (status != SUCCESS)
            {
	        sprintf (errstr, "Error calling prctile2 routine");
	        RETURN_ERROR (errstr, "pcloud", FAILURE);
            }
        }
        else
        {
            clr_mask = 22.5; /* no clear land pixel, make clr_mask double of  
                                cloud_prob_threshold */
        }
        clr_mask += cloud_prob_threshold;

        /* Relase memory for prob */
        free(prob);
        prob = NULL;

        /* Allocate memory for wprob */
        wprob = malloc(input->size.l * input->size.s * sizeof(float));
        if(wprob == NULL)
        {
            sprintf (errstr, "Allocating wprob memory");
	    RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        float wprob_max = 0.0;
        float wprob_min = 0.0;
        int index4 = 0;
        for (row = 0; row < nrows; row++)
        {
	    for (col = 0; col <ncols; col++)
	    {	    
	        if (clear_water_mask[row][col] == 1)
	        {
		    wprob[index4] = wfinal_prob[row][col];
                    if ((wprob[index4] - wprob_max) > MINSIGMA)
                        wprob_max = wprob[index4];
                    if ((wprob_min - wprob[index4]) > MINSIGMA)
                        wprob_min = wprob[index4];
		    index4++;
	        }
            }
        }

        /*Dynamic threshold for water */
        if (index4 != 0)
        {
            status = prctile2(wprob, index4, wprob_min, wprob_max, 100.0*h_pt, 
                              &wclr_mask);
            if (status != SUCCESS)
            {
	        sprintf (errstr, "Error calling prctile2 routine");
	        RETURN_ERROR (errstr, "pcloud", FAILURE);
            }
        }
        else
        {
            wclr_mask = 27.5; /* no clear water pixel, make wclr_mask 27.5 */
        }
        wclr_mask += cloud_prob_threshold;

        /* Relase memory for wprob */
        free(wprob);
        wprob = NULL;
	     	    
        if (verbose)
        {
            printf("pcloud probability threshold (land) = %.2f\n", clr_mask);
            printf("pcloud probability threshold (water) = %.2f\n", wclr_mask);
        }
        
        if (verbose)
            printf("The fourth pass\n");
        /* Loop through each line in the image */
        for (row = 0; row < nrows; row++)
        {
            /* Print status on every 1000 lines */
            if (!(row%1000)) 
            {
                if (verbose)
                {
                    printf ("Processing line %d\r",row);
                    fflush (stdout);
                }
            }

            /* For the thermal band, data is read into input->therm_buf */
	    if (!GetInputThermLine(input, row))
            {
	        sprintf (errstr, "Reading input thermal data for line %d", row);
	        RETURN_ERROR (errstr, "pcloud", FAILURE);
	    }

            for (col =0; col < ncols; col++)
            {
                if (input->therm_buf[col] == input->meta.therm_satu_value_ref)
                   input->therm_buf[col] = input->meta.therm_satu_value_max; 

                if ((cloud_mask[row][col] == 1 && final_prob[row][col] > 
                     clr_mask && water_mask[row][col] == 0) || 
                     (cloud_mask[row][col] == 1 && wfinal_prob[row][col] > 
                     wclr_mask && water_mask[row][col] == 1) 
                     || (input->therm_buf[col] < *t_templ + t_buffer - 3500))
                    cloud_mask[row][col] = 1;
                else
                    cloud_mask[row][col] = 0;
            }
        }

        /* Free the memory */
        status = free_2d_array((void **)wfinal_prob);
        if (status != SUCCESS)
        {
            sprintf (errstr, "Freeing memory: wfinal_prob\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);              
        }
        status = free_2d_array((void **)final_prob);
        if (status != SUCCESS)
        {
            sprintf (errstr, "Freeing memory: final_prob\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);              
        }

        /* Band 4 &5 flood fill */
        nir = calloc(input->size.l * input->size.s, sizeof(int16)); 
        swir = calloc(input->size.l * input->size.s, sizeof(int16)); 
        if (nir == NULL   || swir == NULL)
        {
            sprintf(errstr, "Allocating nir and swir memory");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* Open the intermediate file for writing */
        FILE *fd1;
        FILE *fd2;
        fd1 = fopen("b4.bin", "wb"); 
        if (fd1 == NULL)
        {
            sprintf (errstr, "Opening file: b4.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        fd2 = fopen("b5.bin", "wb"); 
        if (fd2 == NULL)
        {
            sprintf (errstr, "Opening file: b5.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        int16 nir_max = 0;
        int16 nir_min = 0;
        int16 swir_max = 0;
        int16 swir_min = 0;
        int idx = 0;
        int idx2 = 0;
        if (verbose)
            printf("The fifth pass\n");
        /* Loop through each line in the image */
        for (row = 0; row < nrows; row++)
        {
            /* Print status on every 1000 lines */
            if (!(row%1000)) 
            {
                if (verbose)
                {
                    printf ("Processing line %d\r",row);
                    fflush (stdout);
                }
            }

            /* For each of the image bands */
            for (ib = 0; ib < input->nband; ib++)
            {
                /* Read each input reflective band -- data is read into
                   input->buf[ib] */
                if (!GetInputLine(input, ib, row))
                {
                    sprintf (errstr, "Reading input image data for line %d, "
                        "band %d", row, ib);
                    RETURN_ERROR (errstr, "pcloud", FAILURE);
                }
            }

            for (col =0; col < ncols; col++)
            {
                if (input->buf[3][col] == input->meta.satu_value_ref[3])
                   input->buf[3][col] = input->meta.satu_value_max[3]; 
                if (input->buf[4][col] == input->meta.satu_value_ref[4])
                   input->buf[4][col] = input->meta.satu_value_max[4]; 

                if (clear_land_mask[row][col] == 1)
	        {
                    nir[idx] = input->buf[3][col];
                    if (nir[idx] > nir_max)
                        nir_max = nir[idx];
                    if (nir[idx] < nir_min)
                        nir_min = nir[idx];
	            idx++;
	        }

                if (clear_land_mask[row][col] == 1)
	        {
                    swir[idx2] = input->buf[4][col];
                    if (swir[idx2] > swir_max)
                        swir_max = swir[idx2];
                    if (swir[idx2] < swir_min)
                        swir_min = swir[idx2];
	            idx2++;
	        }
            }

            /* Write out the intermediate file */
            status = fwrite(&input->buf[3][0], sizeof(int16), 
                     input->size.s, fd1);
            if (status != input->size.s)
            {
                sprintf (errstr, "Writing file: b4.bin\n");
                RETURN_ERROR (errstr, "pcloud", FAILURE);
            }
            status = fwrite(&input->buf[4][0], sizeof(int16), 
                     input->size.s, fd2);
            if (status != input->size.s)
            {
                sprintf (errstr, "Writing file: b5.bin\n");
                RETURN_ERROR (errstr, "pcloud", FAILURE);
            }
        }

        /* Close the intermediate file */
        status = fclose(fd1);
        if ( status )
        {
            sprintf (errstr, "Closing file: b4.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        status = fclose(fd2);
        if ( status )
        {
            sprintf (errstr, "Closing file: b5.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* Estimating background (land) Band 4 Ref */
        status = prctile(nir, idx + 1, nir_min, nir_max, 100.0*l_pt, &backg_b4);
        if (status != SUCCESS)
        {
            sprintf (errstr, "Calling prctile function\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);              
        }
        status = prctile(swir, idx2 + 1, swir_min, swir_max, 100.0*l_pt, &backg_b5);
        if (status != SUCCESS)
        {
            sprintf (errstr, "Calling prctile function\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);              
        }

        /* Release the memory */
        free(nir);
        free(swir);
        nir = NULL;
        swir = NULL;

        /* Write out the intermediate values */
        fd1 = fopen("b4_b5.txt", "w"); 
        if (fd1 == NULL)
        {
            sprintf (errstr, "Opening file: b4_b5.txt\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* Write out the intermediate file */
        fprintf(fd1, "%f\n", backg_b4);
        fprintf(fd1, "%f\n", backg_b5);
        fprintf(fd1, "%d\n", input->size.l);
        fprintf(fd1, "%d\n", input->size.s);

        /* Close the intermediate file */
        status = fclose(fd1);
        if ( status )
        {
            sprintf (errstr, "Closing file: b4_b5.txt\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* Call the fill minima routine to do image fill */
        status = system("run_fillminima.py");
        if (status != SUCCESS)
        {
            sprintf (errstr, "Running run_fillminima.py routine\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* Open the intermediate file for reading */
        fd1 = fopen("filled_b4.bin", "rb"); 
        if (fd1 == NULL)
        {
            sprintf (errstr, "Opening file: filled_b4.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        fd2 = fopen("filled_b5.bin", "rb"); 
        if (fd2 == NULL)
        {
            sprintf (errstr, "Opening file: filled_b5.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* May need allocate two memory for new band 4 and 5 */
        int16 *new_nir;
        int16 *new_swir;   

        new_nir = (int16 *)malloc(input->size.s * sizeof(int16)); 
        new_swir = (int16 *)malloc(input->size.s * sizeof(int16)); 
        if (new_nir == NULL  || new_swir == NULL)
        {
            sprintf (errstr, "Allocating new_nir/new_swir memory");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        if (verbose)
            printf("The sixth pass\n");
        for (row = 0; row < nrows; row++)
        {
            /* Print status on every 1000 lines */
            if (!(row%1000)) 
            {
                if (verbose)
                {
                    printf ("Processing line %d\r",row);
                    fflush (stdout);
                }
            }

            /* For each of the image bands */
            for (ib = 0; ib < input->nband; ib++)
            {
                /* Read each input reflective band -- data is read into
                   input->buf[ib] */
                if (!GetInputLine(input, ib, row))
                {
                    sprintf (errstr, "Reading input image data for line %d, "
                        "band %d", row, ib);
                    RETURN_ERROR (errstr, "pcloud", FAILURE);
                }
            }

            /* For the thermal band, data is read into input->therm_buf */
	    if (!GetInputThermLine(input, row))
            {
	        sprintf (errstr, "Reading input thermal data for line %d", row);
	        RETURN_ERROR (errstr, "pcloud", FAILURE);
	    }

            /* Read out the intermediate file */
            fread(&new_nir[0], sizeof(int16), input->size.s, fd1);
            fread(&new_swir[0], sizeof(int16), input->size.s, fd2);

            for (col = 0; col < ncols; col++)
            {
                if (input->buf[3][col] == input->meta.satu_value_ref[3])
                    input->buf[3][col] = input->meta.satu_value_max[3]; 
                if (input->buf[4][col] == input->meta.satu_value_ref[4])
                    input->buf[4][col] = input->meta.satu_value_max[4]; 

                /* process non-fill pixels only */
                if (input->therm_buf[col] <= -9999 || input->buf[0][col] == -9999 
                    || input->buf[1][col] == -9999 || input->buf[2][col] == -9999 
                    || input->buf[3][col] == -9999 || input->buf[4][col] == -9999
                    || input->buf[5][col] == -9999)
                    mask = 0;
                else
                    mask = 1;

                if (mask == 1)
                { 
                     new_nir[col] -= input->buf[3][col];
                     new_swir[col] -= input->buf[4][col];

                     if (new_nir[col] < new_swir[col])
	                 shadow_prob = new_nir[col];
                     else
	                 shadow_prob = new_swir[col];

                     if (shadow_prob > 200)
	                 shadow_mask[row][col] = 1;
                     else
	                 shadow_mask[row][col] = 0;
                }
                else
                {
        	    cloud_mask[row][col] = NO_VALUE;
	            shadow_mask[row][col] = NO_VALUE;                 
                }

                /* refine Water mask (no confusion water/cloud) */
                if (water_mask[row][col] == 1 && cloud_mask[row][col] == 0)
                    water_mask[row][col] = 1;
                else
	            water_mask[row][col] = 0;
            }
        }

        /* Release the memory */ 
        free(new_nir);
        free(new_swir);
        new_nir = NULL;
        new_swir = NULL;

        /* Close the intermediate file */
        status = fclose(fd1);
        if ( status )
        {
            sprintf (errstr, "Closing file: filled_b4.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        status = fclose(fd2);
        if ( status )
        {
            sprintf (errstr, "Closing file: filled_b5.bin\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }

        /* Remove the intermediate files */
        status = system("rm b4_b5.txt");
        if (status != SUCCESS)
        {
            sprintf (errstr, "Removing b4_b5.txt file\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        status = system("rm b4.bin");
        if (status != SUCCESS)
        {
            sprintf (errstr, "Removing b4.bin file\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        status = system("rm b5.bin");
        if (status != SUCCESS)
        {
            sprintf (errstr, "Removing b5.bin file\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        status = system("rm filled_b4.bin");
        if (status != SUCCESS)
        {
            sprintf (errstr, "Removing filled_b4.bin file\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
        status = system("rm filled_b5.bin");
        if (status != SUCCESS)
        {
            sprintf (errstr, "Removing filled_b5.bin file\n");
            RETURN_ERROR (errstr, "pcloud", FAILURE);
        }
    }    
    status = free_2d_array((void **)clear_mask);
    if (status != SUCCESS)
    {
        sprintf (errstr, "Freeing memory: clear_mask\n");
        RETURN_ERROR (errstr, "pcloud", FAILURE);              
    }
    status = free_2d_array((void **)clear_land_mask);
    if (status != SUCCESS)
    {
        sprintf (errstr, "Freeing memory: clear_land_mask\n");
        RETURN_ERROR (errstr, "pcloud", FAILURE);              
    }
    status = free_2d_array((void **)clear_water_mask);
    if (status != SUCCESS)
    {
        sprintf (errstr, "Freeing memory: clear_water_mask\n");
        RETURN_ERROR (errstr, "pcloud", FAILURE);              
    }

    return SUCCESS;
    
}

