/**
 * Keyed promise-chain locks with shared and exclusive keys (a per-key readers/writer lock).
 *
 * A task registered with `exclusive` keys runs after every task registered earlier with any of
 * those keys (shared or exclusive), and before any later one. A task holding a key `shared`
 * only waits for earlier exclusive holders of that key, so many shared holders run
 * concurrently. Keys are captured synchronously at call time (FIFO, no starvation), so tasks
 * never deadlock as long as a running task does not itself wait for a task it scheduled on an
 * overlapping key.
 *
 * The Vault uses it like this: a file operation holds the file exclusively and its ancestor
 * folders shared (operations on different files of one folder overlap); a folder operation
 * holds the folder exclusively (it waits for everything inside and blocks later operations).
 */

const noop = (): void => {};

/** Keys of one task. A key listed in both sets is exclusive. */
export interface LockSpec {
  exclusive: readonly string[];
  shared?: readonly string[];
}

interface KeyState {
  /** Completion of the newest exclusive holder (it waited for everything before it). */
  exclusive: Promise<void> | null;
  /** Completions of the shared holders registered after `exclusive`. */
  shared: Set<Promise<void>>;
}

export class KeyedLock {
  private readonly states = new Map<string, KeyState>();

  /** Number of keys that currently have queued or running tasks. */
  get size(): number {
    return this.states.size;
  }

  /** True while any task holds or waits for one of `keys`. */
  isBusy(keys: readonly string[]): boolean {
    return keys.some((k) => this.states.has(k));
  }

  /** Run `task` exclusively with respect to all other tasks sharing any key. */
  run<T>(keys: readonly string[], task: () => Promise<T> | T): Promise<T> {
    return this.runWith({ exclusive: keys }, task);
  }

  /** Run `task` holding `spec.exclusive` exclusively and `spec.shared` shared. */
  runWith<T>(spec: LockSpec, task: () => Promise<T> | T): Promise<T> {
    const exclusive = [...new Set(spec.exclusive)];
    const exclusiveSet = new Set(exclusive);
    const shared = [...new Set(spec.shared ?? [])].filter((k) => !exclusiveSet.has(k));
    const waits: Promise<void>[] = [];
    for (const k of exclusive) {
      const s = this.states.get(k);
      if (!s) continue;
      if (s.exclusive) waits.push(s.exclusive);
      for (const p of s.shared) waits.push(p);
    }
    for (const k of shared) {
      const s = this.states.get(k);
      if (s?.exclusive) waits.push(s.exclusive);
    }
    const start = waits.length === 0 ? Promise.resolve() : Promise.all(waits);
    const result = start.then(() => task());
    const done = result.then(noop, noop);
    for (const k of exclusive) {
      const s = this.state(k);
      s.exclusive = done;
      s.shared = new Set();
    }
    for (const k of shared) this.state(k).shared.add(done);
    void done.then(() => {
      for (const k of exclusive) this.release(k, done);
      for (const k of shared) this.release(k, done);
    });
    return result;
  }

  /** Resolves once every task currently queued on `keys` (shared or exclusive) has settled (does not lock). */
  async wait(keys: readonly string[]): Promise<void> {
    await this.waitFor(keys, true);
  }

  /** Resolves once every exclusive holder currently queued on `keys` has settled (does not lock). */
  async waitExclusive(keys: readonly string[]): Promise<void> {
    await this.waitFor(keys, false);
  }

  /** Resolves when no task is queued or running on any key. */
  async idle(): Promise<void> {
    while (this.states.size > 0) {
      const all: Promise<void>[] = [];
      for (const s of this.states.values()) {
        if (s.exclusive) all.push(s.exclusive);
        for (const p of s.shared) all.push(p);
      }
      await Promise.all(all);
    }
  }

  private async waitFor(keys: readonly string[], includeShared: boolean): Promise<void> {
    const waits: Promise<void>[] = [];
    for (const k of keys) {
      const s = this.states.get(k);
      if (!s) continue;
      if (s.exclusive) waits.push(s.exclusive);
      if (includeShared) for (const p of s.shared) waits.push(p);
    }
    if (waits.length > 0) await Promise.all(waits);
  }

  private state(k: string): KeyState {
    let s = this.states.get(k);
    if (!s) {
      s = { exclusive: null, shared: new Set() };
      this.states.set(k, s);
    }
    return s;
  }

  private release(k: string, done: Promise<void>): void {
    const s = this.states.get(k);
    if (!s) return;
    if (s.exclusive === done) s.exclusive = null;
    s.shared.delete(done);
    if (s.exclusive === null && s.shared.size === 0) this.states.delete(k);
  }
}
