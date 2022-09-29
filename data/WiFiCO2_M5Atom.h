
typedef struct strCO2_reading {
  time_t time;
  uint16_t ppm;
} CO2_reading;

typedef struct strCO2_reading_sum {
  time_t time;
  uint16_t ppm_mean;
  uint16_t ppm_min;
  uint16_t ppm_max;
} CO2_reading_sum;
