// Dev mock only: checks that every parameter write sits inside exactly one gesture
// (begin, changes, end), as hosts need to record automation. The plugin itself doesn't
// complain, so the browser has to.
export class GestureLedger {
  private open = new Set<string>();

  record(phase: string, target: number, param: string) {
    const key = `${target}/${param}`;
    if (phase === "begin") {
      if (this.open.has(key)) throw new Error(`gesture on ${key} began twice`);
      this.open.add(key);
    } else if (phase === "change" || phase === "end") {
      if (!this.open.has(key)) throw new Error(`${phase} on ${key} outside a gesture`);
      if (phase === "end") this.open.delete(key);
    } else throw new Error(`unknown phase "${phase}"`);
  }

  get openGestures() {
    return [...this.open];
  }
}
