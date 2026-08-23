#include <JuceHeader.h>
#include "MainComponent.h"
#include "UI/Palette.h"

class MidiCoachApplication : public juce::JUCEApplication
{
public:
    MidiCoachApplication() = default;

    const juce::String getApplicationName() override    { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        lookAndFeel.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
        lookAndFeel.setColour (juce::ResizableWindow::backgroundColourId, ui::palette::background);
        lookAndFeel.setColour (juce::TextButton::buttonColourId, ui::palette::panel);
        lookAndFeel.setColour (juce::ComboBox::backgroundColourId, ui::palette::panel);
        juce::Desktop::getInstance().setDefaultLookAndFeel (&lookAndFeel);

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::Desktop::getInstance().setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, ui::palette::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (860, 600, 4000, 3000);

            // On a scaled display the preferred size can be taller than the
            // screen, which leaves the keyboard off the bottom edge. Fit the
            // window to whatever room the desktop actually has.
            auto available = juce::Desktop::getInstance().getDisplays()
                                 .getPrimaryDisplay()->userArea;

            centreWithSize (juce::jmin (getWidth(),  available.getWidth()  - 40),
                            juce::jmin (getHeight(), available.getHeight() - 60));

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    juce::LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MidiCoachApplication)
