#include "EngineHost.h"
#include "engines/StillGate.h"
#include "ProcessCpu.h"

EngineHost::EngineHost (juce::File dir, Emit e, Output o)
    : juce::Thread ("head-pose engine"), modelsDir (std::move (dir)), emit (std::move (e)), output (std::move (o))
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
    framesReceived.fetch_add (1, std::memory_order_relaxed);
    {
        const juce::ScopedLock sl (frameLock);
        latestFrame = image;
    }
    frameReady.signal();
}

void EngineHost::post (juce::DynamicObject::Ptr payload)
{
    payload->setProperty ("engine", engineName);

    // Development: AGOGICA_TRACKER_LOG=/path logs every estimate, after the time in ms (to 0.1 ms:
    // with 0 decimal places JUCE writes 1.81068e+08, a resolution of a second).
    static const juce::File log (juce::SystemStats::getEnvironmentVariable ("AGOGICA_TRACKER_LOG", {}));
    if (log != juce::File())
        log.appendText (juce::String (juce::Time::getMillisecondCounterHiRes(), 1) + " " + juce::JSON::toString (juce::var (payload.get()), true) + "\n");
    {
        const juce::ScopedLock sl (postLock);
        pending = std::move (payload);
    }
    triggerAsyncUpdate();
}

// A small JPEG of the frame for the page's preview (the page can't open the camera itself on
// Windows; it mirrors the picture), ~10 a second while the preview is open. Engine thread.
void EngineHost::postPreview (const juce::Image& frame)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (! previewWanted || now - lastPreview < 100.0 || frame.getWidth() <= 0)
        return;
    lastPreview = now;

    constexpr int width = 480; // sharp at 2x in the compact window's ~320 px
    const auto small = frame.rescaled (width, juce::jmax (1, width * frame.getHeight() / frame.getWidth()),
                                       juce::Graphics::mediumResamplingQuality);
    juce::MemoryOutputStream jpeg;
    juce::JPEGImageFormat format;
    format.setQuality (0.7f);
    if (! format.writeImageToStream (small, jpeg))
        return;

    {
        const juce::ScopedLock sl (postLock);
        pendingPreview = "data:image/jpeg;base64," + juce::Base64::toBase64 (jpeg.getData(), jpeg.getDataSize());
    }
    triggerAsyncUpdate();
}

void EngineHost::handleAsyncUpdate()
{
    juce::DynamicObject::Ptr payload;
    juce::String preview;
    {
        const juce::ScopedLock sl (postLock);
        payload = std::move (pending);
        preview = std::exchange (pendingPreview, {});
    }
    if (payload != nullptr)
        emit ("estimate", juce::var (payload.get()));
    if (preview.isNotEmpty())
        emit ("frame", preview);
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

    // Each frame's reading goes to Composer as it is: no filter, no smoothing (for now).
    engines::StillGate still;
    std::optional<engines::Estimate> last;
    const auto started = juce::Time::getMillisecondCounterHiRes();

    // Development monitoring, once a second in the "estimate" payload ("perf") and the log:
    // the whole process's CPU (in cores), frames the models ran on, frames a still head
    // reused, and camera frames the engine never got to (it was busy).
    struct
    {
        double time = 0, cpu = 0;
        int received = 0, full = 0, reused = 0;
    } perf { started / 1000.0, processCpuSeconds(), framesReceived.load(), 0, 0 };

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
        postPreview (frame);

        try
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("status", "running");
            o->setProperty ("provider", engine->provider());

            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto time = (now - started) / 1000.0;

            // A still head repeats the last reading instead of running the models: the page
            // and Composer's file go on as usual (the file is stale after 0.5 s).
            const bool reused = last.has_value() && still.unchanged (frame, time);
            if (! reused)
            {
                last = engine->estimate (frame);
                if (last)
                    still.reset (frame, last->box, time);
                else
                    still.clear();
            }
            const auto& e = last;
            output (e.has_value(), e ? e->yaw : 0.0f, e ? e->pitch : 0.0f, e ? e->roll : 0.0f);

            o->setProperty ("found", e.has_value());

            if (e)
            {
                o->setProperty ("yaw", e->yaw);
                o->setProperty ("pitch", e->pitch);
                o->setProperty ("roll", e->roll);
                o->setProperty ("confidence", e->confidence);
                o->setProperty ("box", juce::Array<juce::var> { e->box.getX(), e->box.getY(), e->box.getWidth(), e->box.getHeight() });
                if (! reused)
                {
                    o->setProperty ("detectMs", e->detectMs);
                    o->setProperty ("poseMs", e->poseMs);
                }
            }
            o->setProperty ("reused", reused);
            o->setProperty ("change", still.difference);

            ++(reused ? perf.reused : perf.full);
            if (const auto seconds = now / 1000.0 - perf.time; seconds >= 1.0)
            {
                const auto cpu = processCpuSeconds();
                const auto received = framesReceived.load (std::memory_order_relaxed);
                auto p = std::make_unique<juce::DynamicObject>();
                p->setProperty ("cpu", (cpu - perf.cpu) / seconds);
                p->setProperty ("models", perf.full / seconds);
                p->setProperty ("reused", perf.reused / seconds);
                p->setProperty ("dropped", juce::jmax (0, received - perf.received - perf.full - perf.reused) / seconds);
                o->setProperty ("perf", p.release());
                perf = { now / 1000.0, cpu, received, 0, 0 };
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
