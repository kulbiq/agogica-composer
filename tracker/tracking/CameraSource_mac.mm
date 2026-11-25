#include "CameraSource.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

// Manual reference counting (the build doesn't use ARC): every alloc/copy here has its release.

// Receives the camera's video frames on the capture queue and hands each on as a juce::Image.
@interface AgogicaTrackerFrameReceiver : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
@public
    tracker::CameraSource::FrameCallback onFrame;
}
@end

@implementation AgogicaTrackerFrameReceiver

- (void) captureOutput: (AVCaptureOutput*) output didOutputSampleBuffer: (CMSampleBufferRef) sampleBuffer fromConnection: (AVCaptureConnection*) connection
{
    juce::ignoreUnused (output, connection);

    CVImageBufferRef pixels = CMSampleBufferGetImageBuffer (sampleBuffer);
    if (pixels == nullptr || CVPixelBufferLockBaseAddress (pixels, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
        return;

    const auto width = (int) CVPixelBufferGetWidth (pixels), height = (int) CVPixelBufferGetHeight (pixels);
    const auto* source = static_cast<const juce::uint8*> (CVPixelBufferGetBaseAddress (pixels));
    const auto sourceBytesPerRow = CVPixelBufferGetBytesPerRow (pixels);

    if (source != nullptr && width > 0 && height > 0)
    {
        // The frames are asked for as 32-bit BGRA, which is how JUCE lays out an ARGB image in
        // memory on this (little-endian) machine: rows are copied as they are. Camera pixels
        // are opaque, so "premultiplied" changes nothing.
        juce::Image frame (juce::Image::ARGB, width, height, false, juce::SoftwareImageType());
        {
            juce::Image::BitmapData destination (frame, juce::Image::BitmapData::writeOnly);
            const auto bytesPerRow = (size_t) width * 4;
            for (int y = 0; y < height; ++y)
                std::memcpy (destination.getLinePointer (y), source + (size_t) y * sourceBytesPerRow, bytesPerRow);
        }
        onFrame (frame);
    }

    CVPixelBufferUnlockBaseAddress (pixels, kCVPixelBufferLock_ReadOnly);
}

@end

namespace tracker
{
namespace
{
// Built-in cameras, external ones (USB), and iPhones offered as cameras.
NSArray<AVCaptureDevice*>* captureDevices()
{
    NSMutableArray<AVCaptureDeviceType>* types = [NSMutableArray arrayWithObject: AVCaptureDeviceTypeBuiltInWideAngleCamera];
    if (@available (macOS 14.0, *))
    {
        [types addObject: AVCaptureDeviceTypeExternal];
        [types addObject: AVCaptureDeviceTypeContinuityCamera];
    }
    else
    {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [types addObject: AVCaptureDeviceTypeExternalUnknown];
        #pragma clang diagnostic pop
    }

    return [[AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes: types
                                                                  mediaType: AVMediaTypeVideo
                                                                   position: AVCaptureDevicePositionUnspecified] devices];
}

class MacCameraSource final : public CameraSource
{
public:
    MacCameraSource (AVCaptureDeviceInput* input, FrameCallback callback)
    {
        receiver = [[AgogicaTrackerFrameReceiver alloc] init];
        receiver->onFrame = std::move (callback);
        captureQueue = dispatch_queue_create ("com.agogica.tracker.camera", DISPATCH_QUEUE_SERIAL);

        session = [[AVCaptureSession alloc] init];
        [session beginConfiguration];

        // VGA: the models need no more (the detector sees 320x192, the pose model a 128 px head).
        if ([session canSetSessionPreset: AVCaptureSessionPreset640x480])
            session.sessionPreset = AVCaptureSessionPreset640x480;
        else
            session.sessionPreset = AVCaptureSessionPresetMedium;

        if ([session canAddInput: input])
            [session addInput: input];

        output = [[AVCaptureVideoDataOutput alloc] init];
        output.videoSettings = @{ (NSString*) kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        output.alwaysDiscardsLateVideoFrames = YES; // the tracker wants the newest frame, not every frame
        [output setSampleBufferDelegate: receiver queue: captureQueue];
        if ([session canAddOutput: output])
            [session addOutput: output];

        [session commitConfiguration];
        startOnceAllowed();
    }

    ~MacCameraSource() override
    {
        // No frame may arrive after this returns: detach the receiver, then let the capture
        // queue finish what it is doing (starting the session, or a frame in hand).
        [output setSampleBufferDelegate: nil queue: nullptr];
        AVCaptureSession* stopping = session;
        auto released = isReleased;
        dispatch_sync (captureQueue, ^{ *released = true; [stopping stopRunning]; });

        [output release];
        [session release];
        [receiver release];
        dispatch_release (captureQueue);
    }

private:
    // startRunning blocks while the camera starts (a phone: seconds), so it runs on the capture
    // queue, not the message thread. The first time, macOS asks the user for the camera: the
    // answer can come after this source is gone, and must not start the camera then.
    void startOnceAllowed()
    {
        AVCaptureSession* starting = [session retain];
        dispatch_queue_t queue = captureQueue;
        dispatch_retain (queue);
        auto released = isReleased;

        auto start = ^(BOOL granted)
        {
            dispatch_async (queue, ^
            {
                if (granted && ! *released)
                    [starting startRunning];
                [starting release];
                dispatch_release (queue);
            });
        };

        if ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeVideo] == AVAuthorizationStatusAuthorized)
            start (YES);
        else
            [AVCaptureDevice requestAccessForMediaType: AVMediaTypeVideo completionHandler: start];
    }

    AgogicaTrackerFrameReceiver* receiver = nil;
    AVCaptureSession* session = nil;
    AVCaptureVideoDataOutput* output = nil;
    dispatch_queue_t captureQueue = nullptr;
    const std::shared_ptr<bool> isReleased = std::make_shared<bool> (false); // read and set on captureQueue only
};
} // namespace

std::vector<CameraDeviceInfo> CameraSource::availableCameras()
{
    std::vector<CameraDeviceInfo> cameras;
    @autoreleasepool
    {
        for (AVCaptureDevice* device in captureDevices())
            cameras.push_back ({ juce::String::fromUTF8 ([[device uniqueID] UTF8String]),
                                 juce::String::fromUTF8 ([[device localizedName] UTF8String]) });
    }
    return cameras;
}

std::unique_ptr<CameraSource> CameraSource::open (const juce::String& deviceId, FrameCallback onFrame)
{
    @autoreleasepool
    {
        for (AVCaptureDevice* device in captureDevices())
        {
            if (juce::String::fromUTF8 ([[device uniqueID] UTF8String]) != deviceId)
                continue;

            NSError* error = nil;
            AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice: device error: &error];
            return input != nil ? std::make_unique<MacCameraSource> (input, std::move (onFrame)) : nullptr;
        }
        return nullptr;
    }
}
} // namespace tracker
