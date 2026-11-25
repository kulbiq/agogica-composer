import { wrapDegrees } from "./dom";

const JITTER_WINDOW_MS = 2000;

// The debugging view's numbers: frames and dropouts in the last second, how far the head has
// turned this session, and the model's frame-to-frame noise.
export class SessionStats {
  private recentYaw: { timeMs: number; yawDegrees: number }[] = [];
  private framesThisSecond = 0;
  dropouts = 0; // frames without a head, this session
  turnedDegrees = { min: 0, max: 0 }; // what Composer heard, this session

  countFrame(yawDegrees: number | undefined) {
    this.framesThisSecond++;
    if (yawDegrees === undefined) {
      this.dropouts++;
      return;
    }
    const now = performance.now();
    this.recentYaw = [...this.recentYaw.filter((r) => now - r.timeMs < JITTER_WINDOW_MS), { timeMs: now, yawDegrees }];
  }

  countHeard(yawDegrees: number) {
    this.turnedDegrees.min = Math.min(this.turnedDegrees.min, yawDegrees);
    this.turnedDegrees.max = Math.max(this.turnedDegrees.max, yawDegrees);
  }

  // Call once a second.
  takeFramesPerSecond() {
    const frames = this.framesThisSecond;
    this.framesThisSecond = 0;
    return frames;
  }

  // RMS of the frame-to-frame yaw changes over the last 2 s (hold still to read the model's noise).
  jitterDegrees() {
    const changes = this.recentYaw.slice(1).map((r, i) => wrapDegrees(r.yawDegrees - this.recentYaw[i].yawDegrees));
    return changes.length ? Math.sqrt(changes.reduce((sum, change) => sum + change * change, 0) / changes.length) : 0;
  }
}
