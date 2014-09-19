void ufraw_kelvin_green_to_multipliers(double temperature, double green, double chanMulArray[3], const char *name);
void ufraw_multipliers_to_kelvin_green(double chanMulArray[3], double* temperature, double* green, const char *name);

void adobe_coeff (const char *name, int * black, int * maximum, float cam_mul[4], float pre_mul[4], float cmatrix[3][4], float rgb_cam[3][4]);
