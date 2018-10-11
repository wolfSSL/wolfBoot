#ifndef TIMER_H_INCLUDED
#define TIMER_H_INCLUDED
int pwm_init(uint32_t clock, uint32_t threshold);
int timer_init(uint32_t clock, uint32_t scaler, uint32_t interval);

#endif
