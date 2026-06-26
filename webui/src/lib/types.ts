// types.ts -- the wire contract with the C++ server. Mirrors web_server.cpp's
// fullState() / telemetry JSON, so the whole UI is type-checked against the
// shape the device actually sends. If a field here drifts from the server,
// svelte-check fails the build (npm run build) -- which is the point.

export type Section = 'input' | 'fx' | 'output' | 'hidden';

export interface Param {
  id: string;
  name: string;
  unit: string;
  min: number;
  max: number;
  def: number;
  value: number;
}

export interface Effect {
  type: string; // unique type_id within the chain (API/preset key)
  name: string;
  enabled: boolean;
  section: Section;
  slot: number; // -1 unless it lives in the FX grid
  fsAssign: number; // -1 = unassigned, else 0..3
  fsMode: number; // 0 normal, 1 inverted
  params: Param[];
}

export interface FxKind {
  type: string;
  name: string;
}

export interface BoardState {
  master: number;
  bypassed: boolean;
  fxSlotCount: number;
  footswitches: boolean[]; // FS1..FS4 latched engaged-state
  effects: Effect[];
  fxKinds: FxKind[];
}

export interface Telemetry {
  dspPermille: number;
  xruns: number;
}

// GET /api/tuner -- live pitch readout, polled while the tuner modal is open.
export interface TunerReading {
  engaged: boolean;
  note: number; // 0..11 (C..B), -1 = no confident pitch
  octave: number;
  cents: number; // signed distance to the nearest semitone, ~-50..+50
  freq: number; // Hz, 0 when no pitch
}

// GET /api/pedal-presets?effect=<type_id> -- the knob-snapshot names available
// for a pedal's KIND (so "drive" and "drive-2" share one list).
export interface PedalPresetList {
  kind: string;
  names: string[];
}

// A setlist: an ordered list of rig names to step through on stage.
export interface Setlist {
  name: string;
  rigs: string[];
}
