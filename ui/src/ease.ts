// One step of an exponential ease: `perFrame` is the share of the remaining distance covered
// in 1/60 s, and `dt` the seconds since the last step. Stepping twice with half the time lands
// where one step does, so motion looks the same however often it is drawn.
export const easeToward = (current: number, goal: number, perFrame: number, dt: number) =>
  goal + (current - goal) * Math.pow(1 - perFrame, dt * 60);

// An angle in degrees as -180..180 (180 stays 180), to a tenth: listenerYaw's range and step.
export const wrapDegrees = (d: number) => {
  const wrapped = Math.round((((((d + 180) % 360) + 360) % 360) - 180) * 10) / 10;
  return wrapped === -180 && d > 0 ? 180 : wrapped;
};
