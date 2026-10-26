// Mod 列表格式化 (总览与成绩库共用):
// - stable: legacy 位 → acronym 串 (键数位在前), 无 mod = "-"
// - lazer: acronym + 非默认 settings, 小数倍率如 "DT(1.25×)", 无 mod = "-"
import type { LazerMod } from "../models/types";
import { KEY_MOD_NAMES, MOD_NAMES } from "../models/types";

export function modsText(mods: number): string {
  // DTNC 归一: stable 对 NC 局同时写 DT+NC 两位, osu 语义为 NC (变速不变调,
  // 优先于 DT) — 显示与 mod= 搜索一致重定向为 NC
  if (mods & 512) mods &= ~64;
  const parts: string[] = [];
  for (const [bit, name] of KEY_MOD_NAMES)
    if (mods & bit) { parts.push(name); mods &= ~bit; }
  for (const [bit, name] of MOD_NAMES) if (mods & bit) parts.push(name);
  return parts.length ? parts.join(" ") : "-";
}

// rate 类 mod 的默认速度不显示 (如 DT 1.5× 为默认, 1.25× 才显示)
const LAZER_RATE_DEFAULTS: Record<string, number> = {
  DT: 1.5, NC: 1.5, HT: 0.75,
};

function fmtSetting(v: unknown): string {
  if (typeof v === "number") {
    return Number.isFinite(v) ? String(Number(v.toFixed(2))) : String(v);
  }
  if (typeof v === "boolean") return v ? "true" : "false";
  if (v == null) return "";
  if (typeof v === "object") {
    try {
      return JSON.stringify(v);
    } catch {
      return "[object]";
    }
  }
  return String(v);
}

export function lazerModText(m: LazerMod): string {
  const acronym = m.acronym || "?";
  const settings = m.settings ?? {};
  const shown = Object.entries(settings).filter(([k, v]) => {
    if (
      (k === "speed_change" || k === "scroll_speed") &&
      typeof v === "number" &&
      LAZER_RATE_DEFAULTS[acronym] === v
    )
      return false;
    return true;
  });
  if (!shown.length) return acronym;
  return (
    acronym +
    "(" +
    shown
      .map(([k, v]) => {
        const isRate = k === "speed_change" || k === "scroll_speed";
        return isRate ? fmtSetting(v) + "×" : k + "=" + fmtSetting(v);
      })
      .join(", ") +
    ")"
  );
}

// 总览用: lazer 局优先尾块 mods 列表 (小数倍率/IN/RD 等透传显示),
// stable 局用 legacy 位串。
export function modsDisplay(
  mods: number | undefined,
  lazerMods: LazerMod[] | undefined,
): string {
  if (lazerMods)
    return lazerMods.length ? lazerMods.map(lazerModText).join(" ") : "-";
  return modsText(mods ?? 0);
}
