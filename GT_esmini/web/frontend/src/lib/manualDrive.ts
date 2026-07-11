/**
 * Manual-drive default network ports.
 *
 * The desktop "manual drive" mode pipes a physical input device (wheel/keyboard)
 * into the simulator over UDP, and — for the RealVehicle physics path — exchanges
 * command/state with an external physics process. These defaults are shared by
 * every place that seeds a ManualDriveConfig (SimulationRunForm, ManualDrivePanel)
 * so the values stay in one place instead of being hand-copied per form.
 */
export const MANUAL_DRIVE_DEFAULT_PORTS = {
  /** UDP port the input network listens on (pedal/steer commands). */
  input: 9100,
  /** UDP port the physics bridge receives commands on. */
  physicsCmd: 9200,
  /** UDP port the physics bridge publishes vehicle state on. */
  physicsState: 9201,
} as const;
