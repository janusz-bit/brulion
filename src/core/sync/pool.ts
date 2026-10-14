/**
 * Tiny promise pool: process a (growable) queue of items with bounded concurrency.
 */

/** Options of `runPool`. */
export interface PoolOptions {
  /** Maximum number of workers running at once (>= 1). */
  concurrency: number;
  /** Stop taking new items once aborted (running ones finish). */
  signal?: AbortSignal | undefined;
}

/**
 * Run `worker` for every item of `items` with at most `concurrency` in parallel. Workers may
 * add more items with `push`. Rejects with the first worker error after running workers
 * settle (callers that want per-item error handling catch inside the worker). Resolves when
 * the queue is drained or, after an abort, when the running workers are done.
 */
export async function runPool<T>(items: Iterable<T>, worker: (item: T, push: (item: T) => void) => Promise<void>, opts: PoolOptions): Promise<void> {
  const queue: T[] = [...items];
  const limit = Math.max(1, Math.floor(opts.concurrency));
  let active = 0;
  let failed = false;
  let failure: unknown;

  return new Promise<void>((resolve, reject) => {
    const finishIfDone = (): void => {
      if (active > 0) return;
      if (failed) reject(failure);
      else resolve();
    };
    const push = (item: T): void => {
      queue.push(item);
      pump();
    };
    function pump(): void {
      while (!failed && !opts.signal?.aborted && active < limit && queue.length > 0) {
        const item = queue.shift() as T;
        active++;
        let p: Promise<void>;
        try {
          p = worker(item, push);
        } catch (e) {
          p = Promise.reject(e);
        }
        p.then(
          () => {
            active--;
            pump();
            finishIfDone();
          },
          (e: unknown) => {
            active--;
            if (!failed) {
              failed = true;
              failure = e;
            }
            finishIfDone();
          },
        );
      }
    }
    pump();
    finishIfDone();
  });
}
