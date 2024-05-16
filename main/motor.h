#ifndef MOTOR_H
#define MOTOR_H

#define MOTOR_PIN_A_1A 1
#define MOTOR_PIN_A_2A 3
#define MOTOR_PIN_A_3A 41
#define MOTOR_PIN_A_4A 42

#define MOTOR_PIN_M1_EN 48
#define MOTOR_PIN_M2_EN 47
#define MOTOR_PIN_M3_EN 46
#define MOTOR_PIN_M4_EN 45
#define MOTOR_A_PIN_SEL ((1ULL << MOTOR_PIN_A_1A) | (1ULL << MOTOR_PIN_A_2A) | (1ULL << MOTOR_PIN_A_3A) | (1ULL << MOTOR_PIN_A_4A))
#define MOTOR_M_PIN_SEL ((1ULL << MOTOR_PIN_M1_EN) | (1ULL << MOTOR_PIN_M2_EN) | (1ULL << MOTOR_PIN_M3_EN) | (1ULL << MOTOR_PIN_M4_EN))

void motor_init();
void motor_stop();
void motor_forward(int speed, int time);
void motor_backward(int speed, int time);
void motor_left(double degrees);
void motor_right(double degrees);

#endif