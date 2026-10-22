"""After every upload, sync the RTC so the 'today' count has a real date."""
Import("env")

env.AddPostAction("upload", env.VerboseAction(
    '"$PYTHONEXE" tools/settime.py "$UPLOAD_PORT"', "Setting RTC from host clock"))
