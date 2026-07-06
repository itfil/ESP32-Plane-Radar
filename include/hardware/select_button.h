#pragma once

/** GPIO setup; call once early in setup(). */
void selectButtonInit();

/** Call every loop() iteration; returns true once per confirmed short tap. */
bool selectButtonConsumeTap();
