// One step of an exponential ease: `perFrame` is the share of the remaining distance covered
// in 1/60 s, and `dt` the seconds since the last step. Stepping twice with half the time lands
// where one step does, so motion looks the same however often it is drawn.
export const easeToward = (current: number, goal: number, perFrame: number, dt: number) =>
  goal + (current - goal) * Math.pow(1 - perFrame, dt * 60);
