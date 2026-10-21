#pragma once

void playBeep();
void playLongBeep();
void playStartMelody();
void playShutdownMelody();
void playGPSEnableBeep();
void playGPSDisableBeep();
void playComboTune();
void play4ClickDown();
void play4ClickUp();
void playBoop();
void playChirp();
void playClick();
void playLongPressLeadUp();
bool playNextLeadUpNote();  // Play the next note in the lead-up sequence
void resetLeadUpSequence(); // Reset the lead-up sequence to start from beginning
#if MESHTASTIC_USER_BUTTON_ACTIONS
bool playHintNote(int index); // Play one note of the user button hint tune, false when past the end
#endif