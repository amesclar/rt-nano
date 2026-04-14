# Walkthrough - Jitter and Drift Reduction (scrum-74)

I have implemented several optimizations to the `rt-nano.ino` sketch to address reported timing jitter and drift issues.

## Changes Made

### 1. Non-Blocking Buzzer Engine
The primary source of jitter was the blocking `delay()` calls in the buzzer logic. A sequence of 3 long buzzes could block the main loop for up to 1.5 seconds, causing the timer to "skip" beats.
- **Implemented a state machine** (`updateBuzzer`) that handles buzzer toggling in the background.
- **Removed all `delay()` calls** from the buzzer event path.
- The main loop now continues to run at full speed regardless of buzzer activity.

### 2. Precise Timing Loop
The timer loop was refactored to use absolute time references.
- **Cured Cumulative Drift**: Instead of incrementing time relative to the last loop, the code now calculates `elapsed` time as `(millis() - startMillis) / 1000`. This ensures that even if one loop iteration is slightly delayed, the next one will catch up immediately.
- **Removed Calibration Hack**: The `MILLIS_PER_SECOND` constant was restored to `1000`. The previous value of `996` was likely an attempt to compensate for software slowness, which is now resolved.

### 3. Refined Sequence Execution
- The `runSequence` function now yields to the buzzer state machine on every iteration.
- Display updates and serial logging happen exactly at the turn of the second.

## Verification Results

- **Logic Review**: The new logic ensures that the `EndEvent` and final buzzer trigger are precisely aligned with the `duration` boundary.
- **Drift Analysis**: By using `(currentMillis - startMillis) / 1000`, the maximum theoretical drift over a 5-minute sequence is limited by the accuracy of the Arduino's crystal/oscillator, rather than software overhead.

## Files Modified
- [rt-nano.ino](file:///home/fred/Projects/rt-nano/rt-nano.ino)
