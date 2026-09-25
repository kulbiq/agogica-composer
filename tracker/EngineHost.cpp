#include "EngineHost.h"

EngineHost::EngineHost (juce::File dir, std::function<void (const juce::var&)> e)
    : juce::Thread ("head-pose engine"), modelsDir (std::move (dir)), emit (std::move (e))
{
}

EngineHost::~EngineHost()
{
    setEngine ("mediapipe"); // stops the worker and the camera
    cancelPendingUpdate();
}

void EngineHost::setEngine (const juce::String& name)
{
    if (name == engineName)
        return;

    stopThread (5000); // an engine finishes its current frame first
    engineName = name;

    if (name == "mediapipe")
    {
        if (camera != nullptr)
            camera->removeListener (this);
        camera.reset();
        return;
    }

    if (camera == nullptr)
    {
        camera.reset (juce::CameraDevice::openDevice (0, 640, 480, 1280, 720, false));
        if (camera != nullptr)
            camera->addListener (this);
    }

    startThread();
}

void EngineHost::imageReceived (const juce::Image& image)
{
    {
        const juce::ScopedLock sl (frameLock);
        latestFrame = image;
    }
    frameReady.signal();
}

void EngineHost::post (juce::DynamicObject::Ptr payload)
{
    payload->setProperty ("engine", engineName);

    // Development: AGOGICA_TRACKER_LOG=/path logs every estimate.
    static const juce::File log (juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_LOG", {}));
    if (log != juce::File())
        log.appendText (juce::String (juce::Time::getMillisecondCounterHiRes(), 0) + " " + juce::JSON::toString (juce::var (payload.get()), true) + "\n");
    {
        const juce::ScopedLock sl (postLock);
        pending = std::move (payload);
    }
    triggerAsyncUpdate();
}

void EngineHost::handleAsyncUpdate()
{
    juce::DynamicObject::Ptr payload;
    {
        const juce::ScopedLock sl (postLock);
        payload = std::move (pending);
    }
    if (payload != nullptr)
        emit (juce::var (payload.get()));
}

void EngineHost::run()
{
    auto status = [this] (const juce::String& s, const juce::String& detail = {})
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("status", s);
        o->setProperty ("detail", detail);
        post (o);
    };

    if (camera == nullptr)
        return status ("error", "No camera");

    std::unique_ptr<engines::Engine> engine;
    try
    {
        status ("loading");
        engine = engines::makeEngine (engineName, modelsDir);
    }
    catch (const std::exception& e)
    {
        return status ("error", e.what());
    }

    bool first = true;
    status ("starting camera");

    while (! threadShouldExit())
    {
        if (! frameReady.wait (200))
            continue;

        juce::Image frame;
        {
            const juce::ScopedLock sl (frameLock);
            frame = std::exchange (latestFrame, {});
        }
        if (! frame.isValid())
            continue;

        if (first)
            status ("compiling", engine->provider()); // CoreML compiles on the first run
        first = false;

        try
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("status", "running");
            o->setProperty ("provider", engine->provider());

            if (const auto e = engine->estimate (frame))
            {
                o->setProperty ("found", true);
                o->setProperty ("yaw", e->yaw);
                o->setProperty ("rawYaw", e->rawYaw);
                o->setProperty ("pitch", e->pitch);
                o->setProperty ("roll", e->roll);
                o->setProperty ("confidence", e->confidence);
                o->setProperty ("box", juce::Array<juce::var> { e->box.getX(), e->box.getY(), e->box.getWidth(), e->box.getHeight() });
                o->setProperty ("detectMs", e->detectMs);
                o->setProperty ("poseMs", e->poseMs);
            }
            else
            {
                o->setProperty ("found", false);
            }
            post (o);
        }
        catch (const std::exception& e)
        {
            // One bad frame is "no head this frame", not the end of tracking.
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("status", "running");
            o->setProperty ("provider", engine->provider());
            o->setProperty ("found", false);
            o->setProperty ("warning", juce::String (e.what()));
            post (o);
        }
    }
}
