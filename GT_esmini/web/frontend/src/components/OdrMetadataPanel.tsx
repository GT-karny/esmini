/**
 * OpenDRIVE side-model metadata panel (plan P9a).
 *
 * Fetches GT_esminiLib-extracted metadata for a scenario's road (parse warnings,
 * userData, signal semantics, junction priorities, crosswalks, railroad) and
 * renders it as a collapsible, sectioned panel next to the road preview in the
 * annotation flow. Loading/error states are shown inline (not silently hidden).
 */
import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api, type OdrMetadata } from '../api/client';

interface OdrMetadataPanelProps {
  projectId: string;
  scenarioFile: string;
  /** Collapsed by default; the header toggles the whole panel. */
  defaultOpen?: boolean;
}

export function OdrMetadataPanel({
  projectId,
  scenarioFile,
  defaultOpen = false,
}: OdrMetadataPanelProps) {
  const [open, setOpen] = useState(defaultOpen);

  const { data, error: queryError, isLoading: loading } = useQuery({
    queryKey: ['odr-metadata', projectId, scenarioFile],
    queryFn: () => api.getOdrMetadata(projectId, scenarioFile),
    retry: false,
  });
  const error = queryError
    ? String((queryError as Error)?.message ?? queryError)
    : null;

  const w = data?.warnings;
  const warnCount = w
    ? w.unsupported_elements + w.unsupported_attributes + w.removed16_hits
    : 0;
  // P9b sections may be absent from an older backend payload — degrade to empty.
  const vjs = data?.virtual_junctions ?? [];
  const laneLayers = data?.lane_layers ?? { mode: 'permanent', roads: [] };

  return (
    <div className="rounded border border-glass-edge text-xs">
      {/* Header */}
      <button
        type="button"
        onClick={() => setOpen((v) => !v)}
        className="w-full flex items-center gap-2 px-3 py-2 text-left hover:bg-glass-2 rounded-t"
      >
        <span className={`text-text-tertiary transition-transform ${open ? 'rotate-90' : ''}`}>
          &rsaquo;
        </span>
        <span className="font-medium text-foreground">OpenDRIVE metadata</span>
        {vjs.length > 0 && (
          <span
            className="px-1.5 py-0.5 rounded text-[10px] font-mono bg-primary/20 text-primary"
            title={`${vjs.length} virtual junction(s) — 1.7+ mid-road branching (P6 native)`}
          >
            virtual junction{vjs.length === 1 ? '' : 's'} {vjs.length}
          </span>
        )}
        <span
          className={`ml-auto px-1.5 py-0.5 rounded text-[10px] font-mono ${
            warnCount > 0
              ? 'bg-warning/20 text-warning'
              : 'bg-glass-1 text-text-tertiary'
          }`}
          title="unsupported elements + attributes + removed-in-1.6 hits"
        >
          {warnCount} warning{warnCount === 1 ? '' : 's'}
        </span>
      </button>

      {open && (
        <div className="px-3 pb-3 flex flex-col gap-3 border-t border-glass-edge pt-3">
          {loading && <div className="text-text-tertiary">Loading metadata…</div>}
          {error && !loading && (
            <div className="text-destructive break-words">
              Metadata unavailable: {error}
            </div>
          )}
          {data && !loading && !error && (
            <>
              <ParseWarnings warnings={data.warnings} />
              <UserDataSection
                title="userData"
                items={data.user_data}
                emptyText="No userData blobs"
              />
              {data.data_quality.length > 0 && (
                <UserDataSection
                  title="dataQuality"
                  items={data.data_quality}
                  emptyText="No dataQuality blobs"
                />
              )}
              <SignalsSection signals={data.signals} />
              <VirtualJunctionsSection junctions={vjs} />
              <LaneLayersSection laneLayers={laneLayers} />
              <JunctionPrioritiesSection junctions={data.junction_priorities} />
              <CrosswalksSection crosswalks={data.crosswalks} />
              <RailroadSection railroad={data.railroad} />
            </>
          )}
        </div>
      )}
    </div>
  );
}

