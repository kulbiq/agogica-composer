export function elementById<T extends HTMLElement = HTMLElement>(id: string): T {
  const element = document.getElementById(id);
  if (!element) throw new Error(`The page has no #${id}`);
  return element as T;
}

export const wrapDegrees = (degrees: number) => ((((degrees + 180) % 360) + 360) % 360) - 180;
