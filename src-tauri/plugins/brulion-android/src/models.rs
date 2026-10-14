//! DTOs of the `brulion-android` IPC contract (docs/ARCHITECTURE.md) and the pure helpers
//! applied before reminders reach the native scheduler.

use std::collections::HashMap;

use serde::{de::Error as _, Deserialize, Deserializer, Serialize};

/// Most reminders armed at once. Android refuses more than 500 concurrent alarms per app;
/// the rest of the budget is left for native snoozes. The soonest reminders win.
pub const MAX_REMINDERS: usize = 400;

/// Largest timestamp a JS `Date` can hold (±8.64e15 ms).
const MAX_EPOCH_MS: f64 = 8.64e15;

/// `{ granted }` — answer of the permission commands.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Granted {
    pub granted: bool,
}

/// `AndroidReminder` of the contract: one notification for the native scheduler.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AndroidReminder {
    /// int32 notification id (`ScheduledNotification.nativeId`), unique per reminder. A uint32
    /// (e.g. a hash `>>> 0` in JS) is accepted too and wrapped into the int32 range, which
    /// keeps distinct ids distinct.
    #[serde(deserialize_with = "int32_id")]
    pub id: i32,
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub body: String,
    /// When to fire, in ms since the Unix epoch. Accepts any finite JS number (rounded).
    #[serde(deserialize_with = "epoch_ms")]
    pub at: i64,
    /// Alarm: loud, insistent, full-screen; otherwise a regular high-priority reminder.
    #[serde(default)]
    pub alarm: bool,
    /// Note to open when the notification is tapped (VaultPath).
    #[serde(default)]
    pub path: String,
    /// Line of the reminder in that note.
    #[serde(default)]
    pub line: i32,
}

/// Arguments of `schedule_reminders` (also the payload sent to Kotlin's `scheduleReminders`).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ScheduleArgs {
    pub items: Vec<AndroidReminder>,
}

/// Result of `schedule_reminders`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct ScheduleResult {
    /// Reminders armed natively (future ones, snoozes kept by the native side included).
    pub scheduled: u32,
    /// Whether they fire exactly (exact-alarm permission) or may be delayed by the system.
    pub exact: bool,
}

/// Result of `take_activation` and payload of the `notification-activated` event:
/// the note a tapped notification points at. Empty (`{}`) when there is none.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Activation {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub line: Option<i32>,
}

fn int32_id<'de, D: Deserializer<'de>>(d: D) -> Result<i32, D::Error> {
    let n = i64::deserialize(d)?;
    if n < i64::from(i32::MIN) || n > i64::from(u32::MAX) {
        return Err(D::Error::custom(
            "`id` must be an int32 (or uint32) integer",
        ));
    }
    // Two's complement wrap: 0x8000_0000..=0xFFFF_FFFF map onto i32::MIN..=-1.
    Ok(n as u32 as i32)
}

fn epoch_ms<'de, D: Deserializer<'de>>(d: D) -> Result<i64, D::Error> {
    let ms = f64::deserialize(d)?;
    if !ms.is_finite() || ms.abs() > MAX_EPOCH_MS {
        return Err(D::Error::custom(
            "`at` must be a finite time in milliseconds since the Unix epoch",
        ));
    }
    Ok(ms.round() as i64)
}

