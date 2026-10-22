"""M5StickCPlus 0.1.1 initializes LEDC at 0 Hz, rejected by Arduino 2.x.

Initialize the silent PWM channel at a valid frequency. Actual tones continue
to use the library's normal frequency and duration controls.
"""
from pathlib import Path

Import("env")

speaker = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "M5StickCPlus/src/utility/Speaker.cpp"
if speaker.exists():
    source = speaker.read_text()
    fixed = source.replace("ledcSetup(TONE_PIN_CHANNEL, 0, 13);", "ledcSetup(TONE_PIN_CHANNEL, 2000, 13);")
    if fixed != source:
        speaker.write_text(fixed)
