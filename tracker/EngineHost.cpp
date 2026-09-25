#include "EngineHost.h"
#include "engines/FilterTuner.h"

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

void EngineHost::setFilterParam (const juce::String& name, const juce::var& value)
{
    for (int i = 0; i < (int) engines::Param::count; ++i)
        if (name == engines::paramName ((engines::Param) i))
        {
            const juce::ScopedLock sl (pinsLock);
            pins[(size_t) i] = value.isVoid() || value.isUndefined() ? std::nullopt : std::optional<float> ((float) value);
        }
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

    // The temporal gate: 6DRepNet360's raw headings -> a stable output (see HeadFilter.h).
    engines::HeadFilter filter;
    engines::FilterTuner tuner;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    auto lastFrame = started;
    double lastRetune = 0;

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

            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto dt = (float) (now - std::exchange (lastFrame, now)) / 1000.0f;
            const auto e = engine->estimate (frame);

            std::optional<engines::Measurement> m;
            if (e)
                m = engines::Measurement { e->yaw, engines::hemisphereCertainty (e->pitch, e->roll) };

            // Adapt the parameters from the recent raw readings (and the user's pins). The
            // filter re-calibrates on setParams, so apply them at most once a second.
            const auto time = (now - started) / 1000.0;
            tuner.add (time, e ? std::optional<float> (e->yaw) : std::nullopt, m ? m->confidence : 0.0f);
            std::array<std::optional<float>, (size_t) engines::Param::count> currentPins;
            {
                const juce::ScopedLock sl (pinsLock);
                currentPins = pins;
            }
            const auto params = tuner.tuned (time, currentPins);
            if (time - lastRetune >= 1.0)
            {
                filter.setParams (params);
                lastRetune = time;
            }

            const auto s = filter.update (m, dt);

            auto parameters = std::make_unique<juce::DynamicObject>();
            for (int i = 0; i < (int) engines::Param::count; ++i)
            {
                const auto p = (engines::Param) i;
                auto entry = std::make_unique<juce::DynamicObject>();
                entry->setProperty ("value", p == engines::Param::confirmFrames ? (float) filter.getParams().confirmFrames
                                                                                  : engines::field (filter.getParams(), p));
                entry->setProperty ("pinned", currentPins[(size_t) i].has_value());
                parameters->setProperty (engines::paramName (p), entry.release());
            }
            o->setProperty ("params", parameters.release());

            // "found" = the gate is tracking (it holds through short losses); raw values only
            // when this frame had a head.
            o->setProperty ("found", s.tracking);
            o->setProperty ("output", s.output);
            o->setProperty ("estimate", s.estimate);
            o->setProperty ("certainty", s.certainty);
            o->setProperty ("reacquiring", s.reacquiring);

            if (e)
            {
                o->setProperty ("measured", true);
                o->setProperty ("yaw", e->yaw);
                o->setProperty ("rawYaw", e->rawYaw);
                o->setProperty ("pitch", e->pitch);
                o->setProperty ("roll", e->roll);
                o->setProperty ("confidence", m->confidence);
                o->setProperty ("box", juce::Array<juce::var> { e->box.getX(), e->box.getY(), e->box.getWidth(), e->box.getHeight() });
                o->setProperty ("detectMs", e->detectMs);
                o->setProperty ("poseMs", e->poseMs);
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
