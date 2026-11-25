import type { TrackingFrame } from "./bridge";
import { wrapDegrees } from "./dom";

const JITTER_WINDOW_MS = 2000;

// The debugging view's numbers: frames in the last second, frames without a head and how far
// the head has turned this session, and the model's frame-to-frame noise.
export class SessionStats {
  private recentCameraYaw: { timeMs: number; yawDegrees: number }[] = [];
  private framesThisSecond = 0;
  dropouts = 0; // frames without a head, this session
  turnedDegrees = { min: 0, max: 0 }; // what Composer heard, this session

  count(frame: TrackingFrame) {
    this.framesThisSecond++;
    if (!frame.found) this.dropouts++;

    if (frame.live) {
      this.turnedDegrees.min = Math.min(this.turnedDegrees.min, frame.listenerYawDegrees);
      this.turnedDegrees.max = Math.max(this.turnedDegrees.max, frame.listenerYawDegrees);
    }

    // The model's own readings only: a still head repeats the last one, which would read as no noise.
    if (frame.cameraYawDegrees !== undefined && !frame.reusedStillHead) {
      const now = performance.now();
      this.recentCameraYaw = [
        ...this.recentCameraYaw.filter((reading) => now - reading.timeMs < JITTER_WINDOW_MS),
        { timeMs: now, yawDegrees: frame.cameraYawDegrees },
      ];
    }
  }

  // Call once a second.
  takeFramesPerSecond() {
    const frames = this.framesThisSecond;
    this.framesThisSecond = 0;
    return frames;
  }

  // RMS of the yaw's change between the model's readings over the last 2 s (hold still to read its noise).
  jitterDegrees() {
    const readings = this.recentCameraYaw;
    const changes = readings.slice(1).map((reading, i) => wrapDegrees(reading.yawDegrees - readings[i].yawDegrees));
    return changes.length ? Math.sqrt(changes.reduce((sum, change) => sum + change * change, 0) / changes.length) : 0;
  }
}
