#ifndef AVOIDANCE_H
#define AVOIDANCE_H

#include <stdint.h>

float GetCurrentYaw(void);
float GetDistanceAt(int angle);
void GetLeftRightDistances(float *left, float *right);
void TurnToTarget(float new_target);
void Maze_AbsoluteYaw(void);
void Avoidance_Stop(void);

#endif