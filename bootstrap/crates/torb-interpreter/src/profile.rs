//! Counters for finding out where the time of a run goes, without a profiler on the machine.
//!
//! They only exist in a build with `--features profile`, and only report when `TORB_PROFILE=1` is set; in the normal
//! build `is_enabled` is a compile-time `false` and every hook folds away. `count`/`add` are exact, `timed` reads the
//! clock twice and is only used around whole native calls (its times are inclusive: a native that calls a closure
//! back counts what the closure does).

use std::cell::RefCell;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU8, Ordering};
use std::time::Instant;

/// Whether the counters are on. Without the feature this is `false` at compile time.
#[inline(always)]
pub fn is_enabled() -> bool {
    cfg!(feature = "profile") && asked_for()
}

fn asked_for() -> bool {
    static STATE: AtomicU8 = AtomicU8::new(2);
    match STATE.load(Ordering::Relaxed) {
        0 => false,
        1 => true,
        _ => {
            let enabled = std::env::var_os("TORB_PROFILE").is_some_and(|value| value != "0");
            STATE.store(u8::from(enabled), Ordering::Relaxed);
            enabled
        }
    }
}

#[derive(Default)]
struct Entry {
    count: u64,
    total: u64,
    nanos: u64,
}

thread_local! {
    static ENTRIES: RefCell<HashMap<&'static str, Entry>> = RefCell::new(HashMap::new());
}

/// One event of this kind.
#[inline(always)]
pub fn count(key: &'static str) {
    if is_enabled() {
        record(key, 0, 0);
    }
}

/// One event, plus an amount (items copied, slots scanned, bytes allocated).
#[inline(always)]
pub fn add(key: &'static str, amount: usize) {
    if is_enabled() {
        record(key, amount as u64, 0);
    }
}

/// One event, with the time it took.
#[inline(always)]
pub fn timed<T>(key: &'static str, body: impl FnOnce() -> T) -> T {
    if !is_enabled() {
        return body();
    }
    let start = Instant::now();
    let result = body();
    record(key, 0, start.elapsed().as_nanos() as u64);
    result
}

fn record(key: &'static str, amount: u64, nanos: u64) {
    ENTRIES.with(|entries| {
        let mut entries = entries.borrow_mut();
        let entry = entries.entry(key).or_default();
        entry.count += 1;
        entry.total += amount;
        entry.nanos += nanos;
    });
}

/// Prints every counter, by time and then by count. Called once at the end of a run.
pub fn report() {
    if !is_enabled() {
        return;
    }
    ENTRIES.with(|entries| {
        let entries = entries.borrow();
        let mut rows: Vec<(&&str, &Entry)> = entries.iter().collect();
        rows.sort_by_key(|(key, entry)| (std::cmp::Reverse(entry.nanos), std::cmp::Reverse(entry.count), **key));
        eprintln!("{:<40} {:>14} {:>16} {:>12}", "counter", "count", "amount", "ms");
        for (key, entry) in rows {
            let amount = if entry.total == 0 { String::new() } else { entry.total.to_string() };
            let milliseconds = if entry.nanos == 0 { String::new() } else { format!("{:.1}", entry.nanos as f64 / 1e6) };
            eprintln!("{key:<40} {:>14} {amount:>16} {milliseconds:>12}", entry.count);
        }
    });
}