/// Prepares the set handed to the native scheduler: drops reminders that are not in the
/// future (`at <= now_ms`), keeps the last entry of a duplicated id, orders by time (then
/// id) and keeps the [`MAX_REMINDERS`] soonest.
pub fn normalize_reminders(items: Vec<AndroidReminder>, now_ms: i64) -> Vec<AndroidReminder> {
    let mut by_id: HashMap<i32, AndroidReminder> = HashMap::with_capacity(items.len());
    for item in items {
        by_id.insert(item.id, item);
    }
    let mut out: Vec<AndroidReminder> = by_id.into_values().filter(|r| r.at > now_ms).collect();
    out.sort_by_key(|r| (r.at, r.id));
    out.truncate(MAX_REMINDERS);
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn reminder(id: i32, at: i64) -> AndroidReminder {
        AndroidReminder {
            id,
            title: format!("r{id}"),
            body: String::new(),
            at,
            alarm: false,
            path: "dzienniki/2026_10_14.md".into(),
            line: 3,
        }
    }

    #[test]
    fn parses_the_contract_shape() {
        let r: AndroidReminder = serde_json::from_value(json!({
            "id": -123456,
            "title": "Zadzwoń do mamy — źdźbło ąćęłńóśźż",
            "body": "Zażółć gęślą jaźń",
            "at": 1_792_000_000_000_i64,
            "alarm": true,
            "path": "strony/Łódź.md",
            "line": 7
        }))
        .unwrap();
        assert_eq!(r.id, -123456);
        assert_eq!(r.title, "Zadzwoń do mamy — źdźbło ąćęłńóśźż");
        assert_eq!(r.at, 1_792_000_000_000);
        assert!(r.alarm);
        assert_eq!(r.path, "strony/Łódź.md");
        assert_eq!(r.line, 7);
        // Round trip keeps camelCase field names and an integer `at` for Kotlin's Long.
        let v = serde_json::to_value(&r).unwrap();
        assert_eq!(v["at"], json!(1_792_000_000_000_i64));
        assert!(v["at"].is_i64());
    }

    #[test]
    fn optional_fields_default() {
        let r: AndroidReminder = serde_json::from_value(json!({ "id": 1, "at": 5 })).unwrap();
        assert_eq!(
            (r.title.as_str(), r.body.as_str(), r.path.as_str()),
            ("", "", "")
        );
        assert!(!r.alarm);
        assert_eq!(r.line, 0);
    }

    #[test]
    fn at_accepts_fractional_ms_and_rejects_nonsense() {
        let r: AndroidReminder =
            serde_json::from_value(json!({ "id": 1, "at": 1_792_000_000_000.6_f64 })).unwrap();
        assert_eq!(r.at, 1_792_000_000_001);
        for bad in [json!(9e15), json!(-9e15), json!("jutro"), json!(null)] {
            assert!(
                serde_json::from_value::<AndroidReminder>(json!({ "id": 1, "at": bad })).is_err(),
                "{bad}"
            );
        }
    }

    #[test]
    fn id_is_int32_and_uint32_wraps() {
        let id = |v: serde_json::Value| {
            serde_json::from_value::<AndroidReminder>(json!({ "id": v, "at": 1 })).map(|r| r.id)
        };
        assert_eq!(id(json!(i32::MIN)).unwrap(), i32::MIN);
        assert_eq!(id(json!(-1)).unwrap(), -1);
        assert_eq!(id(json!(i32::MAX)).unwrap(), i32::MAX);
        // uint32 ids (`hash >>> 0` in JS) wrap around instead of failing the whole set.
        assert_eq!(id(json!(2_147_483_648_i64)).unwrap(), i32::MIN);
        assert_eq!(id(json!(u32::MAX)).unwrap(), -1);
        for bad in [
            json!(4_294_967_296_i64),
            json!(i64::from(i32::MIN) - 1),
            json!(1.5),
            json!("7"),
        ] {
            assert!(id(bad.clone()).is_err(), "{bad}");
        }
    }

    #[test]
    fn schedule_args_shape() {
        let args: ScheduleArgs = serde_json::from_value(json!({ "items": [] })).unwrap();
        assert!(args.items.is_empty());
        assert!(serde_json::from_value::<ScheduleArgs>(json!({})).is_err());
    }

    #[test]
    fn results_serialise_like_the_contract() {
        assert_eq!(
            serde_json::to_value(Granted { granted: true }).unwrap(),
            json!({ "granted": true })
        );
        assert_eq!(
            serde_json::to_value(ScheduleResult {
                scheduled: 3,
                exact: false
            })
            .unwrap(),
            json!({ "scheduled": 3, "exact": false })
        );
        assert_eq!(
            serde_json::to_value(Activation::default()).unwrap(),
            json!({})
        );
        let a = Activation {
            path: Some("Książki/Pan Tadeusz.md".into()),
            line: Some(12),
        };
        assert_eq!(
            serde_json::to_value(&a).unwrap(),
            json!({ "path": "Książki/Pan Tadeusz.md", "line": 12 })
        );
        // What Kotlin answers: `{}` or both fields.
        assert_eq!(
            serde_json::from_value::<Activation>(json!({})).unwrap(),
            Activation::default()
        );
        assert_eq!(
            serde_json::from_value::<Activation>(serde_json::to_value(&a).unwrap()).unwrap(),
            a
        );
    }

    #[test]
    fn normalize_drops_past_and_sorts() {
        let now = 1_000;
        let out = normalize_reminders(
            vec![
                reminder(1, 3_000),
                reminder(2, 999),
                reminder(3, 1_000), // exactly now: already due, the JS side reports it as missed
                reminder(4, 2_000),
                reminder(5, 2_000),
            ],
            now,
        );
        let ids: Vec<i32> = out.iter().map(|r| r.id).collect();
        assert_eq!(ids, vec![4, 5, 1]);
    }

    #[test]
    fn normalize_keeps_the_last_duplicate() {
        let mut later = reminder(7, 5_000);
        later.title = "nowsza wersja".into();
        let out = normalize_reminders(vec![reminder(7, 4_000), later.clone()], 0);
        assert_eq!(out, vec![later]);
        // A duplicate that moved into the past removes the reminder.
        let out = normalize_reminders(vec![reminder(8, 4_000), reminder(8, -1)], 0);
        assert!(out.is_empty());
    }

    #[test]
    fn normalize_caps_to_the_soonest() {
        let items: Vec<_> = (0..(MAX_REMINDERS as i32 + 50))
            .rev()
            .map(|i| reminder(i, 10_000 + i64::from(i)))
            .collect();
        let out = normalize_reminders(items, 0);
        assert_eq!(out.len(), MAX_REMINDERS);
        assert_eq!(out.first().unwrap().id, 0);
        assert_eq!(out.last().unwrap().id, MAX_REMINDERS as i32 - 1);
    }

    #[test]
    fn normalize_empty() {
        assert!(normalize_reminders(Vec::new(), 0).is_empty());
    }
}
