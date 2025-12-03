#ifndef GPIO_H
#define GPIO_H
#include <Arduino.h>
// GPIO Pin Definitions

#define chA 29
#define chB 28
#define chC 27

// Function Prototypes
void gpio_initialise(void);
void disable_linear_encoder(void);
uint8_t readEncoderState(void);




#endif