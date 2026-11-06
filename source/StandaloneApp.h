#pragma once

namespace evil::standalone {
/** The standalone app's window has no title bar and no Options menu, so the
    editor offers what they did through these. All do nothing outside the app.
 */
void showAudioSettings();
void saveState();
void loadState();
/** Recreates the processor and the editor, so never call it from inside the
    editor's own event handling. */
void resetToDefaultState();
void minimiseWindow();
void closeWindow();
} // namespace evil::standalone
