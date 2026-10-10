// evil-snapshot: renders the plugin editor offscreen to PNG files, without opening a window.
//
//   evil-snapshot <output-directory>
//
// Writes editor.png (default size), editor@1.5x.png, and stretched.png, which simulates a
// window manager forcing the window to an odd size, to check the layout scales uniformly.

#include "PluginEditor.h"

#include <iostream>

namespace
{
bool save (juce::Component& c, const juce::File& file, float scale)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << " (" << image.getWidth() << "x"
              << image.getHeight() << ")" << std::endl;
    return ok;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc != 2)
    {
        std::cerr << "usage: evil-snapshot <output-directory>" << std::endl;
        return 1;
    }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    dir.createDirectory();

    evil::EvilAmplifierProcessor processor;
    processor.prepareToPlay (48000.0, 128);

    // Let the background model load finish; the editor reads the status when it opens.
    for (int i = 0; i < 500 && processor.getModels().getStatus().state != evil::ModelManager::State::ready; ++i)
        juce::Thread::sleep (10);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    bool ok = save (*editor, dir.getChildFile ("editor.png"), 1.0f);
    ok = save (*editor, dir.getChildFile ("editor@1.5x.png"), 1.5f) && ok;

    editor->setBoundsConstrained ({ 0, 0, 940, 970 }); // what the window manager did
    std::cout << "asked for 940x970, constrained to " << editor->getWidth() << "x" << editor->getHeight() << std::endl;
    ok = save (*editor, dir.getChildFile ("stretched.png"), 1.0f) && ok;

    editor.reset();
    processor.releaseResources();
    return ok ? 0 : 1;
}
