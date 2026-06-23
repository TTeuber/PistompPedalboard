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
