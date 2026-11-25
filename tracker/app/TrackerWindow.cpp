#include "TrackerWindow.h"

namespace tracker
{
namespace
{
constexpr int compactViewWidth = 380; // its height fits the page (fitCompactViewToPage)
constexpr int compactViewInitialHeight = 600;
constexpr int compactViewMinHeight = 200, compactViewMaxHeight = 1600;
constexpr int debugViewMinWidth = 340, debugViewMinHeight = 200, debugViewMaxWidth = 1200, debugViewMaxHeight = 2400;
constexpr int debugViewDefaultWidth = 900, debugViewDefaultHeight = 720;
constexpr int anySize = 1 << 15;

enum MenuItem { debuggingItem = 1, aboutItem, firstCameraItem = 100 }; // cameras: firstCameraItem + index
const char* const viewMenuName = "View";
const char* const cameraMenuName = "Camera";

// What the build puts next to the app: the page (ui/) and the models (models/).
juce::File appResourcesFolder()
{
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
   #if JUCE_MAC
    return app.getChildFile ("Contents/Resources");
   #else
    return app.getParentDirectory();
   #endif
}
const char* const aboutItemText = "About Agogica Tracker";
} // namespace

TrackerWindow::TrackerWindow()
    : DocumentWindow ("Agogica Tracker", juce::Colours::black, closeButton | minimiseButton),
      listenerPoseOutput (headtracking::defaultFile()),
      page (appResourcesFolder().getChildFile ("ui"), settings, listenerPoseOutput,
            [this] (int pageHeightPixels)
            {
                if (! isDebugView)
                    fitCompactViewToPage (pageHeightPixels);
            }),
      camera (settings.cameraName()),
      trackingLoop (appResourcesFolder().getChildFile ("models"), camera, listenerPoseOutput, page)
{
    listenerPoseOutput.setInverted (settings.inverted());

    setContentNonOwned (&page.component(), false);
    page.component().setSize (compactViewWidth, compactViewInitialHeight);
    setUsingNativeTitleBar (true);
    // Resizable once, here: switching it later recreates the native window, and WebView2
    // goes on drawing into the old one (a frozen page - measured on Windows). The compact
    // view is fixed by its size limits instead (fitCompactViewToPage).
    setResizable (true, false);

    if (const auto saved = settings.windowState(); saved.isEmpty() || ! restoreWindowStateFromString (saved))
        centreWithSize (compactViewWidth, compactViewInitialHeight);
    setDebugView (settings.debugView());
    setVisible (true);

   #if JUCE_MAC
    static const auto about = aboutMenu(); // macOS: About is in the app menu
    juce::MenuBarModel::setMacMainMenu (this, &about);
   #else
    setMenuBar (this);
   #endif
}

TrackerWindow::~TrackerWindow()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #else
    setMenuBar (nullptr);
   #endif
    saveBounds();
}

void TrackerWindow::closeButtonPressed() { juce::JUCEApplication::quit(); }

void TrackerWindow::setDebugView (bool on)
{
    if (on == isDebugView && isVisible())
        return;

    if (isVisible()) // not while the window is being set up: its bounds aren't the user's yet
        saveBounds();
    isDebugView = on;
    settings.setDebugView (on);

    if (on)
        showDebugViewWindow();
    else
        fitCompactViewToPage (page.component().getHeight()); // the page then reports its real height

    page.showDebugView (on);
    menuItemsChanged();
}

void TrackerWindow::showDebugViewWindow()
{
    setResizeLimits (debugViewMinWidth, debugViewMinHeight, debugViewMaxWidth, debugViewMaxHeight);

    const auto saved = settings.debugViewBounds();
    auto bounds = saved.isEmpty() ? getBounds().withSize (debugViewDefaultWidth, debugViewDefaultHeight) : saved;

    // On screen with its frame, clear of the taskbar / Dock (a size saved on a bigger display,
    // or at another scale, would hang off the edge).
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (bounds))
    {
        auto area = display->userBounds.getSmallestIntegerContainer();
        if (auto* peer = getPeer())
            if (const auto frame = peer->getFrameSizeIfPresent())
                area = frame->subtractedFrom (area);
        bounds = bounds.constrainedWithin (area);
    }
    setBounds (bounds);
}

// The compact window: `compactViewWidth` wide, as tall as the page, and fixed at that size.
void TrackerWindow::fitCompactViewToPage (int pageHeightPixels)
{
    setResizeLimits (1, 1, anySize, anySize);
    setContentComponentSize (compactViewWidth, juce::jlimit (compactViewMinHeight, compactViewMaxHeight, pageHeightPixels));

    // The limits apply to the window with its native frame (JUCE's peers add the frame
    // before constraining): limits of our own size alone shrink it by the frame.
    auto sizeWithFrame = getBounds();
    if (auto* peer = getPeer())
        if (const auto frame = peer->getFrameSizeIfPresent())
            sizeWithFrame = frame->addedTo (sizeWithFrame);
    setResizeLimits (sizeWithFrame.getWidth(), sizeWithFrame.getHeight(), sizeWithFrame.getWidth(), sizeWithFrame.getHeight());
}

void TrackerWindow::saveBounds()
{
    settings.setWindowState (getWindowStateAsString());
    if (isDebugView)
        settings.setDebugViewBounds (getBounds());
}

// macOS has About in the app menu (aboutMenu); elsewhere a Help menu holds it.
juce::StringArray TrackerWindow::getMenuBarNames()
{
   #if JUCE_MAC
    return { viewMenuName, cameraMenuName };
   #else
    return { viewMenuName, cameraMenuName, "Help" };
   #endif
}

juce::PopupMenu TrackerWindow::getMenuForIndex (int, const juce::String& menuName)
{
    if (menuName == cameraMenuName)
        return cameraMenu();
    if (menuName != viewMenuName)
        return aboutMenu();

    juce::PopupMenu view;
    view.addItem (debuggingItem, "Debugging", true, isDebugView);
    return view;
}

// The computer's cameras as they are now, the one in use ticked.
juce::PopupMenu TrackerWindow::cameraMenu()
{
    juce::PopupMenu menu;
    camerasInMenu = CameraFeed::availableCameras();
    const auto inUse = camera.cameraName();
    for (int index = 0; index < camerasInMenu.size(); ++index)
        menu.addItem (firstCameraItem + index, camerasInMenu[index], true, camerasInMenu[index] == inUse);
    if (camerasInMenu.isEmpty())
        menu.addItem (firstCameraItem, "No camera found", false);
    return menu;
}

juce::PopupMenu TrackerWindow::aboutMenu()
{
    juce::PopupMenu menu;
    menu.addItem (aboutItem, aboutItemText);
    return menu;
}

void TrackerWindow::menuItemSelected (int itemId, int)
{
    if (itemId == debuggingItem)
        setDebugView (! isDebugView);
    else if (itemId == aboutItem)
        page.showAbout();
    else if (juce::isPositiveAndBelow (itemId - firstCameraItem, camerasInMenu.size()))
    {
        const auto chosen = camerasInMenu[itemId - firstCameraItem]; // the list the user clicked in
        settings.setCameraName (chosen);
        camera.choose (chosen);
        menuItemsChanged();
    }
}
} // namespace tracker