/* ---------- section shell ---------- */

function Section({
  title,
  count,
  children,
}: {
  title: string;
  count?: number;
  children: React.ReactNode;
}) {
  return (
    <div className="flex flex-col gap-1">
      <div className="flex items-baseline gap-2">
        <span className="text-[11px] font-medium text-text-secondary uppercase tracking-wide">
          {title}
        </span>
        {typeof count === 'number' && (
          <span className="text-[10px] text-text-tertiary">{count}</span>
        )}
      </div>
      {children}
    </div>
  );
}

function Empty({ text }: { text: string }) {
  return <div className="text-[11px] text-text-tertiary italic">{text}</div>;
}

/* ---------- 1. parse warnings ---------- */

function ParseWarnings({ warnings }: { warnings: OdrMetadata['warnings'] }) {
  const { entries, version } = warnings;
  return (
    <Section title="Parse warnings" count={entries.length}>
      <div className="text-[10px] text-text-tertiary mb-1">
        OpenDRIVE {version.rev_major}.{version.rev_minor} · elem{' '}
        {warnings.unsupported_elements} · attr {warnings.unsupported_attributes} ·
        removed-1.6 {warnings.removed16_hits}
      </div>
      {entries.length === 0 ? (
        <Empty text="No unsupported constructs" />
      ) : (
        <div className="max-h-40 overflow-auto rounded bg-glass-1 p-1.5">
          {entries.map((e, i) => (
            <div
              key={i}
              className="font-mono text-[10px] text-text-secondary whitespace-pre-wrap break-all leading-snug"
            >
              {e}
            </div>
          ))}
        </div>
      )}
    </Section>
  );
}

/* ---------- 2. userData / dataQuality ---------- */

function UserDataSection({
  title,
  items,
  emptyText,
}: {
  title: string;
  items: OdrMetadata['user_data'];
  emptyText: string;
}) {
  // Group by context_id (road).
  const groups = new Map<string, OdrMetadata['user_data']>();
  for (const it of items) {
    const key = it.context_id || '(none)';
    const arr = groups.get(key) ?? [];
    arr.push(it);
    groups.set(key, arr);
  }

  return (
    <Section title={title} count={items.length}>
      {items.length === 0 ? (
        <Empty text={emptyText} />
      ) : (
        <div className="flex flex-col gap-1.5">
          {[...groups.entries()].map(([ctx, rows]) => (
            <div key={ctx} className="flex flex-col gap-1">
              <div className="text-[10px] text-text-tertiary">road {ctx}</div>
              {rows.map((r, i) => (
                <UserDataItem key={i} item={r} />
              ))}
            </div>
          ))}
        </div>
      )}
    </Section>
  );
}

function UserDataItem({ item }: { item: OdrMetadata['user_data'][number] }) {
  const [open, setOpen] = useState(false);
  return (
    <div className="rounded bg-glass-1">
      <button
        type="button"
        onClick={() => setOpen((v) => !v)}
        className="w-full flex items-center gap-1.5 px-2 py-1 text-left hover:bg-glass-2 rounded"
      >
        <span className={`text-text-tertiary transition-transform ${open ? 'rotate-90' : ''}`}>
          &rsaquo;
        </span>
        <span className="font-mono text-[10px] text-text-secondary truncate">
          {item.owner_path}
        </span>
      </button>
      {open && (
        <pre className="max-h-40 overflow-auto px-2 pb-2 font-mono text-[10px] text-text-secondary whitespace-pre-wrap break-all">
          {item.xml}
        </pre>
      )}
    </div>
  );
}

/* ---------- 3. signals (semantics) ---------- */

