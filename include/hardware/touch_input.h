#pragma once

/** GPIO/touch controller setup; call once early in setup(). */
void touchInit();

/** Call every loop() iteration to poll and debounce touches. */
void touchPoll();

/** True once per debounced tap released over the radar circle (top 240x240). */
bool touchConsumeRangeTap();

/** True once per debounced tap released over the info panel (bottom strip). */
bool touchConsumeSelectTap();
