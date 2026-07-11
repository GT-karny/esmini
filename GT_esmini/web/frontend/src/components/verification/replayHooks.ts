/**
 * Replay transport + scene-building hooks for the verification pages
 * (replay + annotate). Split from ReplayTransport.tsx so that file only
 * exports components (react-refresh constraint); the button cluster and
 * timeline components live there.
 */
import { useEffect, useMemo, useRef, useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api, type VdTelemetryFrame, type VerificationTelemetry } from '../../api/client';
import { type RoadGeometry } from '../LiveSceneView';
import type { OsiObject, TrafficLight } from '../../hooks/useOsiStream';

/* ---------- transport hook ---------- */

export interface ReplayState {
  idx: number;
  setIdx: (i: number) => void;
  playing: boolean;
  setPlaying: React.Dispatch<React.SetStateAction<boolean>>;
  speed: number;
  setSpeed: (s: number) => void;
  loop: boolean;
  setLoop: React.Dispatch<React.SetStateAction<boolean>>;
  lastIdx: number;
  atEnd: boolean;
  dt: number;
  frame: VdTelemetryFrame | null;
  reset: () => void;
  stepBy: (d: number) => void;
  togglePlay: () => void;
}

/** Playback state machine over a frame array (idx/play/speed/loop + timer). */
export function useReplay(frames: VdTelemetryFrame[]): ReplayState {
  const [idx, setIdx] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [loop, setLoop] = useState(false);
  const timerRef = useRef<number | null>(null);

  const lastIdx = Math.max(0, frames.length - 1);
  const atEnd = frames.length > 0 && idx >= lastIdx;
  const dt = frames.length > 1 ? Math.max(0.01, frames[1].sim_time - frames[0].sim_time) : 0.05;

  const reset = () => setIdx(0);
  const stepBy = (d: number) => {
    setPlaying(false);
    setIdx((i) => Math.max(0, Math.min(lastIdx, i + d)));
  };
  const togglePlay = () => {
    if (!playing && atEnd && !loop) setIdx(0);
    setPlaying((p) => !p);
  };

  useEffect(() => {
    if (!playing || frames.length === 0) return;
    const period = Math.max(16, (dt * 1000) / speed);
    timerRef.current = window.setInterval(() => {
      setIdx((i) => {
        if (i >= frames.length - 1) {
          if (loop) return 0;
          setPlaying(false);
          return i;
        }
        return i + 1;
      });
    }, period);
    return () => { if (timerRef.current != null) window.clearInterval(timerRef.current); };
  }, [playing, speed, dt, frames.length, loop]);

  const frame: VdTelemetryFrame | null = frames[Math.min(idx, frames.length - 1)] ?? null;
  return { idx, setIdx, playing, setPlaying, speed, setSpeed, loop, setLoop,
           lastIdx, atEnd, dt, frame, reset, stepBy, togglePlay };
}

/* ---------- scene-building hook ---------- */

export interface EventMarker {
  idx: number;
  t: number;
  kind: string;
  label: string;
  color: string;
}

function mk(idx: number, f: VdTelemetryFrame, kind: string, label: string, color: string): EventMarker {
  return { idx, t: f.sim_time, kind, label, color };
}

export interface SceneReplay {
  roadGeometry: RoadGeometry | null;
  currentScene: VerificationTelemetry['scene'][number] | null;
  sceneTrafficLights: TrafficLight[];
  /** ego (+ recorded traffic) at the current frame, WITHOUT any ghost overlay */
  baseEgoObjects: OsiObject[];
  events: EventMarker[];
}

/**
 * Builds LiveSceneView inputs from a recorded run: fetches static road geometry,
 * picks the nearest recorded OSI scene frame, and synthesizes the ego marker when
 * no scene was recorded. Also detects start/stop/lane/override event markers.
 */
export function useSceneReplay(
  telemetry: VerificationTelemetry | undefined,
  frames: VdTelemetryFrame[],
  frame: VdTelemetryFrame | null,
): SceneReplay {
  const scene = useMemo(() => telemetry?.scene ?? [], [telemetry]);

  const pid = telemetry?.meta?.project_id;
  const sfile = telemetry?.meta?.scenario_file;
  const { data: roadGeometryData } = useQuery({
    queryKey: ['road-geometry', pid, sfile],
    queryFn: () => api.getRoadGeometry(pid!, sfile!),
    enabled: !!pid && !!sfile,
    retry: false, // road overlay is optional
  });
  const roadGeometry = (roadGeometryData as RoadGeometry | undefined) ?? null;

  const currentScene = useMemo(() => {
    if (scene.length === 0 || !frame) return null;
    const t = frame.sim_time;
    let lo = 0, hi = scene.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (scene[mid].sim_time <= t) lo = mid; else hi = mid;
    }
    return Math.abs(scene[lo].sim_time - t) <= Math.abs(scene[hi].sim_time - t) ? scene[lo] : scene[hi];
  }, [scene, frame]);

  const sceneTrafficLights = useMemo(
    () => (currentScene?.traffic_lights ?? []) as unknown as TrafficLight[],
    [currentScene],
  );

  const baseEgoObjects: OsiObject[] = useMemo(() => {
    if (!frame) return [];
    return currentScene
      ? (currentScene.objects as unknown as OsiObject[]).slice()
      : [{
          id: 0, name: 'ego', x: frame.ego.x, y: frame.ego.y, z: frame.ego.z, h: frame.ego.h, speed: frame.ego.speed,
          head_light: 'off',
          indicator: frame.indicator.left ? 'left' : frame.indicator.right ? 'right' : 'off',
          brake_light: frame.driver.brake > 0.05 ? 'normal' : 'off',
          obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
        }] as unknown as OsiObject[];
  }, [frame, currentScene]);

  const events = useMemo<EventMarker[]>(() => {
    const out: EventMarker[] = [];
    let moving = false;
    let prevLane: number | null | undefined = undefined;
    let prevOverride = false;
    frames.forEach((f, i) => {
      const sp = f.ego.speed;
      if (!moving && sp > 0.8) { moving = true; if (i > 0) out.push(mk(i, f, 'start', 'start', '#4FD18B')); }
      else if (moving && sp < 0.3) { moving = false; out.push(mk(i, f, 'stop', 'stop', '#E8884F')); }
      const lane = f.ego.lane;
      if (prevLane != null && lane != null && lane !== prevLane)
        out.push(mk(i, f, 'lane', `lane ${prevLane}→${lane}`, '#7B88E8'));
      if (lane != null) prevLane = lane;
      const ov = f.override.lateral || f.override.longitudinal;
      if (ov !== prevOverride) out.push(mk(i, f, 'override', ov ? 'override on' : 'override off', '#E8C84F'));
      prevOverride = ov;
    });
    return out;
  }, [frames]);

  return { roadGeometry, currentScene, sceneTrafficLights, baseEgoObjects, events };
}