function SignalsSection({ signals }: { signals: OdrMetadata['signals'] }) {
  return (
    <Section title="Signals (semantics)" count={signals.length}>
      {signals.length === 0 ? (
        <Empty text="No signals" />
      ) : (
        <div className="overflow-x-auto rounded bg-glass-1">
          <table className="w-full text-[10px] border-collapse">
            <thead>
              <tr className="text-text-tertiary text-left">
                <th className="px-1.5 py-1 font-medium">road</th>
                <th className="px-1.5 py-1 font-medium">signal</th>
                <th className="px-1.5 py-1 font-medium">summary</th>
                <th className="px-1.5 py-1 font-medium">flags</th>
              </tr>
            </thead>
            <tbody>
              {signals.map((s, i) => (
                <tr key={i} className="border-t border-glass-edge align-top">
                  <td className="px-1.5 py-1 font-mono text-text-secondary">{s.road_id}</td>
                  <td className="px-1.5 py-1 font-mono text-text-secondary">{s.signal_id}</td>
                  <td className="px-1.5 py-1 text-text-secondary">
                    {s.has_semantics ? (
                      <span className="flex flex-wrap gap-x-2">
                        {s.semantics.speeds.length > 0 && (
                          <span>{s.semantics.speeds.length} speed(s)</span>
                        )}
                        {s.semantics.lane_types.length > 0 && (
                          <span>lanes: {s.semantics.lane_types.join(',')}</span>
                        )}
                        {s.semantics.priority_types.length > 0 && (
                          <span>prio: {s.semantics.priority_types.join(',')}</span>
                        )}
                        {s.semantics.prohibited.length > 0 && (
                          <span>{s.semantics.prohibited.length} prohibited</span>
                        )}
                        {s.semantics.warning_count > 0 && (
                          <span className="text-warning">{s.semantics.warning_count} warn</span>
                        )}
                        {(s.dependencies.length > 0 || s.references.length > 0) && (
                          <span className="text-text-tertiary">
                            {s.dependencies.length} dep · {s.references.length} ref
                          </span>
                        )}
                      </span>
                    ) : (
                      <span className="text-text-tertiary italic">no semantics</span>
                    )}
                  </td>
                  <td className="px-1.5 py-1">
                    {s.temporary && <span className="text-warning mr-1">temp</span>}
                    {s.invalidated && <span className="text-destructive">invalid</span>}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </Section>
  );
}

/* ---------- 3b. virtual junctions (P9b / P6) ---------- */

/**
 * Span marker: the [s_start, s_end] window rendered against the main road's
 * full length as a horizontal bar. Falls back to text-only when the road
 * length is unknown (<= 0).
 */
function SpanBar({
  start,
  end,
  total,
  title,
}: {
  start: number;
  end: number;
  total: number;
  title?: string;
}) {
  if (!(total > 0) || end < start) return null;
  const left = Math.max(0, Math.min(100, (start / total) * 100));
  const width = Math.max(1, Math.min(100 - left, ((end - start) / total) * 100));
  return (
    <div
      className="relative h-1.5 rounded bg-glass-2 overflow-hidden"
      title={title ?? `s ${start}–${end} of ${total}`}
    >
      <div
        className="absolute top-0 h-full bg-primary/70 rounded"
        style={{ left: `${left}%`, width: `${width}%` }}
      />
    </div>
  );
}

function VirtualJunctionsSection({
  junctions,
}: {
  junctions: OdrMetadata['virtual_junctions'];
}) {
  if (junctions.length === 0) return null; // sparse: hide entirely on legacy assets
  return (
    <Section title="Virtual junctions" count={junctions.length}>
      <div className="flex flex-col gap-1.5">
        {junctions.map((vj, i) => (
          <div key={i} className="rounded bg-glass-1 px-2 py-1 flex flex-col gap-1">
            <div className="flex items-center gap-1.5 flex-wrap">
              <span className="px-1 py-0.5 rounded text-[9px] font-mono bg-primary/20 text-primary">
                virtual junction
              </span>
              <span className="font-mono text-[10px] text-text-secondary">
                {vj.junction_id}
                {vj.name && <span className="text-text-tertiary"> ({vj.name})</span>}
              </span>
            </div>
            <div className="font-mono text-[10px] text-text-tertiary">
              main road {vj.main_road_id} · s {vj.s_start}–{vj.s_end}
              {vj.orientation && <> · orient {vj.orientation}</>}
              {' · '}
              {vj.anchor_count} anchor{vj.anchor_count === 1 ? '' : 's'} ·{' '}
              {vj.connection_count} connection{vj.connection_count === 1 ? '' : 's'}
            </div>
            <SpanBar
              start={vj.s_start}
              end={vj.s_end}
              total={vj.main_road_length}
              title={`junction ${vj.junction_id}: s ${vj.s_start}–${vj.s_end} on road ${vj.main_road_id} (length ${vj.main_road_length})`}
            />
          </div>
        ))}
      </div>
    </Section>
  );
}

/* ---------- 3c. lane layers (P9b / P8) ---------- */

function LaneLayersSection({ laneLayers }: { laneLayers: OdrMetadata['lane_layers'] }) {
  const { mode, roads } = laneLayers;
  if (roads.length === 0) return null; // sparse: only 1.9 multi-layer assets appear
  return (
    <Section title="Lane layers" count={roads.length}>
      <div className="text-[10px] text-text-tertiary mb-1">
        process mode:{' '}
        <span className={`font-mono ${mode === 'temporary' ? 'text-warning' : ''}`}>
          {mode}
        </span>{' '}
        (env GT_ODR_LANE_LAYERS, latched per process)
      </div>
      <div className="flex flex-col gap-1.5">
        {roads.map((rd, i) => {
          // Scale for the coverage bar: furthest authored s across all layers,
          // or the temporary range end when it reaches beyond.
          const maxS = Math.max(
            rd.temp_s_end,
            ...rd.layers.flatMap((l) =>
              l.sections.map((sec) => sec.s + (sec.has_length ? sec.length : 0)),
            ),
          );
          return (
            <div key={i} className="rounded bg-glass-1 px-2 py-1 flex flex-col gap-1">
              <div className="font-mono text-[10px] text-text-secondary">
                road {rd.road_id}
                <span className="text-text-tertiary"> · active {rd.active_mode}</span>
              </div>
              <div className="font-mono text-[10px] text-text-tertiary">
                {rd.layers.map((l, k) => (
                  <span key={k}>
                    {k > 0 && ' · '}
                    {l.name}: {l.sections.length} section
                    {l.sections.length === 1 ? '' : 's'}
                    {l.lane_offset_count > 0 && <>, {l.lane_offset_count} offset(s)</>}
                  </span>
                ))}
              </div>
              {rd.has_temporary && (
                <>
                  <div className="font-mono text-[10px] text-warning">
                    temporary covers s {rd.temp_s_start}–{rd.temp_s_end}
                  </div>
                  <SpanBar
                    start={rd.temp_s_start}
                    end={rd.temp_s_end}
                    total={maxS}
                    title={`temporary layer coverage s ${rd.temp_s_start}–${rd.temp_s_end}`}
                  />
                </>
              )}
            </div>
          );
        })}
      </div>
    </Section>
  );
}

/* ---------- 4. junction priorities ---------- */

function JunctionPrioritiesSection({
  junctions,
}: {
  junctions: OdrMetadata['junction_priorities'];
}) {
  return (
    <Section title="Junction priorities" count={junctions.length}>
      {junctions.length === 0 ? (
        <Empty text="No junction priorities" />
      ) : (
        <div className="flex flex-col gap-1.5">
          {junctions.map((j, i) => (
            <div key={i} className="rounded bg-glass-1 px-2 py-1">
              <div className="font-mono text-[10px] text-text-secondary">
                junction {j.junction_id}
                {j.type && <span className="text-text-tertiary"> ({j.type})</span>}
              </div>
              {j.priorities.length === 0 ? (
                <div className="text-[10px] text-text-tertiary italic">no pairs</div>
              ) : (
                <div className="flex flex-col">
                  {j.priorities.map((p, k) => (
                    <span key={k} className="font-mono text-[10px] text-text-secondary">
                      {p.high} &gt; {p.low}
                    </span>
                  ))}
                </div>
              )}
            </div>
          ))}
        </div>
      )}
    </Section>
  );
}

/* ---------- 5. crosswalks ---------- */

function CrosswalksSection({ crosswalks }: { crosswalks: OdrMetadata['crosswalks'] }) {
  return (
    <Section title="Crosswalks" count={crosswalks.length}>
      {crosswalks.length === 0 ? (
        <Empty text="No crosswalks" />
      ) : (
        <div className="overflow-x-auto rounded bg-glass-1">
          <table className="w-full text-[10px] border-collapse">
            <thead>
              <tr className="text-text-tertiary text-left">
                <th className="px-1.5 py-1 font-medium">junction</th>
                <th className="px-1.5 py-1 font-medium">id</th>
                <th className="px-1.5 py-1 font-medium">crossing road</th>
              </tr>
            </thead>
            <tbody>
              {crosswalks.map((c, i) => (
                <tr key={i} className="border-t border-glass-edge">
                  <td className="px-1.5 py-1 font-mono text-text-secondary">{c.junction_id}</td>
                  <td className="px-1.5 py-1 font-mono text-text-secondary">{c.id}</td>
                  <td className="px-1.5 py-1 font-mono text-text-secondary">{c.crossing_road}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </Section>
  );
}

/* ---------- 6. railroad (inert) ---------- */

function RailroadSection({ railroad }: { railroad: OdrMetadata['railroad'] }) {
  const { switches, stations } = railroad;
  const empty = switches.length === 0 && stations.length === 0;
  return (
    <Section title="Railroad" count={switches.length + stations.length}>
      <div className="text-[10px] text-text-tertiary italic mb-1">
        inert (stored only) — no runtime effect
      </div>
      {empty ? (
        <Empty text="No railroad data" />
      ) : (
        <div className="flex flex-col gap-2">
          {switches.length > 0 && (
            <div className="flex flex-col gap-1">
              <div className="text-[10px] text-text-tertiary">switches ({switches.length})</div>
              {switches.map((sw, i) => (
                <div key={i} className="rounded bg-glass-1 px-2 py-1 font-mono text-[10px] text-text-secondary">
                  <div>
                    road {sw.road_id} · {sw.name || '(unnamed)'} · id {sw.id} · {sw.position}
                  </div>
                  <div className="text-text-tertiary">
                    main {sw.main_track?.id}@{sw.main_track?.s} → side {sw.side_track?.id}@{sw.side_track?.s}
                    {sw.partner && <> · partner {sw.partner.name || sw.partner.id}</>}
                  </div>
                </div>
              ))}
            </div>
          )}
          {stations.length > 0 && (
            <div className="flex flex-col gap-1">
              <div className="text-[10px] text-text-tertiary">stations ({stations.length})</div>
              {stations.map((st, i) => (
                <div key={i} className="rounded bg-glass-1 px-2 py-1 text-[10px] text-text-secondary">
                  <div className="font-mono">
                    {st.id} · {st.name || '(unnamed)'}
                    {st.type && <span className="text-text-tertiary"> ({st.type})</span>}
                  </div>
                  {st.platforms.map((pf, k) => (
                    <div key={k} className="text-text-tertiary font-mono pl-2">
                      platform {pf.id} {pf.name && `(${pf.name})`}:{' '}
                      {pf.segments.map((sg, m) => (
                        <span key={m}>
                          {m > 0 && ', '}
                          road {sg.road_id} s{sg.s_start}–{sg.s_end} {sg.side}
                        </span>
                      ))}
                    </div>
                  ))}
                </div>
              ))}
            </div>
          )}
        </div>
      )}
    </Section>
  );
}
